#include "tflow-build-cfg.hpp"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>

#include <netinet/ip.h>

#include <arpa/inet.h>
#include <poll.h>

#include "tflow-glib.hpp"
#include "tflow-common.hpp"

#include <json11.hpp>

#include "encoder-v4l2/tflow-v4l2enc.hpp"
#include "tflow-rtp-vstreamer.hpp"

TFlowRTPStreamerCfg tflow_rtp_streamer_cfg;

static struct timespec diff_timespec(
    const struct timespec* time1,
    const struct timespec* time0)
{
    assert(time1);
    assert(time0);
    struct timespec diff = { .tv_sec = time1->tv_sec - time0->tv_sec, //
        .tv_nsec = time1->tv_nsec - time0->tv_nsec };
    if (diff.tv_nsec < 0) {
        diff.tv_nsec += 1000000000; // nsec/sec
        diff.tv_sec--;
    }
    return diff;
}

static double diff_timespec_msec(
    const struct timespec* time1,
    const struct timespec* time0)
{
    struct timespec d_tp = diff_timespec(time1, time0);
    return d_tp.tv_sec * 1000 + (double)d_tp.tv_nsec / (1000 * 1000);
}


static gboolean tflow_rtp_vstreamer_on_idle(gpointer data)
{
    TFlowRTPVStreamer* rtp_streamer = (TFlowRTPVStreamer*)data;

    rtp_streamer->onIdle_no_ts();

    return G_SOURCE_CONTINUE;
}

void TFlowRTPVStreamer::CloseUDP()
{
    if (sck_fd != -1) {
        close(sck_fd);
        sck_fd = -1;
    }
}

int TFlowRTPVStreamer::parseCfgAddrPort(struct sockaddr_in *sock_addr_out, const char *cfg_addr_port_in)
{
    const char *colon = strchr(cfg_addr_port_in, ':');
    char addr_clone[128];
    const char *addr;
    uint16_t port = 0;

    if ( colon ) {
        // Port specified - lets split port and address
        strncpy(addr_clone, cfg_addr_port_in, (colon - cfg_addr_port_in));
        addr_clone[(colon - cfg_addr_port_in)] = 0;
        addr = addr_clone;
        char *endptr, *str;
        port = strtol(colon+1, &endptr, 10);
        if ( endptr == 0 || endptr > colon + 1 ) {
            // port OK or some garbage at the end
            sock_addr_out->sin_port = htons(port);
        } 
    }
    else {
        // No port, only address
        addr = cfg_addr_port_in;
    }

    if ( port == 0 ) {
        sock_addr_out->sin_port = htons(21040);
    }

    unsigned char buf[sizeof(in_addr)];
    sock_addr_out->sin_family = AF_INET;
    if (1 > inet_pton(sock_addr_out->sin_family, addr, &sock_addr_out->sin_addr) ) {
        // SISO mode
        inet_pton(sock_addr_out->sin_family, "127.0.0.1", &sock_addr_out->sin_addr);
        return -1;
    }

    return 0;
}

int TFlowRTPVStreamer::OpenUDP()
{
    int rc;
    
    addr_nok = 0;
    addr_nok |= parseCfgAddrPort(&remote_addr, cfg->udp_remote_addr.v.c_str);
    addr_nok |= parseCfgAddrPort(&local_addr,  cfg->udp_local_addr.v.c_str);

    if ( addr_nok ) {
        g_warning("TFlowUDP: Bad address format");
        return -1;
    }

    sck_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);  // In case of blocking we will just drop a frame
    if (sck_fd == -1) {
        g_warning("TFlowUDPStreamer: Can't create socket (%d) - %s", errno, strerror(errno));
        return -1;
    }

    char addr_str_buf[128];
    const char *addr_str = inet_ntop(local_addr.sin_family, 
        (const void*)&local_addr.sin_addr, addr_str_buf, sizeof(addr_str_buf));

	// bind socket to port
    rc = bind(sck_fd, (const struct sockaddr*)&local_addr, sizeof(local_addr));
	if ( rc == -1) {
        g_warning("TFlowUDPStreamer: Can't bind to %s:%d (%d) - %s", 
            addr_str, ntohs(local_addr.sin_port), errno, strerror(errno));
        addr_nok = 1;
        return -1;
	}

	g_info("TFlowUDPStreamer: Bond sucessfully on %s:%d", 
        addr_str, ntohs(local_addr.sin_port));

    return 0;
}

int TFlowRTPVStreamer::onFrameEncoded(TFlowBuf &tflow_buf)
{
    size_t enc_buf_len = tflow_buf.v4l2_buf.m.planes->bytesused;
    uint8_t *enc_buf = (uint8_t *)tflow_buf.start;     
    uint32_t tot_enc_bytes_sent = 0;
   
    assert(tflow_buf.state == TFlowBuf::BUF_STATE_APP);

    // RTP timestamp is in 90kHz samples, thus convert v4l2 timestamp to this form
    uint32_t rtp_ts = (tflow_buf.v4l2_buf.timestamp.tv_sec * 1000000 +
        tflow_buf.v4l2_buf.timestamp.tv_usec) * 9 / 100;

#pragma pack(push, 1)

    struct rtp_header rtp_hdr = {
        .version = 2,
        .padding = 0,
        .extension = 0,
        .csrc_count = 1,
        .mark = 0,
        .payload_type = (uint8_t)(payload_type & 0x7F),
        .seq = 0,           // RTP packet sequency. Not a frame sequence!
        .timestamp = rtp_ts,
        .ssrc = 0,
        .csrc = 0
    };
#pragma pack(pop)

    if (tflow_buf.v4l2_buf.flags & V4L2_BUF_FLAG_PFRAME)  {
        rtp_hdr.payload_type = 99;  // 96/97 for H264, 98 key / 99delta for H265
    }
    if (tflow_buf.v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) {
        rtp_hdr.payload_type = 98;  // 96/97 for H264, 98 key / 99delta for H265
    }

    do {
        uint32_t enc_bytes_to_send = 0;

        // If socket isn't active don't even try to send
        if (sck_state_flag.v != Flag::SET) break;
        
        // Split packet to samller size segments as native IP packet is 1400 
        // bytes long. Max UDP packet is 65535
        rtp_hdr.extension = 0;
        rtp_hdr.mark = 0;

        int iov_idx = 0;
        enc_iov[iov_idx].iov_base = (caddr_t)&rtp_hdr;
        enc_iov[iov_idx].iov_len = sizeof(rtp_hdr);
        iov_idx++;

        // TODO: Actually buffer must be split on codec NAL boundary
        enc_bytes_to_send = enc_buf_len - tot_enc_bytes_sent;
        int max_enc_bytes = 32768 - tflow_buf.aux_data_len;
        if (enc_bytes_to_send > max_enc_bytes) {
            // Encoded frame doesn't fit to one packet - send only part
            enc_bytes_to_send = max_enc_bytes;
        }
        else {
            // Encoded frame does fit UDP/RTP packet mark it as last
            rtp_hdr.mark = 1;

            // Last segment of the frame may contain AUX data as RTP extension
            if (tflow_buf.aux_data_len) {
                struct rtp_extension rtp_ext = {
                    .id = 0x5254,          // RT
                    .len = (uint16_t)((tflow_buf.aux_data_len + 3) >> 2)
                };

                rtp_hdr.extension = 1;

                enc_iov[iov_idx].iov_base = (caddr_t)&rtp_ext;
                enc_iov[iov_idx].iov_len = sizeof(rtp_ext);
                iov_idx++;

                enc_iov[iov_idx].iov_base = (caddr_t)tflow_buf.aux_data;
                enc_iov[iov_idx].iov_len = rtp_ext.len << 2;    // Some garbage may appear as padding to 32bit boundary
                iov_idx++;
            }
        }

        enc_iov[iov_idx].iov_base = (caddr_t)enc_buf;
        enc_iov[iov_idx].iov_len = enc_bytes_to_send;
        iov_idx++;

        enc_buf += enc_bytes_to_send;
        tot_enc_bytes_sent += enc_bytes_to_send;

        enc_mh.msg_control = nullptr;
        enc_mh.msg_controllen = 0;
        enc_mh.msg_flags = 0;

        enc_mh.msg_name = (caddr_t)&remote_addr;
        enc_mh.msg_namelen = sizeof(remote_addr);
        enc_mh.msg_iov = enc_iov;
        enc_mh.msg_iovlen = iov_idx;
        assert(iov_idx <= ARRAYSIZE(enc_iov));

        rtp_hdr.seq = enc_seq++;

        if (sck_fd != -1) {
            int rc = 0;
            rc = sendmsg(sck_fd, &enc_mh, 0);            /* no flags used */
            if (rc == -1) {
                int err = errno;
                if (err == EWOULDBLOCK || err == EAGAIN) {
                    g_warning("TFlow RTP Streamer: Blocking - drops the frame");
                }
                else {
                    g_warning("TFlow RTP Streamer: Can't send (%d) - %s", errno, strerror(errno));
                    sck_state_flag.v = Flag::FALL;
                    break;
                }
            }

#if FRAME_DELAY_PROFILE
            PRESC(0x1F) {
                struct timespec buf_ts;
                struct timespec now_ts;
                buf_ts.tv_sec = tflow_buf.ts.tv_sec;
                buf_ts.tv_nsec = tflow_buf.ts.tv_usec * 1000;
                clock_gettime(CLOCK_MONOTONIC, &now_ts);
                double dt = diff_timespec_msec(&now_ts, &buf_ts);
                g_critical("== 5 == Time 2 Send                [%d] %f (%d, %d)",
                    tflow_buf.sequence, dt, buf_ts.tv_nsec, now_ts.tv_nsec);
            }               
#endif
        }
        assert(tot_enc_bytes_sent <= enc_buf_len);

    } while (tot_enc_bytes_sent != enc_buf_len);

    encoder->enqueueOutputBuffer(tflow_buf);

    return 0;
}

#if 0
void TFlowRTPVStreamer::fillBuffer(TFlowBuf& buf_enc, const TFlowBuf& buf_in)
{
    const uint8_t* data_buff;
    size_t data_buff_len;

    data_buff = (uint8_t*)buf_in.start;
    data_buff_len = buf_in.length;

    // AV: Should be ommitted in case of AUX data become much bigger.
    //     For ex. passing as an argument, as buf_in also holds copy of AUX data.
    //     This excessive copy can be ommited.
    memcpy(buf_enc.aux_data, buf_in.aux_data, buf_in.aux_data_len);
    buf_enc.aux_data_len = buf_in.aux_data_len;

    buf_enc.ts = buf_in.ts;
    buf_enc.sequence = buf_in.sequence;
    
    if (in_frame_fmt == V4L2_PIX_FMT_NV12) {
        // Streamer uses NV12 as an input format
        g_critical("RTPVStreamer: bad frame fmt 0x%04X", in_frame_fmt);
        assert(0);
        // ^^^ NV12 needs to be converted to BGRA 
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
    else if (in_frame_fmt == V4L2_PIX_FMT_ABGR32) {
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
    else if (in_frame_fmt == V4L2_PIX_FMT_GREY) {
        // Convert GREY to NV12. 
        // Y += 16, U = 128, V = 128;
        // Let's ignore proper conversion (Y+=16) for a while, just copy Y 
        // component. UV component are not affected by GREY frame thus they can
        // be prefilled upon buffer creation. See TFlowWsVStreamer::restart()
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
}

int TFlowRTPVStreamer::consumeBuffer(TFlowBuf& buf)
{
    // Application returns back our buffer for further processing.
    // Upon encoding onFrameEncoded() callback will be triggered.
    if (encoder) {
        encoder->enqueueInputBuffer(buf);
    }
#if CODE_BROWSE
    TFlowRTPVStreamer::onFrameEncoded(buf_idx);

#endif
    return 0;
}

TFlowBuf* TFlowRTPVStreamer::getFreeBuffer() 
{
    // Application requests buffer for feeding
    return encoder ? encoder->getFreeInputBuffer() : nullptr;
}
#endif

void TFlowRTPVStreamer::onIdle_no_ts()
{
    // Called as a kick 
    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    onIdle(now_ts);
}

void TFlowRTPVStreamer::onIdle(struct timespec now_ts)
{
    if (buf_cli_in_stream) {
        buf_cli_in_stream->onIdle(now_ts);
    }

    // TODO: Rework to use generic UDP class
    if (sck_state_flag.v == Flag::CLR) {
        if (diff_timespec_msec(&now_ts, &last_conn_check_ts) > 1000) {
            last_conn_check_ts = now_ts;
            sck_state_flag.v = Flag::RISE;
        }
        return;
    }

    if (sck_state_flag.v == Flag::SET) {
        // Normal operation. 
        if (diff_timespec_msec(&now_ts, &last_send_ts) > 1000) {
            // Do something lazy;
            // Ping?
        }
        return;
    }

    if (sck_state_flag.v == Flag::UNDEF || sck_state_flag.v == Flag::RISE) {
        int rc;

        rc = OpenUDP();
        if (rc) {
            sck_state_flag.v = Flag::FALL;
        }
        else {
            sck_state_flag.v = Flag::SET;
            //app_onConnect();
            // TODO: send "need a KEY frame" signal to encoder if already running
            // if (encoder) encoder->need_key();
        }
        return;
    }

    if (sck_state_flag.v == Flag::FALL) {
        // UDP normally won't fail, thus probably user change address and we
        // have to reopen the socket.
        CloseUDP();

        // Try to reconnect later
        sck_state_flag.v = Flag::CLR;
    }
}

void TFlowRTPVStreamer::onConfigValidate(json11::Json::object& j_out_params,
    TFlowRTPStreamerCfg::cfg_rtp_streamer* rw_cfg)
{
}

int TFlowRTPVStreamer::onConfig()
{
    if (cfg->udp_local_addr.flags & TFlowCtrl::FIELD_FLAG::CHANGED ||
        cfg->udp_remote_addr.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {
        sck_state_flag.v = Flag::FALL;
        // TODO: Stop encoder and restart from a key frame upon a new socket
        //       opened.
        g_idle_add((GSourceFunc)tflow_rtp_vstreamer_on_idle, this);
    }

    if (cfg->src.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {

        stop();

        // Close current input stream in any case
        if (buf_cli_in_stream) {
            delete buf_cli_in_stream;
            buf_cli_in_stream = nullptr;
        }

        TFlowRTPStreamerUI::VIDEO_SRC src = (TFlowRTPStreamerUI::VIDEO_SRC)cfg->src.v.num;

        const char *srv_name = 
            (src == TFlowRTPStreamerUI::VIDEO_SRC_CAM0) ? "com.reedl.tflow.capture0.buf-server" :
            (src == TFlowRTPStreamerUI::VIDEO_SRC_CAM1) ? "com.reedl.tflow.capture1.buf-server" :
            (src == TFlowRTPStreamerUI::VIDEO_SRC_PROC) ? "com.reedl.tflow.process.buf-server"  : nullptr;

        if (srv_name) {
            buf_cli_in_stream = new TFlowBufCli(
                "TFlowStreamer_RTP", srv_name, 0,
                std::bind(&TFlowRTPVStreamer::onFrame,    this, std::placeholders::_1, std::placeholders::_2),   // TFlowBufCli::app_onFrame()
                std::bind(&TFlowRTPVStreamer::onSrcReady, this, std::placeholders::_1, std::placeholders::_2),   // TFlowBufCli::app_onSrcReady()
                std::bind(&TFlowRTPVStreamer::onSrcGone,  this, std::placeholders::_1),                          // TFlowBufCli::app_onSrcGone()
                nullptr,
                nullptr);
        }

        // In case of input source changed, other configuration doesn't matter
        // as it was applyed on SrcReady
        return 0;

    } // End of SRC changed

    // Apply WebSocket streamer configuration here
    if (encoder && cfg->v4l2_enc.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {

        int rc = encoder->onConfig();
        if (rc == -1) {
            // New configuration can be applied through full restart only
            restart();
        }
    }

    return 0;

}

void TFlowRTPVStreamer::onSrcGone(int src_id)
{
    stop();
}

void TFlowRTPVStreamer::onSrcReady(int src_id, const struct frame_info &_in_frame_info)
{
    in_frame_info = _in_frame_info;

    restart();
}

void TFlowRTPVStreamer::onFrame(int src_id, const TFlowBuf &buf_in)
{
    if (sck_state_flag.v != Flag::SET) {
        return;
    }

    // TODO: Check received src_id vs configured source

    if (encoder) {
        encoder->onFrame(buf_in);
    }
}

TFlowRTPVStreamer::TFlowRTPVStreamer(
    const TFlowRTPStreamerCfg::cfg_rtp_streamer* rtp_streamer_cfg)
{
    memset(&in_frame_info, 0, sizeof(struct frame_info));
    encoder = nullptr;
    buf_cli_in_stream = nullptr;

    last_conn_check_ts.tv_nsec = 0;
    last_conn_check_ts.tv_sec = -999;

    last_send_ts.tv_nsec = 0;
    last_send_ts.tv_sec = -999;

    cfg = rtp_streamer_cfg;
    last_idle_check = 0;
    addr_nok = 1;
}

void TFlowRTPVStreamer::stop()
{
    if (encoder) {
        delete encoder;
        encoder = nullptr;
        memset(&in_frame_info, 0, sizeof(in_frame_info));
    }
}

void TFlowRTPVStreamer::restart() 
{
    TFlowEncCfg::cfg_v4l2_enc *v4l2_enc_cfg = 
        (TFlowEncCfg::cfg_v4l2_enc*)cfg->v4l2_enc.v.ref;

    stop();

    // ATT: Encoder can be reconfigured in runtime, i.e. recreated. Thus, 
    //      DO NOT assume pointer is always exists.
    encoder = new TFlowEnc(in_frame_info, v4l2_enc_cfg,
        std::bind(&TFlowRTPVStreamer::onFrameEncoded, this, std::placeholders::_1));

    if (v4l2_enc_cfg->codec.v.num == TFlowEncUI::ENC_CODEC_H264) {
        payload_type = 96;
    }
    else if (v4l2_enc_cfg->codec.v.num == TFlowEncUI::ENC_CODEC_H265) {
        payload_type = 98;
    } else {
        g_critical("RTP Streamer: Bad codec type - %d", v4l2_enc_cfg->codec.v.num);
        assert(0);
    }

    enc_seq = 0;
}

TFlowRTPVStreamer::~TFlowRTPVStreamer()
{
    CloseUDP();
    
    stop();
}

#include "../tflow-build-cfg.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <thread>

#include <glib-unix.h>

#include <json11.hpp>

#include "../encoder-v4l2/tflow-v4l2enc.hpp"
#include "tflow-ws-vstreamer.hpp"

TFlowWSStreamerCfg tflow_ws_streamer_cfg;

struct user {
  const char *name, *pass, *access_token;
};

static struct user *authenticate(struct mg_http_message *hm) {
  // In production, make passwords strong and tokens randomly generated
  // In this example, user list is kept in RAM. In production, it can
  // be backed by file, database, or some other method.
  static struct user users[] = {
      {"admin", "admin", "admin_token"},
      {"user1", "user1", "user1_token"},
      {"user2", "user2", "user2_token"},
      {NULL, NULL, NULL},
  };
  char user[64], pass[64];
  struct user *u, *result = NULL;
  mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));
  MG_VERBOSE(("user [%s] pass [%s]", user, pass));

  if (user[0] != '\0' && pass[0] != '\0') {
    // Both user and password is set, search by user/password
    for (u = users; result == NULL && u->name != NULL; u++)
      if (strcmp(user, u->name) == 0 && strcmp(pass, u->pass) == 0) result = u;
  } else if (user[0] == '\0') {
    // Only password is set, search by token
    for (u = users; result == NULL && u->name != NULL; u++)
      if (strcmp(pass, u->access_token) == 0) result = u;
  }
  return result;
}

/* ********************************************************************* 
 *                                                                     *
 * Clones of original Mongoose mkhdr() and mg_ws_mask() functions.     *
 * Do not change! Copy-Paste from original mongoose only.              *
 *                                                                     *
 ***********************************************************************/
size_t _mkhdr(size_t len, int op, bool is_client, uint8_t* buf)
{
  size_t n = 0;
  buf[0] = (uint8_t) (op | 128);
  if (len < 126) {
    buf[1] = (unsigned char) len;
    n = 2;
  } else if (len < 65536) {
    uint16_t tmp = mg_htons((uint16_t) len);
    buf[1] = 126;
    memcpy(&buf[2], &tmp, sizeof(tmp));
    n = 4;
  } else {
    uint32_t tmp;
    buf[1] = 127;
    tmp = mg_htonl((uint32_t) (((uint64_t) len) >> 32));
    memcpy(&buf[2], &tmp, sizeof(tmp));
    tmp = mg_htonl((uint32_t) (len & 0xffffffffU));
    memcpy(&buf[6], &tmp, sizeof(tmp));
    n = 10;
  }
  if (is_client) {
    buf[1] |= 1 << 7;  // Set masking flag
    mg_random(&buf[n], 4);
    n += 4;
  }
  return n;
}

static void _mg_ws_mask(struct mg_connection *c, size_t len) {
  if (c->is_client && c->send.buf != NULL) {
    size_t i;
    uint8_t *p = c->send.buf + c->send.len - len, *mask = p - 4;
    for (i = 0; i < len; i++) p[i] ^= mask[i & 3];
  }
}
/**************** End Of Mongoose mimic *************/

void TFlowWsVStreamer::wakeup(struct mg_connection* c, int enc_buf_idx)
{
    assert(enc_buf_idx < encoder->output_bufs.size());
    TFlowBuf &tflow_buf = encoder->output_bufs[enc_buf_idx];

    assert(tflow_buf.state == TFlowBuf::BUF_STATE_APP);
    size_t enc_buf_len = tflow_buf.v4l2_buf.m.planes->bytesused;
    uint8_t *enc_buf = (uint8_t *)tflow_buf.start;     

    uint32_t enc_packet_type = 
        (tflow_buf.v4l2_buf.flags & V4L2_BUF_FLAG_PFRAME)   ? packet_type_dlt :
        (tflow_buf.v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME) ? packet_type_key : 0;

    if (enc_packet_type == 0) return;     // Unknown Encoder frame.

#pragma pack(push, 1)
    struct enc_tlv_s {
        uint32_t magic;
        uint32_t seq;
        uint32_t packet_type;
        uint32_t packet_length;
    } enc_tlv = {
        .magic = 0x342E5452,
        .seq = (uint32_t)enc_seq++,
        .packet_type = enc_packet_type,
        .packet_length = (uint32_t)enc_buf_len
    };
#pragma pack(pop)

    // Broadcast message to all connected websocket clients.
    // Traverse over all connections
    for (struct mg_connection* wc = c->mgr->conns; wc != NULL; wc = wc->next) {
        // TODO: Start sending to newly opened connection from a KEY frame
        // Send only to marked connections
        if (wc->data[0] == 'W') {
            // The following code is mg_ws_send() unrolling to add transport 
            // related header (TLV).
#if 0
            mg_ws_send(wc, data->ptr, data->len, WEBSOCKET_OP_TEXT);
#endif
            uint8_t ws_header[14];
            size_t ws_header_len = _mkhdr(enc_buf_len + sizeof(enc_tlv), WEBSOCKET_OP_BINARY, wc->is_client, ws_header);
            mg_send(wc, ws_header, ws_header_len);
            mg_send(wc, &enc_tlv, sizeof(enc_tlv));
            mg_send(wc, enc_buf, enc_buf_len);
            _mg_ws_mask(wc, enc_buf_len + sizeof(enc_tlv));

            //        MG_VERBOSE(("WS out: %d [%.*s]", (int) len, (int) len, buf));
        }
    }
    
    // Pass the buffer back to the driver
    encoder->enqueueOutputBuffer(tflow_buf);

    return;
}

void TFlowWsVStreamer::_on_msg(struct mg_connection* c, int ev, void* ev_data)
{
    TFlowWsVStreamer *ws_streamer = (TFlowWsVStreamer *)c->fn_data;

    if (ev == MG_EV_OPEN && c->is_listening) {
        // Connection created
    }
    else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        struct user *u = authenticate(hm);  // AV: is it required?

        if (mg_http_match_uri(hm, "/websocket")) {
            mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to Websocket
            c->data[0] = 'W';            // Set some unique mark on a connection
        }
    }
    if (ev == MG_EV_ACCEPT) {
        size_t cert_len = 0;
        size_t key_len = 0;
        struct mg_tls_opts opts = {
            .cert = mg_file_read(&mg_fs_posix, "/home/root/cert/server.crt", &cert_len),
            .key  = mg_file_read(&mg_fs_posix, "/home/root/cert/server.key", &key_len),
            // .ca   = mg_file_read(&mg_fs_posix, "/home/root/cert/ca.crt", NULL)
        };
        opts.cert.len = cert_len;
        opts.key.len = key_len;
        mg_tls_init(c, &opts);
    }
    else if (ev == MG_EV_WS_OPEN) {
        c->data[0] = 'W';  // Mark this connection as an established WS client
    }
    else if (ev == MG_EV_WS_MSG) {
        // Got websocket frame. Received data is wm->data
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        mg_ws_send(c, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
        mg_iobuf_del(&c->recv, 0, c->recv.len);
    }
    else if (ev == MG_EV_WAKEUP) {
        struct mg_str* wake_up_data = (struct mg_str*)ev_data; 

        assert(wake_up_data->len == sizeof(uint32_t));
        uint32_t enc_buf_idx = *(uint32_t*)wake_up_data->ptr;

        if (enc_buf_idx == -1) {
            ws_streamer->terminate_thread = 1;
            return;
        }
        ws_streamer->wakeup(c, (int)enc_buf_idx);
    }

}

void* TFlowWsVStreamer::_thread(void* ctx)
{
    TFlowWsVStreamer* m = (TFlowWsVStreamer*)ctx;

    /* Mongoose main thread */
    mg_mgr_init(&m->mgr);           // Initialise event manager
    mg_log_set(MG_LL_INFO);         // Set log level
    mg_http_listen(&m->mgr, "http://0.0.0.0:8020", m->_on_msg, (void*)m);
    mg_wakeup_init(&m->mgr);        // Initialise wakeup socket pair
    while(m->terminate_thread == 0) {                      // Event loop
        mg_mgr_poll(&m->mgr, 1000);
    }
    mg_mgr_free(&m->mgr);

    return nullptr;
}

int TFlowWsVStreamer::onFrameEncoded(TFlowBuf &buf)
{
    // Awake WS sender (Mongoose) with the buffer's index
    uint32_t wake_up_data = buf.index;
    mg_wakeup(&mgr, 1, &wake_up_data, sizeof(wake_up_data));
#if CODE_BROWSE
    TFlowWsVStreamer::wakeup(struct mg_connection* c, int enc_buf_idx);
#endif
    return 0;
}

void TFlowWsVStreamer::fillBuffer(TFlowBuf& buf_enc, const TFlowBuf& buf_in)
{
    const uint8_t* data_buff;
    size_t data_buff_len;

    data_buff = (uint8_t*)buf_in.start;
    data_buff_len = buf_in.length;

    if (in_frame_fmt == V4L2_PIX_FMT_NV12) {
        // Streamer uses NV12 as an input format
        g_critical("WsVStreamer: bad frame fmt 0x%04X", in_frame_fmt);
        assert(0);
        // ^^^ NV12 needs to be converted to BGRA 
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
    else if (in_frame_fmt == V4L2_PIX_FMT_ABGR32) {
        // Streamer uses ABGR as an input format
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
    else if (in_frame_fmt == V4L2_PIX_FMT_GREY) {
        // Convert GREY to NV12. 
        // Y += 16, U = 128, V = 128;
        // Let's ignore proper conversion (Y+=16) for a while, just copy Y 
        // component. UV component are not affected by GREY frame thus they can
        // be prefilled upon buffer creation. See TFlowWsVStreamer::start()
        memcpy(buf_enc.start, data_buff, data_buff_len);
    }
    
}

int TFlowWsVStreamer::consumeBuffer(TFlowBuf& buf)
{
    // Application returns back our buffer for further processing
    // Upon encoding onFrameEncoded() callback will be triggered
    if (encoder) {
        encoder->enqueueInputBuffer(buf);
    }
#if CODE_BROWSE
    TFlowWsVStreamer::onFrameEncoded(buf_idx);
#endif
    return 0;
}

TFlowBuf* TFlowWsVStreamer::getFreeBuffer() 
{
    // Do not provide input buffer if there is no available output buffers
    if (!encoder || !encoder->isDriverOutputBuffers())
        return nullptr;

    // Application request buffer for feeding
    return encoder->getFreeInputBuffer();
}

int TFlowWsVStreamer::restart()
{
    TFlowEncCfg::cfg_v4l2_enc *v4l2_enc_cfg = 
        (TFlowEncCfg::cfg_v4l2_enc*)cfg->v4l2_enc.v.ref;

    stop();

    if (v4l2_enc_cfg->codec.v.num == TFlowEncUI::ENC_CODEC_H264) {
        packet_type_key = 0x344B0000;
        packet_type_dlt = 0x34500000;
    }
    else if (v4l2_enc_cfg->codec.v.num == TFlowEncUI::ENC_CODEC_H265) {
        packet_type_key = 0x354B0000;
        packet_type_dlt = 0x35500000;
    } else {
        g_critical("WsStreamer: Bad codec type - %d", v4l2_enc_cfg->codec.v.num);
        assert(0);
    }

    // ATT: Encoder can be reconfigured in runtime, i.e. recreated. Thus, 
    //      DO NOT assume pointer is always exists.
    encoder = new TFlowEnc(frame_width, frame_height, v4l2_enc_cfg,
        std::bind(&TFlowWsVStreamer::onFrameEncoded, this, std::placeholders::_1));

    if (in_frame_fmt == V4L2_PIX_FMT_GREY) {
        for (TFlowBuf& enc_buf : encoder->input_bufs) {
            // GREY to NV12 conversion.
            // The input buffer will be reused and GREY frame doesn't affect UV
            // components. Thus, prefill UV here and don't touch during a new 
            // frame copying.
            memset(enc_buf.start, 128, enc_buf.length);
        }
    }

    enc_seq = 0;

    return 0;
}

int TFlowWsVStreamer::start(int _w, int _h, uint32_t _fmt)
{
    frame_width = _w;
    frame_height = _h;
    in_frame_fmt = _fmt;

    return restart();
}

TFlowWsVStreamer::TFlowWsVStreamer(
    const TFlowWSStreamerCfg::cfg_ws_streamer *ws_streamer_cfg)
{
    frame_width = 0;
    frame_height = 0;
    in_frame_fmt = 0;

    encoder = nullptr;
    cfg = ws_streamer_cfg;

    last_idle_check = 0;

    // Create WebSocket (mongoose) thread
    //     Socket will be in idle state. I.e. no payload data transfer until 
    //     ws_streamer->start(). 
    // TODO: Send "NO SIGNAL" packet while in idle to render something on 
    //       canvas while source isn't connected
    //       
    pthread_attr_t attr;

    pthread_cond_init(&th_cond, nullptr);
    pthread_attr_init(&attr);

    int rc = pthread_create(&th, &attr, _thread, this);
    pthread_attr_destroy(&attr);

    terminate_thread = 0;
}

void TFlowWsVStreamer::stop()
{
    if (encoder) {
        delete encoder;
        encoder = nullptr;
    }
}

TFlowWsVStreamer::~TFlowWsVStreamer()
{
    stop();

    // Send termination notification to WS thread
    uint32_t wake_up_data = -1;
    mg_wakeup(&mgr, 1, &wake_up_data, sizeof(wake_up_data));

    int rc = pthread_join(th, nullptr);
}

void TFlowWsVStreamer::onConfigValidate(json11::Json::object& j_out_params,
    TFlowWSStreamerCfg::cfg_ws_streamer* rw_cfg)
{
    // Validate and fix parameters if needed. Set "changed" flag on modified 
    // parameters. For ex. in case of parameters mutal relation.
    // Called from TFlowCtrl only.
    // ...

    if (encoder && cfg->v4l2_enc.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {
        TFlowEncCfg::cfg_v4l2_enc *rw_enc_cfg = 
            (TFlowEncCfg::cfg_v4l2_enc*)rw_cfg->v4l2_enc.v.ref;

        encoder->onConfigValidate(j_out_params, rw_enc_cfg);
    }
}

int TFlowWsVStreamer::onConfig(json11::Json::object& j_out_params)
{
    // Apply WebSocket streamer configuration here

    if (encoder && cfg->v4l2_enc.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {

        int rc = encoder->onConfig(j_out_params);
        if (rc == -1) {
            // New configuration can be applied through full restart only
            restart();
        }
    }

    return 0;
}
#pragma once 

#include <netinet/ip.h>

#include "tflow-common.hpp"
#include "tflow-glib.hpp"

#include "tflow-buf.hpp"

#include "tflow-buf-cli.hpp"
#include "encoder-v4l2/tflow-v4l2enc.hpp"

#include "tflow-rtp-vstreamer-cfg.hpp" 

class TFlowRTPVStreamer {
public:
    TFlowRTPVStreamer(const TFlowRTPStreamerCfg::cfg_rtp_streamer *rtp_streamer_cfg);
    ~TFlowRTPVStreamer();

    void restart();
    void stop();
    void onIdle_no_ts();
    void onIdle(struct timespec now_ts);
    void onConfigValidate(json11::Json::object& j_out_params, TFlowRTPStreamerCfg::cfg_rtp_streamer *rw_cfg);
    int  onConfig();

    TFlowEnc *encoder;

private:
    
    int OpenUDP();
    void CloseUDP();

    int parseCfgAddrPort(struct sockaddr_in *sock_addr_out, const char *cfg_addr_port_in);

#pragma pack(push, 1)
    struct rtp_header {
        uint8_t version    : 2;
        uint8_t padding    : 1;
        uint8_t extension  : 1;             // Always present
        uint8_t csrc_count : 4;             // Always 1

        uint8_t mark         : 1;           // Set for the last packet of the access unit
        uint8_t payload_type : 7;           // Codec specific

        uint16_t seq;
        uint32_t timestamp;
        uint32_t ssrc;              // Synchronization Source Identifier
        uint32_t csrc;              // Contributing Source ID
    };
    
    struct rtp_extension {
        uint16_t id;            // ID
        uint16_t len;           // Indicates the length of the extension in 32-bit units, excluding the 32 bits of the extension header.
        uint8_t data[0];
    };

#pragma pack(pop)

    // HEVC/H264 encoder
    int onFrameEncoded(TFlowBuf &buf);

    const TFlowRTPStreamerCfg::cfg_rtp_streamer *cfg;

    struct frame_info in_frame_info;   // Streamer's input frame format may differs from the
                                       // Encoder's input format. Format conversion will be
                                       // performed on data buffer copy (see fillBuffer()).

    // Input stream client (TFlowBufCli) and its callbacks
    TFlowBufCli *buf_cli_in_stream;
    void onFrame(int src_id, const TFlowBufPck::pck_consume* msg_consume);
    void onSrcReady(int src_id, const TFlowBufPck::pck_src_info* src_info);
    void onSrcGone(int src_id);

    clock_t last_idle_check;

    // TODO: Rework to UDP class
    int sck_fd;
    Flag sck_state_flag;
    struct timespec last_send_ts;
    struct timespec last_conn_check_ts;

    uint8_t payload_type;
    uint16_t enc_seq;           // TODO: Is it part of encoder?

    int addr_nok;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    struct iovec enc_iov[4];    // 
    struct msghdr enc_mh;
};


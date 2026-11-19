#pragma once 

#include <netinet/ip.h>

#include "../tflow-common.hpp"
#include "../tflow-glib.hpp"

#include "../tflow-buf.hpp"

#include "../encoder-v4l2/tflow-v4l2enc.hpp"

#include "tflow-rtp-vstreamer-cfg.hpp" 

class TFlowRTPVStreamer {
public:
    TFlowRTPVStreamer(int _w, int _h, uint32_t _in_fmt,
        const TFlowRTPStreamerCfg::cfg_rtp_streamer *rtp_streamer_cfg);
    ~TFlowRTPVStreamer();

    TFlowBuf* getFreeBuffer();
    void fillBuffer(TFlowBuf& buf_enc, const TFlowBuf& buf_in);     // Should be part of TFlowEnc()
    int consumeBuffer(TFlowBuf& buf);
    void onIdle_no_ts();
    void onIdle(struct timespec now_ts);
    void onFrame(const TFlowBuf& buf_in);

    TFlowEnc *encoder;
private:
    int frame_width, frame_height;
    uint32_t in_frame_fmt;  // Streamer's input frame format may differs from the
                            // Encoder's input format. Format conversion will be
                            // performed on data buffer copy (see fillBuffer()).
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

    clock_t last_idle_check;

    int sck_fd;
    Flag sck_state_flag;
    struct timespec last_send_ts;
    struct timespec last_conn_check_ts;

    uint8_t payload_type;
    uint16_t pck_seq;

    int addr_nok;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;

    struct iovec enc_iov[4];    // 
    struct msghdr enc_mh;
};


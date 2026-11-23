#pragma once 

#include <thread>
#include "mongoose.h"

#include "tflow-common.hpp"
#include "tflow-glib.hpp"

#include "tflow-buf.hpp"

#include "tflow-buf-cli.hpp"

#include "encoder-v4l2/tflow-v4l2enc.hpp"

#include "tflow-ws-vstreamer-cfg.hpp" 

class TFlowWsVStreamer {
public:
    TFlowWsVStreamer(const TFlowWSStreamerCfg::cfg_ws_streamer *ws_streamed_cfg);

    ~TFlowWsVStreamer();

    void onIdle(const struct timespec &now_ts);
    void onConfigValidate(json11::Json::object& j_out_params, TFlowWSStreamerCfg::cfg_ws_streamer *rw_cfg);
    int onConfig();

    TFlowEnc *encoder;

    void stop();

private:
    
    const TFlowWSStreamerCfg::cfg_ws_streamer *cfg;

    int restart();  // The same as start,  but without frame configuration change. 
                    // Is used on encoder configuration change.

    TFlowBuf* getFreeBuffer();                                      // TODO: rework required. Must be part of TFlowEnc
    void fillBuffer(TFlowBuf& buf_enc, const TFlowBuf& buf_in);     // TODO: rework required. Must be part of TFlowEnc
    int consumeBuffer(TFlowBuf& buf);

    uint32_t packet_type_key;
    uint32_t packet_type_dlt;

    int enc_seq;

    struct frame_info in_frame_info;   // Streamer's input frame format may differs from the
                                       // Encoder's input format. Format conversion will be
                                       // performed on data buffer copy (see fillBuffer()).

    // Input stream client (TFlowBufCli) and its callbacks
    TFlowBufCli *buf_cli_in_stream;
    void onFrame(int src_id, const TFlowBufPck::pck_consume* msg_consume);
    void onSrcReady(int src_id, const TFlowBufPck::pck_src_info* src_info);
    void onSrcGone(int src_id);


    // HEVC/H264 encoder
    int onFrameEncoded(TFlowBuf &buf);

    pthread_t           th;
    pthread_cond_t      th_cond;
    pthread_mutex_t     th_mutex;

    int terminate_thread;

    struct mg_mgr mgr;

    clock_t last_idle_check;
    clock_t last_send_ts;

    static void* _thread(void* ctx);
    static void _on_msg(struct mg_connection* c, int ev, void* ev_data);

    void wakeup(struct mg_connection* c, int enc_buf_idx);

};


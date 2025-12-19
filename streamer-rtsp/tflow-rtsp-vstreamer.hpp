#pragma once 

#include <netinet/ip.h>

#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

#include "../tflow-common.hpp"
#include "../tflow-glib.hpp"

#include "../tflow-buf.hpp"

#include "../encoder-v4l2/tflow-v4l2enc.hpp"

#include "tflow-rtsp-vstreamer-cfg.hpp" 

class TFlowRTSPVStreamer {
public:
    TFlowRTSPVStreamer(int _w, int _h,
        const TFlowRTSPStreamerCfg::cfg_rtsp_streamer *rtsp_streamer_cfg);
    ~TFlowRTSPVStreamer();

    void onFrame(const TFlowBuf& buf_in);
    void onConfigValidate(json11::Json::object& j_out_params, TFlowRTSPStreamerCfg::cfg_rtsp_streamer* rw_cfg);
    int onConfig(json11::Json::object& j_out_params);

    void onIdle(struct timespec now_ts);

    int pck_seq;
    TFlowEnc *encoder;
private:
    
    int StartRTSP();
    void CloseRTSP();

    const TFlowRTSPStreamerCfg::cfg_rtsp_streamer *cfg;

    int width;
    int height;
    long frame_size;
    GstBuffer* in_buffer;
    uint8_t *in_data_copy;

    clock_t last_idle_check;

    struct timespec last_send_ts;
    struct timespec last_conn_check_ts;

    void need_data();
    void media_configure(GstRTSPMedia* media);

    static void s_need_data(GstElement* appsrc, guint unused, void* ctx);
    static void s_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media,
        gpointer user_data);

    GstClockTime buff_timestamp;
    GstClockTime buff_duration;
    int buff_seq;


    /* GST RTSP server runing in its own thread. Take care about members shared 
       between default and GST thread */

    GstRTSPServer* server;
    guint server_tag;

    GstRTSPMediaFactory* factory;
    guint factory_tag;
    gulong sigid_media_configure;

    GstElement  *appsrc;
    gulong sigid_need_data;

};


#include "../tflow-build-cfg.hpp"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>

#include <netinet/ip.h>

#include <arpa/inet.h>
#include <poll.h>

#include <glib-unix.h>

#include <json11.hpp>

#include "../encoder-v4l2/tflow-v4l2enc.hpp"
#include "tflow-rtsp-vstreamer.hpp"

TFlowRTSPStreamerCfg tflow_rtsp_streamer_cfg;

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
static uint64_t diff_timespec_nsec(
    const struct timespec* time1,
    const struct timespec* time0)
{
    struct timespec d_tp = diff_timespec(time1, time0);
    return (uint64_t)d_tp.tv_sec * 1000*1000*1000 + d_tp.tv_nsec;
}

static GstRTSPFilterResult 
client_filter(GstRTSPServer* server, GstRTSPClient* client, gpointer user_data)
{
    /* Simple filter that shuts down all clients. */
    return GST_RTSP_FILTER_REMOVE;
}

void TFlowRTSPVStreamer::CloseRTSP()
{
    // From GLIB: You must use g_source_destroy() for sources added to a non-default main context.
    if (server) {

        /* Remove the mount point to prevent new clients connecting */
        GstRTSPMountPoints* mounts;
        mounts = gst_rtsp_server_get_mount_points(server);
        gst_rtsp_mount_points_remove_factory(mounts, "/tflow-vstream");
        g_object_unref(mounts);

        /* Filter existing clients and remove them */
        gst_rtsp_server_client_filter(server, client_filter, NULL);

        g_source_remove(server_tag);
        gst_object_unref(server);
        server = nullptr;
    }

    if (factory) {
        g_signal_handler_disconnect(factory, sigid_media_configure);
        gst_object_unref(factory);
        factory = nullptr;
    }

    if (appsrc) {
        g_signal_handler_disconnect(appsrc, sigid_need_data);
        gst_object_unref(appsrc);
        appsrc = nullptr;
    }

    if (in_buffer) {
        gst_buffer_unref(in_buffer);
        in_buffer = nullptr;
    }

    if (in_data_copy) {
        g_free(in_data_copy);
        in_data_copy = nullptr;
    }

}

void TFlowRTSPVStreamer::need_data()
{
    guint size;
    GstFlowReturn ret;

    assert(in_buffer == nullptr);

    in_buffer = gst_buffer_new_memdup(in_data_copy, frame_size);

    GST_BUFFER_PTS(in_buffer) = buff_timestamp;
    GST_BUFFER_DURATION(in_buffer) = buff_duration;
    buff_timestamp += GST_BUFFER_DURATION(in_buffer);
    buff_seq ++;

    g_signal_emit_by_name(appsrc, "push-buffer", in_buffer, &ret);
    gst_buffer_unref(in_buffer);
    in_buffer = nullptr;

    PRESC(0x3F) {
        g_debug("TFlowRTSPVStreamer: need data OK. seq=%d TS=%7.3fsec",
            buff_seq, (double)buff_timestamp / GST_SECOND);
    }

    return;
}

/* called when we need to give data to appsrc */
void TFlowRTSPVStreamer::s_need_data(GstElement* _appsrc, guint unused, void* ctx)
{
    TFlowRTSPVStreamer *tflow_rtsp = (TFlowRTSPVStreamer*)ctx;

    // input argument _appsrc must be the same with our preserved appsrc
    assert(tflow_rtsp->appsrc == _appsrc);
    tflow_rtsp->need_data();
}

void TFlowRTSPVStreamer::s_media_configure(GstRTSPMediaFactory* _factory,
    GstRTSPMedia* media, gpointer user_data)
{
    TFlowRTSPVStreamer* tflow_rtsp = (TFlowRTSPVStreamer*)user_data;
    assert(tflow_rtsp->factory == _factory);

    tflow_rtsp->media_configure(media);
}

void TFlowRTSPVStreamer::media_configure(GstRTSPMedia* media)
{
    GstElement *element;

    buff_seq = 0;
    buff_timestamp = 0;
    buff_duration = gst_util_uint64_scale_int(1, GST_SECOND, cfg->fps.v.num);

    /* get the element used for providing the streams of the media */
    element = gst_rtsp_media_get_element(media);
    appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "vstreamer");
    gst_object_unref(element);

    /* this instructs appsrc that we will be dealing with timed buffer */
    gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");

    /* configure the caps of the video */
    g_object_set(G_OBJECT(appsrc), "caps",
        gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGRA",    // BGRA is a native format for TFlowProcess. Other (NV12) not efficient for OpenCV rendering
            "width", G_TYPE_INT, width,
            "height", G_TYPE_INT, height,
            "framerate", GST_TYPE_FRACTION, 0, 1, NULL), NULL);

    /* install the callback that will be called when a buffer is needed */
    sigid_need_data = g_signal_connect(appsrc, "need-data", (GCallback)s_need_data, this);
    
    g_debug("TFlowRTSPVStreamer: media_configured");

    gst_rtsp_media_set_latency(media, 0);
}

int TFlowRTSPVStreamer::StartRTSP()
{
    GstRTSPMountPoints* mounts;

    gst_init(NULL, NULL);

    in_data_copy = (uint8_t*)g_malloc(frame_size);

    server = gst_rtsp_server_new();
    mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();
    gst_object_ref(factory);    // Will be cleaned on Stop()

    if (cfg->enc_type.v.num == TFlowRTSPStreamerUI::ENC_TYPE_H265) {
        gst_rtsp_media_factory_set_launch(factory,
            "( appsrc  name=vstreamer  ! vpuenc_hevc ! rtph265pay name=pay0 pt=96 )");    // GST OK. 16% CPU load @ 50FPS
    }
    else {
        gst_rtsp_media_factory_set_launch(factory,
            "( appsrc  name=vstreamer  ! vpuenc_h264 ! rtph264pay name=pay0 pt=96 )");   // GST OK. 22 % CPU load @ 50FPS
    }
    // "( appsrc name=vstreamer   ! videoconvert ! video/x-raw,format=I420 ! x264enc  ! rtph264pay name=pay0 pt=96 )");   // VLC/GST OK        CPU load 6 threads by 30% each!!!

    sigid_media_configure = g_signal_connect(factory, "media-configure",
        (GCallback)s_media_configure, this);

    /* attach the test factory to the /tflow-vstream url */
    /* GST: Ownership is taken of the reference on factory so that factory 
     *      should not be used after calling this function. 
     */
    gst_rtsp_mount_points_add_factory(mounts, "/tflow-vstream", factory);

    /* don't need the ref to the mounts anymore */
    g_object_unref(mounts);

    /* attach the server to the default context */
    server_tag = gst_rtsp_server_attach(server, NULL);   
    /* Att: ^^^ If context is not default, then gst_rtsp_server_create_source()
            must be used and then attached to the context */

    g_info("TFlowRTSPStreamer: Server started at "\
           "rtsp://127.0.0.1:8554/tflow-vstream");

    /* WIN10 RTSP Client: 
        gst-launch-1.0.exe rtspsrc location=rtsp://192.168.2.2:8554/tflow-vstream latency=0 ! queue ! rtph265depay ! h265parse ! d3d11h265dec ! glimagesink sync=false
        gst-launch-1.0.exe rtspsrc location=rtsp://192.168.2.2:8554/tflow-vstream latency=0 ! queue ! rtph264depay ! h264parse ! d3d11h264dec ! glimagesink sync=false
    */
    return 0;
}

void TFlowRTSPVStreamer::onIdle(struct timespec now_ts)
{
    // ???
}

void TFlowRTSPVStreamer::onFrame(const TFlowBuf& buf_in)
{
    // TODO: Greate GST memory buffer and map it to TFlowBuffer to avoid memory copy.
    //       gst_buffer_new_wrapped_full(). Rework input buffer for shared pointer?
    //  
    // Note: as soon as rtsp server running in same mainloop it is safe
    //       to change the input buffer without MT protection
    memcpy(in_data_copy, buf_in.start, buf_in.length);
}

TFlowRTSPVStreamer::TFlowRTSPVStreamer(int _w, int _h,
    const TFlowRTSPStreamerCfg::cfg_rtsp_streamer *rtsp_streamer_cfg)
{
    int rc;
                   
    cfg = rtsp_streamer_cfg;

    // TODO: share encoder between WS and RTSP streamers
    //       Option 1) Use encoder in GST pipeline, connect to encoder output
    //       and pass it to WS streamer.
    //       Option 2) Use current v4l2 encoder and construct GST pipeline with
    //                 rtp264pay element
    //       Option 2 looks more reasonable as V4L2 has already implemented and
    //       already can be controlled from WEB. takes ~6% CPU
    // 
    //       Option 1 preferable as it can include input format transformation
    //       using videoconvert element, but GST HW encoder takes ~26% of CPU
    //       Also GST v4l2 seems not stable in our current dirty release (6.6.52)

    last_idle_check = 0;

    width = _w;
    height = _h;
    uint32_t format = V4L2_PIX_FMT_ABGR32;  
    
    // TODO: pass format as a constructor param received on SRC connection

    frame_size = (width * height *
           ((format == V4L2_PIX_FMT_GREY  ) ? 8 :
            (format == V4L2_PIX_FMT_BGR24 ) ? 24 :
            (format == V4L2_PIX_FMT_ABGR32) ? 32 :
            (format == V4L2_PIX_FMT_NV12  ) ? 12 : 0)) / 8;

    server = nullptr;
    factory = nullptr;
    appsrc = nullptr;
    in_data_copy = nullptr;
    in_buffer = nullptr;

    StartRTSP();
}

TFlowRTSPVStreamer::~TFlowRTSPVStreamer()
{
    CloseRTSP();
}

void TFlowRTSPVStreamer::onConfigValidate(json11::Json::object& j_out_params,
    TFlowRTSPStreamerCfg::cfg_rtsp_streamer* rw_cfg)
{
    // Validate and fix parameters if needed. Set "changed" flag on modified 
    // parameters. For ex. in case of parameters mutal relation.
    // Called from TFlowCtrl only.
    // ...

    if (cfg->fps.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {
        if (cfg->fps.v.num < 1) rw_cfg->fps.v.num = 1;
        if (cfg->fps.v.num > 60) rw_cfg->fps.v.num = 60;
    }

}

int TFlowRTSPVStreamer::onConfig(json11::Json::object& j_out_params)
{
    if (cfg->fps.flags & TFlowCtrl::FIELD_FLAG::CHANGED ||
        cfg->enc_type.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {

        CloseRTSP();
        StartRTSP();
    }

    return 0;
}
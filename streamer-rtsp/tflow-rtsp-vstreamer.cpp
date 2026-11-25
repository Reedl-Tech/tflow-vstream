#include "tflow-build-cfg.hpp"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>

#include <netinet/ip.h>

#include <arpa/inet.h>
#include <poll.h>

#include <glib-unix.h>

#include <json11.hpp>

#include "encoder-v4l2/tflow-v4l2enc.hpp"
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

void TFlowRTSPVStreamer::stop()
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

    // ??? Time stamp not needed as we using "do-timestamp=true" for appsrc
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

    const char *gst_fmt_string;
    switch (in_frame_info.format) {
    case V4L2_PIX_FMT_ABGR32:
        gst_fmt_string = "BGRA";
        break;
    case V4L2_PIX_FMT_NV12:
        gst_fmt_string = "NV12";
        break;
    case V4L2_PIX_FMT_GREY:
        // See RTP Streamer for GREY handling
    default:
        assert(0);
    }

    /* configure the caps of the video */
    g_object_set(G_OBJECT(appsrc), "caps",                  
        gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "BGRA",    // BGRA is a native format for TFlowProcess. Other (NV12) not efficient for OpenCV rendering
            "width", G_TYPE_INT, in_frame_info.width,
            "height", G_TYPE_INT, in_frame_info.height,
            "framerate", GST_TYPE_FRACTION, 0, 1, NULL), NULL);

    /* install the callback that will be called when a buffer is needed */
    sigid_need_data = g_signal_connect(appsrc, "need-data", (GCallback)s_need_data, this);
    
    g_debug("TFlowRTSPVStreamer: media_configured");

    gst_rtsp_media_set_latency(media, 0);
}

int TFlowRTSPVStreamer::restart()
{
    GstRTSPMountPoints* mounts;

    stop();

    in_data_copy = (uint8_t*)g_malloc(frame_size);

    server = gst_rtsp_server_new();
    mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();
    gst_object_ref(factory);    // Will be cleaned on Stop()

    char port_str[16] = { 0 };
    if (cfg->port.v.num) {
        snprintf(port_str, sizeof(port_str) - 1, "%d", cfg->port.v.num);
        g_object_set(server, "service", port_str, NULL);
    }
    else {
        snprintf(port_str, sizeof(port_str) - 1, "%d", 8554);
        // Use default port - 8554
    }

    char gst_crop_str[256] = { 0 };
    if (cfg->crop_en.v.num) {
        snprintf(gst_crop_str, sizeof(gst_crop_str) - 1,
            "! videocrop top=%d left=%d right=%d bottom=%d ",
            (*cfg->crop_v.v.vnum)[0],  // crop_top
            (*cfg->crop_h.v.vnum)[0],  // crop_left,
            (*cfg->crop_h.v.vnum)[1],  // crop_right
            (*cfg->crop_v.v.vnum)[1]); // crop_bottom
    }

    char gst_pipeline_str[512] = { 0 };
    if (cfg->enc_type.v.num == TFlowRTSPStreamerUI::ENC_TYPE_H265) {
        snprintf(gst_pipeline_str, sizeof(gst_pipeline_str) - 1,
            "( appsrc name=vstreamer  is-live=true %s ! vpuenc_hevc ! rtph265pay name=pay0 pt=96 )",
            gst_crop_str);
    }
    else {
        snprintf(gst_pipeline_str, sizeof(gst_pipeline_str) - 1,
            "( appsrc name=vstreamer is-live=true %s ! vpuenc_h264 ! rtph264pay name=pay0 pt=96 )",
            gst_crop_str);
    }
    gst_rtsp_media_factory_set_launch(factory, gst_pipeline_str);

        //gst_rtsp_media_factory_set_launch(factory,
//      "(appsrc name=vstreamer  ! vpuenc_h264 ! rtph264pay name=pay0 pt=96 )");   // GST OK. 22 % CPU load @ 50FPS
//      "( appsrc  name=vstreamer  ! videocrop top=10 left=10 right=406 bottom=302 ! vpuenc_hevc ! rtph265pay name=pay0 pt=96 )");    // GST OK. 16% CPU load @ 50FPS
//      "( appsrc  name=vstreamer  ! vpuenc_hevc ! rtph265pay name=pay0 pt=96 )");    // GST OK. 16% CPU load @ 50FPS
//      "( appsrc name=vstreamer   ! videoconvert ! video/x-raw,format=I420 ! x264enc  ! rtph264pay name=pay0 pt=96 )");   // VLC/GST OK        CPU load 6 threads by 30% each!!!

    sigid_media_configure = g_signal_connect(factory, "media-configure",
        (GCallback)s_media_configure, this);

    /* attach the test factory to the /tflow-vstream url */
    /* GST: Ownership is taken of the reference on factory so that factory 
     *      should not be used after calling this function. 
     */
    char mount_name[256] = { 0 }; 
    if (cfg->mount_name.v.str) {
        snprintf(mount_name, sizeof(mount_name) - 1, "/%s", cfg->mount_name.v.str);
    }
    else {
        snprintf(mount_name, sizeof(mount_name) - 1, "/tflow-vstream");
    }
    gst_rtsp_mount_points_add_factory(mounts, mount_name, factory);

    /* don't need the ref to the mounts anymore */
    g_object_unref(mounts);

    /* attach the server to the default context */
    server_tag = gst_rtsp_server_attach(server, NULL);   
    /* Att: ^^^ If context is not default, then gst_rtsp_server_create_source()
            must be used and then attached to the context */

    g_info("TFlowRTSPStreamer: Server started at "\
           "rtsp://127.0.0.1:%s/%s", port_str, mount_name);

    /* WIN10 RTSP Client: 
        gst-launch-1.0.exe rtspsrc location=rtsp://192.168.2.2:8554/tflow-vstream latency=0 ! queue ! rtph265depay ! h265parse ! d3d11h265dec ! glimagesink sync=false
        gst-launch-1.0.exe rtspsrc location=rtsp://192.168.2.2:8554/tflow-vstream latency=0 ! queue ! rtph264depay ! h264parse ! d3d11h264dec ! glimagesink sync=false
    */
    return 0;
}

void TFlowRTSPVStreamer::onIdle(struct timespec now_ts)
{
    if (buf_cli_in_stream) {
        buf_cli_in_stream->onIdle(now_ts);
    }
}

void TFlowRTSPVStreamer::onFrame(int src_id, const TFlowBuf &buf_in)
{
    // TODO: Check received src_id vs configured source

    // Note: as soon as rtsp server running in same mainloop it is safe
    //       to change the input buffer without MT protection
        memcpy(in_data_copy, buf_in.start, buf_in.length);
}

TFlowRTSPVStreamer::TFlowRTSPVStreamer(const TFlowRTSPStreamerCfg::cfg_rtsp_streamer *rtsp_streamer_cfg)
{
    int rc;
                   
    gst_init(NULL, NULL);

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

    server = nullptr;
    factory = nullptr;
    appsrc = nullptr;
    in_data_copy = nullptr;
    in_buffer = nullptr;
}

TFlowRTSPVStreamer::~TFlowRTSPVStreamer()
{
    stop();
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

int TFlowRTSPVStreamer::onConfig()
{
    if (cfg->src.flags & TFlowCtrl::FIELD_FLAG::CHANGED) {

        stop();

        // Close current input stream in any case
        if (buf_cli_in_stream) {
            delete buf_cli_in_stream;
            buf_cli_in_stream = nullptr;
        }

        TFlowRTSPStreamerUI::VIDEO_SRC src = (TFlowRTSPStreamerUI::VIDEO_SRC)cfg->src.v.num;

        const char* srv_name =
            (src == TFlowRTSPStreamerUI::VIDEO_SRC_CAM0) ? "com.reedl.tflow.capture0.buf-server" :
            (src == TFlowRTSPStreamerUI::VIDEO_SRC_CAM1) ? "com.reedl.tflow.capture1.buf-server" :
            (src == TFlowRTSPStreamerUI::VIDEO_SRC_PROC) ? "com.reedl.tflow.process.buf-server"  : nullptr;

        if (srv_name) {
            buf_cli_in_stream = new TFlowBufCli(
                "TFlowStreamer_RTSP", srv_name, 0,
                std::bind(&TFlowRTSPVStreamer::onFrame,    this, std::placeholders::_1, std::placeholders::_2),   // TFlowBufCli::app_onFrame()
                std::bind(&TFlowRTSPVStreamer::onSrcReady, this, std::placeholders::_1, std::placeholders::_2),   // TFlowBufCli::app_onSrcReady()
                std::bind(&TFlowRTSPVStreamer::onSrcGone,  this, std::placeholders::_1),                          // TFlowBufCli::app_onSrcGone()
                nullptr,
                nullptr);
        }

        // In case of input source changed, other configuration doesn't matter
        // as it was applyed on SrcReady
        return 0;

    } // End of SRC changed

    if (cfg->fps.flags      & TFlowCtrl::FIELD_FLAG::CHANGED ||
        cfg->enc_type.flags & TFlowCtrl::FIELD_FLAG::CHANGED ||
        cfg->crop_en.flags  & TFlowCtrl::FIELD_FLAG::CHANGED) {

        restart();
    }
     
    return 0;
}

void TFlowRTSPVStreamer::onSrcReady(int src_id, const struct frame_info& _in_frame_info)
{
    in_frame_info = _in_frame_info;

    frame_size = (in_frame_info.width * in_frame_info.height *
           ((in_frame_info.format == V4L2_PIX_FMT_GREY)   ?  8 :
            (in_frame_info.format == V4L2_PIX_FMT_BGR24)  ? 24 :
            (in_frame_info.format == V4L2_PIX_FMT_ABGR32) ? 32 :
            (in_frame_info.format == V4L2_PIX_FMT_NV12)   ? 12 : 0)) / 8;

    restart();
}

void TFlowRTSPVStreamer::onSrcGone(int src_id)
{
    stop();
}

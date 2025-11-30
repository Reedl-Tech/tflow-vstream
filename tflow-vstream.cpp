#include "tflow-build-cfg.hpp"
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <limits>

#include <functional>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/eventfd.h>

#include <glib-unix.h>
#include <gio/gio.h>
#include <json11.hpp>

#include <linux/videodev2.h> //V4L2 stuff

#include "tflow-vstream.hpp"

#define IDLE_INTERVAL_MSEC 500

using namespace json11;

InFrameJP::InFrameJP(uint32_t width, uint32_t height, uint32_t format, uint8_t* data) 
{
    this->width = width;
    this->height = height;
    this->format = format;
    this->data = data;

    jp_rows.reserve(height);
    JSAMPROW p = (JSAMPROW)data;
    for (int i = 0; i < height; i++) {
        jp_rows.emplace_back(p);
        p += width;
    }
}

TFlowVStream::TFlowVStream(GMainContext *_context, const std::string cfg_fname) :
    buf_cli_recording(nullptr),
    buf_cli_streaming(nullptr),
    ws_streamer(nullptr),
    udp_streamer(nullptr),
    context(_context),
    ctrl(*this, cfg_fname)  // Att!: Must be constructed after submodules' pointers initialization
{

   
    main_loop = g_main_loop_new(context, false);
    
    /* JPEG LIB related init */
    jp_buf = nullptr;
    jp_buf_sz = 0;

    CLEAR(jp_cinfo);
    jp_cinfo.err = jpeg_std_error(&jp_err);
    jpeg_create_compress(&jp_cinfo);

    mjpeg_file_size = 0;

    recording_total_size = -1;          // Unknown
    recording_total_size_recalc = 1;    // Recalculate total file size on start
    forced_split = 0;
    split_last_ts = 0;
    vdump_timeout_src = NULL;
}

TFlowVStream::~TFlowVStream()
{
    if (buf_cli_recording) delete buf_cli_recording;
    if (buf_cli_streaming) delete buf_cli_streaming;

    // Finalize the vdump file if active
    fileClose();

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    context = NULL;

    jpEncClose();

}

static gboolean tflow_vstream_on_vdump_timer(gpointer data)
{
    TFlowVStream* app = (TFlowVStream*)data;

    app->forced_split = 1;

    g_source_destroy(app->vdump_timeout_src);
    g_source_unref(app->vdump_timeout_src);
    app->vdump_timeout_src = NULL;

    return G_SOURCE_REMOVE;
}

static gboolean tflow_vstream_on_idle(gpointer data)
{
    TFlowVStream* app = (TFlowVStream*)data;

    app->OnIdle();

    return G_SOURCE_CONTINUE;
}

static void tflow_vstream_idle_once(gpointer data)
{
    tflow_vstream_on_idle(data);
}

void TFlowVStream::recordingCheckTotalSize()
{
    // For all files in current vdump path
    GError* error = NULL;
    const gchar *dir_entry = NULL; 
    gchar *vdump_dirname = g_path_get_dirname(ctrl.cmd_flds_cfg_recording.path.v.c_str);
    gchar *vdump_basename = g_path_get_basename(ctrl.cmd_flds_cfg_recording.path.v.c_str);
    GDir* dir = g_dir_open(vdump_dirname, 0, &error);
    if (error) {
        // ??? Disable check somehow ???
        // ...
        g_free(vdump_dirname);
        return;
    }

    ssize_t size_total = 0;
    time_t lru_time = std::numeric_limits<time_t>::max();
    char *lru_filename = nullptr;

    while ((dir_entry = g_dir_read_name(dir))) {
        struct stat st;
        gchar *entry_full_name;

        if (strncmp(dir_entry, vdump_basename, strlen(vdump_basename))) {
            continue;
        }

        entry_full_name = g_build_filename(vdump_dirname, dir_entry,  (char *)NULL);
        
        if (-1 == stat(entry_full_name, &st)) {
            g_free(entry_full_name);
            continue;
        }

        if (!S_ISREG(st.st_mode)) continue;

        size_total += st.st_size;

        // Get last modified file
        if (st.st_mtime < lru_time) {
            lru_time = st.st_mtime;
            if (lru_filename) free(lru_filename);
            lru_filename = strdup(entry_full_name);
        }
        g_free(entry_full_name);
    }

    if (size_total / 1024 / 1024 > ctrl.cmd_flds_cfg_recording.max_tot_size_mb.v.num) {
        GFile* lru_file = g_file_parse_name(lru_filename);
        g_file_delete_async(lru_file, G_PRIORITY_LOW, NULL, NULL, NULL);
        g_object_unref(lru_file);
    }
    
    if (lru_filename) free(lru_filename);

    g_dir_close(dir);
    if (vdump_dirname) g_free(vdump_dirname);
    if (vdump_basename) g_free(vdump_basename);

}

void TFlowVStream::OnIdle()
{
    struct timespec now_ts;

    // Get dump files' total size.
    // If it's more than the limit from the config, then remove the latest file.
    if (recording_total_size_recalc) {
        recording_total_size_recalc = 0;
        recordingCheckTotalSize();
    }

    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    if (buf_cli_recording) {
        buf_cli_recording->onIdle(now_ts);
    }

    if (buf_cli_streaming) {
        buf_cli_streaming->onIdle(now_ts);
    }

    ctrl.ctrl_srv.onIdle(now_ts);

}

void TFlowVStream::AttachIdle()
{
    GSource* src_idle = g_timeout_source_new(IDLE_INTERVAL_MSEC);
    g_source_set_callback(src_idle, (GSourceFunc)tflow_vstream_on_idle, this, nullptr);
    g_source_attach(src_idle, context);
    g_source_unref(src_idle);

    return;
}

int TFlowVStream::isDumpRequired(const uint8_t* aux_data, size_t aux_data_len)
{
    /* 
     * By default dump is enabled and sometimes, if configured, can be disabled
     * for some case. For instance - don't dump video while disarmed.
     */
    
    // !!!!!!!!!! remove me when mounted !!!!!!!!!!
    // imu.mode = TFlowImu::IMU_MODE::DISARMED;

    TFlowImu::IMU_MODE mode_prev = imu.mode;

    // No imu data - just dump 
    if (0 == aux_data_len) return 1;

    // In case of error of unknown IMU format let's Dump anyway,
    // but don't check IMU mode (copter/disarmed)
    imu.getIMU(aux_data, aux_data_len);
    if (!imu.is_valid) return 1;

    // !!!!!!!!!! remove me when mounted !!!!!!!!!!
    // imu.mode = TFlowImu::IMU_MODE::DISARMED;

    // Normlly, Disarmed, Copter or Plane mode should be debugged separately.
    // Thus, lets split dump into several files for different modes.
    if (mode_prev != imu.mode) {
        forced_split = 1;
        return 1;
    }

    // By default don't dump if disarmed
    if (imu.mode == TFlowImu::IMU_MODE::DISARMED && 
        ctrl.cmd_flds_cfg_recording.dump_disarmed.v.num == 0) { 
        return 0;
    }

    return 1;
}

void TFlowVStream::jpEncClose()
{
    in_frames_jp.clear();

    if (jp_buf) {
        free(jp_buf);
        jp_buf = nullptr;
    }

    jpeg_destroy_compress(&jp_cinfo);
}

void TFlowVStream::fileCreateDir(const gchar *file_path)
{
    struct stat st;
    gchar* dirname = g_path_get_dirname(file_path);

    if (stat(dirname, &st) == -1) {
        int rc;
        // Directory not found. Let's create
        GFile* dir_path = g_file_parse_name(dirname);
        rc = g_file_make_directory_with_parents(dir_path, NULL, NULL);
        if (rc == 0) {
            g_info("New vdump directory created - %s", dirname);
        }
        // It is OK then directory can't be created. For instance uSD is 
        // configured as destination, but not installed.
    } 
    // Don't care if we can't create diectory. Let open() handle the error.
    return;
}

void TFlowVStream::fileClose(const struct tm* tm_info)
{
    if (tm_info == nullptr) {
        // Get current time if not provided by callee
        time_t time_now = time(NULL);
        tm_info = localtime(&time_now);
    }

    // Stop the  timer if active
    if (vdump_timeout_src) {
        g_source_destroy(vdump_timeout_src);
        g_source_unref(vdump_timeout_src);
        vdump_timeout_src = NULL;
    }

    if (!mjpeg_file) return;

    fclose(mjpeg_file);
    mjpeg_file = nullptr;

    // Add stop suffix if configured
    if (ctrl.cmd_flds_cfg_recording.suffix_ts_stop.v.c_str) {
        constexpr int ts_suffix_max = 64;
        std::unique_ptr<char> ts_str(new char[ts_suffix_max]);
        strftime(ts_str.get(), ts_suffix_max,
            ctrl.cmd_flds_cfg_recording.suffix_ts_stop.v.c_str, tm_info);
        if (*ts_str) {
            // Some suffix composed, let's added it to the file
            // use g_rename instead ???
            rename(mjpeg_filename.c_str(), 
                (mjpeg_filename + std::string(ts_str.get())).c_str());
        }
    }

}
void TFlowVStream::fileSplit()
{
    time_t time_now;
    struct tm* tm_info;

    time_now = time(NULL);
    tm_info = localtime(&time_now);

    fileClose(tm_info);
    
    if (ctrl.cmd_flds_cfg_recording.path.v.c_str == nullptr) return;

    // File not opened yet or just closed
    // Check timestamp of last split attempt and try again if allowed.
    if (split_last_ts) {
        if (difftime(time_now, split_last_ts) < 3) {
            // Don't allow split too often. 3sec interval should be good enough.
            return;
        }
    }

    // Update timestamp of the last split attempt, even if it will
    // be unsuccessful.
    split_last_ts = time_now;

    // Compose new name
    mjpeg_filename = std::string(ctrl.cmd_flds_cfg_recording.path.v.c_str);

    // check is directory exist. Create full path if not exist yet
    fileCreateDir(mjpeg_filename.c_str());

    // Add mode suffix if configured
    if (ctrl.cmd_flds_cfg_recording.suffix_mode.v.num) {
        mjpeg_filename += std::string(
            (imu.mode == TFlowImu::IMU_MODE::COPTER)   ? "-copter" :
            (imu.mode == TFlowImu::IMU_MODE::PLANE)    ? "-plane"  :
            (imu.mode == TFlowImu::IMU_MODE::DISARMED) ? "-disarm" : "-x"
        );
    }

    // Add start time suffix
    if (ctrl.cmd_flds_cfg_recording.suffix_ts_start.v.c_str) {
        constexpr int ts_suffix_max = 64;
        std::unique_ptr<char> ts_str(new char[ts_suffix_max]);
        strftime(ts_str.get(), ts_suffix_max,
            ctrl.cmd_flds_cfg_recording.suffix_ts_start.v.c_str, tm_info);
        mjpeg_filename += std::string(ts_str.get());
    }

    if (mjpeg_filename.c_str()) {
        mjpeg_file = fopen(mjpeg_filename.c_str(), "wb");
        if (mjpeg_file) {
            g_info("Vdump file split - %s", mjpeg_filename.c_str());
        }
    }

    if (mjpeg_file) {
        recording_total_size_recalc = 1;
        mjpeg_file_size = 0;
        forced_split = 0;

        if (ctrl.cmd_flds_cfg_recording.split_time_sec.v.num > 0) {
            vdump_timeout_src = g_timeout_source_new(ctrl.cmd_flds_cfg_recording.split_time_sec.v.num * 1000);
            g_source_set_callback(vdump_timeout_src, (GSourceFunc)tflow_vstream_on_vdump_timer, this, nullptr);
            g_source_attach(vdump_timeout_src, context);
        }
    }
}

void TFlowVStream::onFrameStreaming(const TFlowBufPck::pck_consume* msg_consume)
{
    uint32_t aux_data_len = msg_consume->aux_data_len;
    const uint8_t *aux_data = msg_consume->aux_data;       // This data is from shared packet, thus do not modify!

    if (ws_streamer) {      
        TFlowBuf* ws_streamer_buf = ws_streamer ? ws_streamer->getFreeBuffer() : nullptr;

        if (ws_streamer_buf) {

            assert(msg_consume->buff_index >= 0 && msg_consume->buff_index < buf_cli_streaming->tflow_bufs.size());
            TFlowBuf &tflow_buf_in = buf_cli_streaming->tflow_bufs.at(msg_consume->buff_index);

            // TODO: implemtent Meta data sending along with encoded frame
            //       to move dashboard rendering on the host side
            
            // VStreamer receives aux data via shared memory
            // tflow_buf_in.aux_data = msg_consume->aux_data; 
            // tflow_buf_in.aux_data_len = msg_consume->aux_data_len; 

            ws_streamer->fillBuffer(*ws_streamer_buf, tflow_buf_in);
            ws_streamer->consumeBuffer(*ws_streamer_buf);
        }
        else {
            static int cnt = 0;
            g_info("============== can't get free buffer %d =============", cnt++ );
        }
    }

    if (udp_streamer) {      
        const uint8_t* buff;
        size_t buff_len;

        TFlowBuf* dashboard_buf = udp_streamer ? udp_streamer->getFreeBuffer() : nullptr;

        if (dashboard_buf) {
            memcpy(dashboard_buf->start, buff, buff_len);
            udp_streamer->consumeBuffer(*dashboard_buf);
        }
        else {
            static int cnt = 0;
            g_info("============== can't get free buffer %d =============", cnt++ );
        }
    }
}

void TFlowVStream::onFrameRecording(const TFlowBufPck::pck_consume* msg_consume)
{
#if CODE_BROWSE
    // called from
    TFlowBufCli::onMsg();
#endif

    InFrameJP &in_frame = in_frames_jp.at(msg_consume->buff_index);

#define JP_COMM_MAX_LEN 128
    char comment_txt[JP_COMM_MAX_LEN];
    int comment_len = 0;

    if (0 == isDumpRequired(msg_consume->aux_data, msg_consume->aux_data_len)) {
        return;
    }

    if (forced_split || !mjpeg_file) {
        // Split due to mode change, file size or timeout
        fileSplit();
    }
    
    if (!mjpeg_file) {
        // Can't open file
        return;
    }

#if 1
    static int g_dump_raw_img = 0;

    if (g_dump_raw_img) {
        int w = 384;
        int h = 288;
        static uint8_t buff_1[288*384];
        static uint8_t buff_2[288*384];
        for (int i = 0; i < in_frame.height; i++) {
            unsigned char *a = in_frame.jp_rows[i];
            unsigned char* b = in_frame.data + (i * 384);
            memcpy(&buff_1[i * 384], a, 384);
        }
        memcpy(buff_2, in_frame.data, 288*384);

        FILE* f1 = fopen("raw_dump1", "wb");
        FILE* f2 = fopen("raw_dump1", "wb");
        if (f1) fwrite(buff_1, 1, sizeof(buff_1), f1);
        if (f2) fwrite(buff_2, 1, sizeof(buff_2), f2);
    }
#endif

    jpeg_set_quality(&jp_cinfo, ctrl.cmd_flds_cfg_recording.jpeg_quality.v.num, TRUE);

    unsigned char* jp_buf_tmp = jp_buf;
    unsigned long jp_buf_sz_tmp = jp_buf_sz;

    jpeg_mem_dest(&jp_cinfo, &jp_buf_tmp, &jp_buf_sz_tmp);
    jpeg_start_compress(&jp_cinfo, true);
    if (comment_len) {
        jpeg_write_marker(&jp_cinfo, JPEG_COM, (uint8_t*)comment_txt, comment_len + 1); // Null terminated
    }
    jpeg_write_scanlines(&jp_cinfo, (JSAMPARRAY)in_frame.jp_rows.data(), in_frame.height );
    jpeg_finish_compress(&jp_cinfo);

    assert(jp_buf_tmp == jp_buf);   // As we provide our own buffer check it shouldn't be changed by jpeglib

#pragma pack(push, 1)
    struct {
        char sign[4];
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t aux_data_len;
        uint32_t jpeg_sz;
    } jpeg_frame_data = {
            .sign = {'R', 'T', '.', '2'},           // 0x322E5452
            .width        = in_frame.width,
            .height       = in_frame.height,
            .format       = in_frame.format,
            .aux_data_len = (uint32_t)msg_consume->aux_data_len,
            .jpeg_sz      = (uint32_t)jp_buf_sz_tmp
    };                                                      
#pragma pack(pop)

    if (mjpeg_file) {
        ssize_t bwr = 0;
        // TODO: Consider scatter-gather function like - 
        //       writev (int filedes, const struct iovec *vector, int count)
        bwr += fwrite(&jpeg_frame_data, 1, sizeof(jpeg_frame_data), mjpeg_file);
        if (msg_consume->aux_data_len) {
            bwr += fwrite(msg_consume->aux_data, 1, msg_consume->aux_data_len, 
                mjpeg_file);
        }
        bwr += fwrite(jp_buf, 1, jp_buf_sz_tmp, mjpeg_file);
        if (bwr == 0) {
            // Ooops, can't write any more. Close the file and try open a new 
            // one later.
            // AV: In case of SD card which was mounted as async,nofail is 
            //     removed on-the-fly, then fwrite doesn't return error - just 
            //     continue writes and systemd can't unmount the device.
            //     After Nth attemp systed stops trying.
            //     TODO: probably need to poll mountable status...

            time_t time_now;
            struct tm* tm_info;
            g_critical("Can't write vdump (%d) - %s", errno, strerror(errno));

            time_now = time(NULL);
            tm_info = localtime(&time_now);

            split_last_ts = time_now;
            forced_split = 1;

            // Let's cleanup ASAP.
            fileClose(tm_info);
        }
        else {
            ssize_t max_file_size = ctrl.cmd_flds_cfg_recording.split_size_mb.v.num * 1024 * 1024;
            mjpeg_file_size += bwr;
            if (max_file_size > 0 && mjpeg_file_size > max_file_size) {
                forced_split = 1;
            }
        }
    }
}

void TFlowVStream::onSrcGoneRecording()
{
    jpEncClose();

    fileClose();
}

void TFlowVStream::onSrcGoneStreaming()
{
    if (ws_streamer) {
        delete ws_streamer;
        ws_streamer = nullptr;
    }

    if (udp_streamer) {
        delete udp_streamer;
        udp_streamer = nullptr;
    }
}

void TFlowVStream::onConnect()
{
}

void TFlowVStream::onDisconnect()
{
}

void TFlowVStream::onSrcReadyRecording(const TFlowBufPck::pck_fd* src_info) 
{
    switch (src_info->format) {
    case V4L2_PIX_FMT_GREY:
        // Native format for Thermal imaging camera.
        jp_cinfo.input_components = 1;           /* # of color components per pixel */
        jp_cinfo.in_color_space = JCS_GRAYSCALE; /* colorspace of input image */
        break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_ABGR32:
        // Native format for RGB camera or TFlowProcess output.
        g_warning("Oooops - Unsupported recorder source format 0x%04X", src_info->format);
        assert(0);
        break;
    default:
        // oops. Unknown format
        g_warning("Oooops - Unknown source format 0x%04X", src_info->format);
        return;
    }

    jp_cinfo.image_width = src_info->width;
    jp_cinfo.image_height = src_info->height;

    jpeg_set_defaults(&jp_cinfo);
    jp_buf_sz = src_info->width * src_info->height;
    jp_buf = (uint8_t*)malloc(jp_buf_sz);

    in_frames_jp.reserve(src_info->buffs_num);

    for (int i = 0; i < src_info->buffs_num; i++) {
        in_frames_jp.emplace(in_frames_jp.end(),
            // Parameters of InFrame constructor
            src_info->width, 
            src_info->height, 
            src_info->format, 
            (uint8_t *)buf_cli_recording->tflow_bufs.at(i).start);
#if CODE_BROWSE
            InFrameJP x;
#endif
    }

}

void TFlowVStream::onSrcReadyStreaming(const TFlowBufPck::pck_fd* src_info) 
{
    switch (src_info->format) {
    case V4L2_PIX_FMT_GREY:
        // Normally generated by TFlowCapture for IR camera (CAM0)
        // ... 
        assert(0); 
        // TODO: Need to be explicitly converted to NV12
        //       Try allocate memory in user space and in Kernel space.
        //       Compare performance.
        break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_ABGR32:
        // Normally generated by TFlowProcess or by TFlowCapture for RGB camera (CAM1)
        // ... 
        break;
    default:
        // oops. Unknown format
        g_warning("Oooops - Unknown frame format 0x%04X", src_info->format);
        return;
    }

    // TODO: Support both streamers or make them mutally exclusive on build time?
#if WS_STREAMER
    const auto ws_streamer_cfg = 
        (TFlowWSStreamerCfg::cfg_ws_streamer*)ctrl.cmd_flds_cfg_streaming.ws_streamer.v.ref;

    ws_streamer = new TFlowWsVStreamer(src_info->width, src_info->height, src_info->format,
        ws_streamer_cfg);

#elif UDP_STREAMER
    
    const TFlowUDPStreamerCfg::cfg_udp_streamer* udp_streamer_cfg = 
        (TFlowUDPStreamerCfg::cfg_udp_streamer*)ctrl.cmd_flds_config.udp_streamer.v.ref;

    udp_streamer = new TFlowUDPVStreamer(context, dashboard_w, dashboard_h, udp_streamer_cfg);

#endif
}


int TFlowVStream::setStreamingSrc(int src, int en)
{
    // Close current stream in any case
    if (buf_cli_streaming) {
        delete buf_cli_streaming;
        buf_cli_streaming = nullptr;
    }

    if (!en) {  // Streaming disabled
        return 0;
    }

    // Check is the stream really changed
    // ???
    const char *srv_name = 
        (src == TFlowCtrlVStreamUI::VIDEO_SRC::VIDEO_SRC_CAM0) ? "com.reedl.tflow.capture0.buf-server" :
        //(src == TFlowCtrlVStreamUI::VIDEO_SRC::VIDEO_SRC_CAM1) ? "com.reedl.tflow.capture1.buf-server" :
        (src == TFlowCtrlVStreamUI::VIDEO_SRC::VIDEO_SRC_PROC) ? "com.reedl.tflow.process.buf-server" : "";

    buf_cli_streaming = new TFlowBufCli(
        context,
        "TFlowVStream", srv_name,
        std::bind(&TFlowVStream::onFrameStreaming,    this, std::placeholders::_1),   // TFlowBufCli::app_onFrame()
        std::bind(&TFlowVStream::onSrcReadyStreaming, this, std::placeholders::_1),   // TFlowBufCli::app_onSrcReady()
        std::bind(&TFlowVStream::onSrcGoneStreaming,  this),                          // TFlowBufCli::app_onSrcGone()
        std::bind(&TFlowVStream::onConnect,           this),                          // TFlowBufCli::app_onConnect()
        std::bind(&TFlowVStream::onDisconnect,        this));                         // TFlowBufCli::app_onDisconnect()

    return 0;
}

int TFlowVStream::setRecordingSrc(int src, int en)
{
    // Close current stream in any case
    if (buf_cli_recording) {
        delete buf_cli_recording;
        buf_cli_recording = nullptr;
    }

    fileClose();

    if (!en) {  // Recording disabled
        return 0;
    }

    const char *srv_name = 
        (src == TFlowCtrlVStreamUI::VIDEO_SRC::VIDEO_SRC_CAM0) ? "com.reedl.tflow.capture0.buf-server" :
        (src == TFlowCtrlVStreamUI::VIDEO_SRC::VIDEO_SRC_PROC) ? "com.reedl.tflow.process.buf-server" : "";

    buf_cli_recording = new TFlowBufCli(
        context,
        "TFlowVStream", srv_name,
        std::bind(&TFlowVStream::onFrameRecording,    this, std::placeholders::_1),   // TFlowBufCli::app_onFrame()
        std::bind(&TFlowVStream::onSrcReadyRecording, this, std::placeholders::_1),   // TFlowBufCli::app_onSrcReady()
        std::bind(&TFlowVStream::onSrcGoneRecording,  this),                          // TFlowBufCli::app_onSrcGone()
        std::bind(&TFlowVStream::onConnect,           this),                          // TFlowBufCli::app_onConnect()
        std::bind(&TFlowVStream::onDisconnect,        this));                         // TFlowBufCli::app_onDisconnect()

    return 0;
}


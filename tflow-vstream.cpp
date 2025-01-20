#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <limits>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib-unix.h>
#include <gio/gio.h>
#include <json11.hpp>

#include <linux/videodev2.h> //V4L2 stuff

#include "tflow-vstream.h"

#define IDLE_INTERVAL_MSEC 500

using namespace json11;

TFlowBuf::~TFlowBuf()
{
    if (start != MAP_FAILED) {
        munmap(start, length);
        start = MAP_FAILED;
    }
}

TFlowBuf::TFlowBuf(int cam_fd, int index, int planes_num)
{
    v4l2_buffer v4l2_buf{};
    v4l2_plane mplanes[planes_num];

    v4l2_buf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory     = V4L2_MEMORY_MMAP;
    v4l2_buf.m.planes   = mplanes;
    v4l2_buf.length     = planes_num;

    v4l2_buf.index = index;

    this->index = -1;
    this->length = 0;
    this->start = MAP_FAILED;

    // Query the information of the buffer with index=n into struct buf
    if (-1 == ioctl(cam_fd, VIDIOC_QUERYBUF, &v4l2_buf)) {
        g_warning("Can't VIDIOC_QUERYBUF (%d)", errno);
    }
    else {
        // Record the length and mmap buffer to user space
        this->length = v4l2_buf.m.planes[0].length;
        this->start = mmap(NULL, v4l2_buf.m.planes[0].length,
            PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, v4l2_buf.m.planes[0].m.mem_offset);
        this->index = index;
    }
}

TFlowBuf::TFlowBuf()
{
    // unmap ??? or unmap on the tflow-capture side only ???
}

int TFlowBuf::age() {
    int rc;
    struct timespec tp;
    unsigned long proc_frame_ms, now_ms;

    rc = clock_gettime(CLOCK_MONOTONIC, &tp);
    now_ms = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
    proc_frame_ms = ts.tv_sec * 1000 + ts.tv_usec / 1000;

    return (now_ms - proc_frame_ms);
}

InFrame::InFrame(uint32_t width, uint32_t height, uint32_t format, uint8_t* data) 
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

TFlowVStream::TFlowVStream() : 
    ctrl(*this)
{
    context = g_main_context_new();
    g_main_context_push_thread_default(context);
    
    main_loop = g_main_loop_new(context, false);
    
    ctrl.InitConfig(); // Q: ? Should it be part of constructor ?

    buf_cli = new TFlowBufCli(context);

    /* Link TFlow Buffer Client and parent class. Client will call frame 
     * processing routine from it on every received frame.
     */
    buf_cli->app = this;

    /* JPEG LIB related init */
    jp_buf = nullptr;
    jp_buf_sz = 0;

    CLEAR(jp_cinfo);
    jp_cinfo.err = jpeg_std_error(&jp_err);
    jpeg_create_compress(&jp_cinfo);

    mjpeg_file_size = 0;

    vdump_total_size = -1;          // Unknow
    vdump_total_size_recalc = 1;    // Recalculate total file size on start
    forced_split = 0;
    split_last_ts = 0;
    vdump_timeout_src = NULL;
}

TFlowVStream::~TFlowVStream()
{
    time_t time_now;
    struct tm* tm_info;

    time_now = time(NULL);
    tm_info = localtime(&time_now);

    // Finalize the vdump file if active
    fileClose(tm_info);

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    context = NULL;

    if (jp_buf) free(jp_buf);

    jpeg_destroy_compress(&jp_cinfo);
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

void TFlowVStream::vdumpCheckTotalSize()
{
    // For all files in current vdump path
    GError* error = NULL;
    const gchar *dir_entry = NULL; 
    gchar *vdump_dirname = g_path_get_dirname(ctrl.cmd_flds_cfg_vdump.path.v.c_str);
    gchar *vdump_basename = g_path_get_basename(ctrl.cmd_flds_cfg_vdump.path.v.c_str);
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

        entry_full_name = g_build_filename(vdump_dirname, dir_entry);
        
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

    if (size_total / 1024 / 1024 > ctrl.cmd_flds_cfg_vdump.max_tot_size_mb.v.num) {
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

    // Get dump files' total size
    // If more than limit, then remove the latest
    if (vdump_total_size_recalc) {
        vdump_total_size_recalc = 0;
        vdumpCheckTotalSize();
    }


    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    buf_cli->onIdle(now_ts);
}

void TFlowVStream::AttachIdle()
{
    GSource* src_idle = g_timeout_source_new(IDLE_INTERVAL_MSEC);
    g_source_set_callback(src_idle, (GSourceFunc)tflow_vstream_on_idle, this, nullptr);
    g_source_attach(src_idle, context);
    g_source_unref(src_idle);

    return;
}

int TFlowVStream::isDumpRequired(uint8_t* aux_data, size_t aux_data_len)
{
    /* 
     * By default dump is enabled and sometimes, if configured, can be disabled
     * for some case. For instance - don't dump video while disarmed.
     */
    
    // !!!!!!!!!! remove me when mounted !!!!!!!!!!
    imu.mode = TFlowImu::IMU_MODE::DISARMED;

    TFlowImu::IMU_MODE mode_prev = imu.mode;

    // No imu data - just dump 
    if (0 == aux_data_len) return 1;

    // In case of error of unknown IMU format let's Dump anyway.
    imu.ts = 0;
    if (TFlowImu::getIMU(imu, aux_data, aux_data_len)) return 1;

    // !!!!!!!!!! remove me when mounted !!!!!!!!!!
    imu.mode = TFlowImu::IMU_MODE::DISARMED;

    // By default don't dump if disarmed
    if (imu.mode == TFlowImu::IMU_MODE::DISARMED && 
        ctrl.cmd_flds_cfg_vdump.dump_disarmed.v.num == 0) { 
        return 0;
    }

    // Normlly, Disarmed, Copter of Plane mode should be debugged separately.
    // Thus, lets split dump into several files for different modes.
    if (mode_prev != imu.mode) {
        forced_split = 1;
    }

    return 1;
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
    if (ctrl.cmd_flds_cfg_vdump.suffix_ts_stop.v.c_str) {
        constexpr int ts_suffix_max = 64;
        std::unique_ptr<char> ts_str(new char[ts_suffix_max]);
        strftime(ts_str.get(), ts_suffix_max,
            ctrl.cmd_flds_cfg_vdump.suffix_ts_stop.v.c_str, tm_info);
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
    
    if (ctrl.cmd_flds_cfg_vdump.path.v.c_str == nullptr) return;

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
    mjpeg_filename = std::string(ctrl.cmd_flds_cfg_vdump.path.v.c_str);

    // check is directory exist. Create full path if not exist yet
    fileCreateDir(mjpeg_filename.c_str());

    // Add mode suffix if configured
    if (ctrl.cmd_flds_cfg_vdump.suffix_mode.v.num) {
        mjpeg_filename += std::string(
            (imu.mode == TFlowImu::IMU_MODE::COPTER)   ? "-copter" :
            (imu.mode == TFlowImu::IMU_MODE::PLANE)    ? "-plane"  :
            (imu.mode == TFlowImu::IMU_MODE::DISARMED) ? "-disarm" : "-x"
        );
    }

    // Add start time suffix
    if (ctrl.cmd_flds_cfg_vdump.suffix_ts_start.v.c_str) {
        constexpr int ts_suffix_max = 64;
        std::unique_ptr<char> ts_str(new char[ts_suffix_max]);
        strftime(ts_str.get(), ts_suffix_max,
            ctrl.cmd_flds_cfg_vdump.suffix_ts_start.v.c_str, tm_info);
        mjpeg_filename += std::string(ts_str.get());
    }

    if (mjpeg_filename.c_str()) {
        mjpeg_file = fopen(mjpeg_filename.c_str(), "wb");
        if (mjpeg_file) {
            g_info("Vdump file split - %s", mjpeg_filename.c_str());
        }
    }

    if (mjpeg_file) {
        vdump_total_size_recalc = 1;
        mjpeg_file_size = 0;
        forced_split = 0;

        if (ctrl.cmd_flds_cfg_vdump.split_time_sec.v.num > 0) {
            vdump_timeout_src = g_timeout_source_new(ctrl.cmd_flds_cfg_vdump.split_time_sec.v.num * 1000);
            g_source_set_callback(vdump_timeout_src, (GSourceFunc)tflow_vstream_on_vdump_timer, this, nullptr);
            g_source_attach(vdump_timeout_src, context);
        }
    }

}

void TFlowVStream::onFrame(int index, struct timeval ts, uint32_t seq, uint8_t *aux_data, size_t aux_data_len)
{
#if CODE_BROWSE
    // called from
    TFlowBufCli::onMsg();
        TFlowBufCli::onConsume();
#endif

    InFrame &in_frame = in_frames.at(index);

#define JP_COMM_MAX_LEN 128
    char comment_txt[JP_COMM_MAX_LEN];
    int comment_len = 0;

    if (0 == isDumpRequired(aux_data, aux_data_len)) {
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

    jpeg_set_quality(&jp_cinfo, ctrl.cmd_flds_cfg_vdump.jpeg_quality.v.num, TRUE);

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
            .aux_data_len = (uint32_t)aux_data_len,
            .jpeg_sz      = (uint32_t)jp_buf_sz_tmp
    };                                                      
#pragma pack(pop)

    if (mjpeg_file) {
        ssize_t bwr = 0;
        // TODO: Consider scatter-gather function like - 
        //       writev (int filedes, const struct iovec *vector, int count)
        bwr += fwrite(&jpeg_frame_data, 1, sizeof(jpeg_frame_data), mjpeg_file);
        if (aux_data_len) {
            bwr += fwrite(aux_data, 1, aux_data_len, mjpeg_file);
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
            ssize_t max_file_size = ctrl.cmd_flds_cfg_vdump.split_size_mb.v.num * 1024 * 1024;
            mjpeg_file_size += bwr;
            if (max_file_size > 0 && mjpeg_file_size > max_file_size) {
                forced_split = 1;
            }
        }
    }
}

void TFlowVStream::onCamFD(TFlowBuf::pck_cam_fd* cam_info) 
{
    switch (cam_info->format) {
    case V4L2_PIX_FMT_GREY:
        jp_cinfo.input_components = 1;           /* # of color components per pixel */
        jp_cinfo.in_color_space = JCS_GRAYSCALE; /* colorspace of input image */
        break;
    default:
        // oops. Unknown format
        g_warning("Oooops - Unknown frame format 0x%04X", cam_info->format);
        return;
    }

    jp_cinfo.image_width = cam_info->width;
    jp_cinfo.image_height = cam_info->height;

    jpeg_set_defaults(&jp_cinfo);
    jp_buf_sz = cam_info->width * cam_info->height;
    jp_buf = (uint8_t*)malloc(jp_buf_sz);

    in_frames.reserve(cam_info->buffs_num);

    for (int i = 0; i < cam_info->buffs_num; i++) {
        in_frames.emplace(in_frames.end(),
            // Parameters of InFrame constructor
            cam_info->width, 
            cam_info->height, 
            cam_info->format, 
            (uint8_t *)buf_cli->tflow_bufs.at(i).start);
    }

#if CODE_BROWSE
    InFrame x;
#endif 
}


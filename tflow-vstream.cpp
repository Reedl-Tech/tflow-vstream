#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <giomm.h>
#include <glib-unix.h>
#include <json11.hpp>

#include <linux/videodev2.h> //V4L2 stuff

#include "tflow-vstream.h"

#define IDLE_INTERVAL_MSEC 100

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

}

TFlowVStream::~TFlowVStream()
{

    g_source_unref(src_idle);

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    context = NULL;

    if (jp_buf) free(jp_buf);

    jpeg_destroy_compress(&jp_cinfo);
}


static gboolean tflow_vstream_idle(gpointer data)
{
    TFlowVStream* app = (TFlowVStream*)data;

    app->OnIdle();

    return G_SOURCE_CONTINUE;
}

static void tflow_vstream_idle_once(gpointer data)
{
    tflow_vstream_idle(data);
}

void TFlowVStream::OnIdle()
{
    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    buf_cli->onIdle(now_ts);
}

void TFlowVStream::AttachIdle()
{
    GSource* src_idle = g_timeout_source_new(IDLE_INTERVAL_MSEC);
    g_source_set_callback(src_idle, (GSourceFunc)tflow_vstream_idle, this, nullptr);
    g_source_attach(src_idle, context);
    g_source_unref(src_idle);

    return;
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

#if 0
    // Vstreamer shouldn't known anything about packet aux data. It is private
    // between the capture and player apps.
    assert(aux_data_len == 0 || aux_data_len == sizeof(struct imu_data));
    struct imu_data *imu_data = (struct imu_data*)aux_data;

    if (aux_data_len) {
        comment_txt[JP_COMM_MAX_LEN] = 0;
        comment_len = snprintf(comment_txt, JP_COMM_MAX_LEN - 1,
            "TS:%d.%d LOG: %d ATT:[%d, %d, %d] H:%d POS:[%d, %d, %d] HW:0x%08X",
            imu_data->tv_sec, imu_data->tv_usec, imu_data->log_ts,
            imu_data->roll,
            imu_data->pitch,
            imu_data->yaw,
            imu_data->altitude,
            imu_data->pos_x,
            imu_data->pos_y,
            imu_data->pos_z,
            imu_data->hwHealthSatus);

        comment_len = MIN(comment_len, JP_COMM_MAX_LEN - 1);
    }
#endif

    // The frame will be blocked until function returns
    // get quality from current config?
    int quality = 90;
    jpeg_set_quality(&jp_cinfo, quality, TRUE);

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
            .sign = {'R', 'T', '.', '1'},
            .width        = in_frame.width,
            .height       = in_frame.height,
            .format       = in_frame.format,
            .aux_data_len = (uint32_t)aux_data_len,
            .jpeg_sz      = (uint32_t)jp_buf_sz_tmp
    };
#pragma pack(pop)

    if (mjpeg_file) {
        fwrite(&jpeg_frame_data, sizeof(jpeg_frame_data), 1, mjpeg_file);
        fwrite(aux_data, aux_data_len, 1, mjpeg_file);
        fwrite(jp_buf, jp_buf_sz_tmp, 1, mjpeg_file);
//        fflush(mjpeg_file);
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

    if (mjpeg_filename.c_str()) {
        mjpeg_file = fopen(mjpeg_filename.c_str(), "wb");
    }
}


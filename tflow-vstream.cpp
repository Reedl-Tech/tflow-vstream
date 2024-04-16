#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <giomm.h>
#include <glib-unix.h>
#include <json11.hpp>

#include <linux/videodev2.h> //V4L2 stuff

#include <opencv2/core/ocl.hpp>

#include "tflow-vstream.h"

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

TFlowVStream::TFlowVStream() : 
    ctrl(*this)
{
    context = g_main_context_new();
    g_main_context_push_thread_default(context);
    
    main_loop = g_main_loop_new(context, false);

    ctrl.Init(); // Q: ? Should it be part of constructor ?

    buf_cli = new TFlowBufCli(context);
    fifo_streamer = NULL;

    /* Link TFlow Buffer Client and parent class. Client will call frame 
     * processing routine from it on every received frame.
     */
    buf_cli->app = this;

    last_algo_check = clock();

}

TFlowVStream::~TFlowVStream()
{

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    context = NULL;
}


static gboolean tflow_process_idle(gpointer data)
{
    TFlowVStream* app = (TFlowVStream*)data;

    app->OnIdle();

    return true;
}

void TFlowVStream::OnIdle()
{
    clock_t now = clock();
    buf_cli->onIdle(now);
}

void TFlowVStream::AttachIdle()
{
    GSource* src_idle = g_idle_source_new();
    g_source_set_callback(src_idle, (GSourceFunc)tflow_process_idle, this, nullptr);
    g_source_attach(src_idle, context);
    g_source_unref(src_idle);

    return;
}

void TFlowVStream::onFrame(int index, struct timeval ts, uint32_t seq)
{
    InFrame &in_frame = in_frames.at(index);
    
    // The frame will be blocked until function returns

}

void TFlowVStream::onCamFD(struct TFlowBuf::pck_cam_fd* cam_info) 
{
#define V4L2_PIX_FMT_GREY_META1 0

    switch (cam_info->format) {
    case V4L2_PIX_FMT_GREY:
        break;
    case V4L2_PIX_FMT_GREY_META1:
        break;
    default:
        // oops. Unknown format
    }

    for (int i = 0; i < cam_info->buffs_num; i++) {
        in_frames.emplace(in_frames.end(),
            // Parameters of InFrame constructor
            cam_info->height, 
            cam_info->width, 
            cam_info->format, 
            buf_cli->tflow_bufs.at(i).start);
    }

#if CODE_BROWSE
    InFrame x;
#endif 
}


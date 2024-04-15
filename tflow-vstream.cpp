#include <iostream>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <giomm.h>
#include <glib-unix.h>
#include <json11.hpp>

#include <linux/videodev2.h> //V4L2 stuff

#include <opencv2/core/ocl.hpp>

#include "tflow-process.h"

using namespace json11;
using namespace cv;

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
    // ???
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

TFlowProcess::TFlowProcess() : 
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

    // Get OpenCL configuration from config
    // setOpenCL(false/true);

}

TFlowProcess::~TFlowProcess()
{

    g_main_loop_unref(main_loop);
    main_loop = NULL;

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    context = NULL;
}


static gboolean tflow_process_idle(gpointer data)
{
    TFlowProcess* app = (TFlowProcess*)data;

    app->OnIdle();

    return true;
}

void TFlowProcess::OnIdle()
{
    clock_t now = clock();
    buf_cli->onIdle(now);

}

void TFlowProcess::AttachIdle()
{
    GSource* src_idle = g_idle_source_new();
    g_source_set_callback(src_idle, (GSourceFunc)tflow_process_idle, this, nullptr);
    g_source_attach(src_idle, context);
    g_source_unref(src_idle);

    return;
}

void TFlowProcess::setOpenCL(bool ocl_enabled) {

    if (!cv::ocl::haveOpenCL()) {
        g_info("OpenCL is not available...");
        return;
    }

    cv::ocl::setUseOpenCL(ocl_enabled);

    g_info("TFlowProcess: OpenCL %s in use",
        cv::ocl::useOpenCL() ? "is" : "isn't" );

#define OPENCL_INFO  0

#if OPENCL_INFO
    cv::ocl::Context context;
    if (!context.create(cv::ocl::Device::TYPE_ALL)) {
        g_warning("Failed creating the context...");
        return;
    }

    g_info("TFlowProcess: %d OpenCL devices are detected.",  context.ndevices()); 

    for (int i = 0; i < context.ndevices(); i++) {
        cv::ocl::Device device = context.device(i);
        g_info( "\tname: %s \r\n"
                "\tavailable: %d\r\n" 
                "\timageSupport: %d\r\n"
                "\tOpenCL_C_Version: %s\r\n",
            device.name(),
            device.available(),
            device.imageSupport(),
            device.OpenCL_C_Version());
    }

    // cv::ocl::Device(context.device(0));
#endif
}

void TFlowProcess::onFrame(int index, struct timeval ts, uint32_t seq)
{
    auto &in_frame = in_frames.at(index);

    cv::cvtColor(in_frame, in_frame_rgb, COLOR_GRAY2BGR);

    if (fifo_streamer) {
        fifo_streamer->fifoWrite(in_frame_rgb.datastart, in_frame_rgb.dataend - in_frame_rgb.datastart);
    }
}

void TFlowProcess::onCamFD(struct TFlowBuf::pck_cam_fd* cam_info) 
{
    uint32_t mat_fmt;

    switch (cam_info->format) {
    case V4L2_PIX_FMT_GREY:
        mat_fmt = CV_8UC1;
        break;
    default:
        mat_fmt = CV_8UC1;
    }

    for (int i = 0; i < cam_info->buffs_num; i++) {
        in_frames.emplace(in_frames.end(), 
            Mat(cam_info->height, cam_info->width, mat_fmt, buf_cli->tflow_bufs.at(i).start));
    }
 
}


#pragma once

#include <cassert>
#include <time.h>
#include <giomm.h>

#include <opencv2/opencv.hpp>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

class Flag {
public:
    enum states {
        UNDEF,
        CLR,
        SET,
        FALL,
        RISE
    };
    enum states v = Flag::UNDEF;
};

#include "tflow-ctrl-process.h"
#include "tflow-buf-cli.h"
#include "tflow-streamer.h"

class TFlowProcess {
public:
    TFlowProcess();
    ~TFlowProcess();

    GMainContext *context;
    GMainLoop *main_loop;
    
    void AttachIdle();
    void OnIdle();

    TFlowBufCli *buf_cli;
    TFlowStreamer *fifo_streamer;

    void setOpenCL(bool ocl_enabled);
    void onFrame(int index, struct timeval ts, uint32_t seq);
    void onCamFD(struct TFlowBuf::pck_cam_fd* msg);
private:

    TFlowCtrlProcess ctrl;

    Flag    algo_state_flag;     // FL_SET -> Algorithm processing enabled; FL_CLR -> disabled
    clock_t last_algo_check;

    std::vector<cv::Mat> in_frames{};
    cv::Mat in_frame_rgb;            // Input frame in RGB format. Supposed for debug info rendering.
};


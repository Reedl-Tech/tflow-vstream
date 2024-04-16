#pragma once

#include <cassert>
#include <time.h>
#include <giomm.h>

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

#include "tflow-ctrl-vstream.h"
#include "tflow-buf-cli.h"

class InFrame {
public:

    InFrame::InFrame(uint32_t width, uint32_t height, uint32_t format, uint8_t* data) {
        this->width  = width;
        this->height = height;
        this->format = format;
        this->data   = data;  
    }

    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint8_t *data;
};


class TFlowVStream {
public:
    TFlowVStream();
    ~TFlowVStream();

    GMainContext *context;
    GMainLoop *main_loop;
    
    void AttachIdle();
    void OnIdle();

    TFlowBufCli *buf_cli;

    void onFrame(int index, struct timeval ts, uint32_t seq);
    void onCamFD(struct TFlowBuf::pck_cam_fd* msg);
private:

    TFlowCtrlVStream ctrl;

    std::vector<InFrame> in_frames{};
    // cv::Mat in_frame_rgb;            // Input frame in RGB format. Supposed for debug info rendering.
};


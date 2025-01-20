#pragma once

#include <cassert>
#include <ctime>
#include <cstdint>
#include <cstdio>

#include <jpeglib.h>
#include <jerror.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#include "tflow-common.hpp"
#include "tflow-nav-imu.hpp"
#include "tflow-ctrl-vstream.h"
#include "tflow-buf-cli.h"

class InFrame {
public:
    InFrame(uint32_t width, uint32_t height, uint32_t format, uint8_t* data);

    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint8_t *data;

    // Initialized upon CamFD arrived
    // contains arrays of pointers to each image's row
    std::vector<JSAMPROW> jp_rows;
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

    void onFrame(int index, struct timeval ts, uint32_t seq, uint8_t* aux_data, size_t aux_data_len);
    void onCamFD(TFlowBuf::pck_cam_fd* msg);

    int forced_split;   // Set upon vdump timer expiration or 
                        // upon reaches the maximum size or file write error.

    GSource* vdump_timeout_src;

private:

    int isDumpRequired(uint8_t* aux_data, size_t aux_data_len);
    void fileSplit();
    void fileClose(const struct tm* tm_info);
    void fileCreateDir(const gchar* file_path);

    TFlowCtrlVStream ctrl;

    std::vector<InFrame> in_frames{};
    
    struct jpeg_compress_struct jp_cinfo;
    struct jpeg_error_mgr jp_err;
    unsigned char *jp_buf;
    unsigned long jp_buf_sz;

    FILE  *mjpeg_file = nullptr;
    std::string mjpeg_filename;
    ssize_t mjpeg_file_size;

    TFlowImu imu;
    int mode_split_countdown;
    time_t split_last_ts;

    ssize_t vdump_total_size;
    int vdump_total_size_recalc;
    void vdumpCheckTotalSize();

};


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
#include "tflow-ctrl-vstream.hpp"
#include "tflow-buf-cli.hpp"
#include "streamer-ws/tflow-ws-vstreamer.hpp"

#include "streamer-udp/tflow-udp-vstreamer.hpp"

class InFrameJP {
public:
    InFrameJP(uint32_t width, uint32_t height, uint32_t format, uint8_t* data);

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
    TFlowVStream(GMainContext *_context, const std::string cfg_fname);
    ~TFlowVStream();

    GMainContext *context;
    GMainLoop *main_loop;
    
    void AttachIdle();
    void OnIdle();

    TFlowBufCli *buf_cli_recording;
    TFlowBufCli *buf_cli_streaming;

    void onConnect();
    void onDisconnect();
    void onFrameStreaming(const TFlowBufPck::pck_consume* msg_consume);
    void onFrameRecording(const TFlowBufPck::pck_consume* msg_consume);
    void onSrcGoneRecording();
    void onSrcGoneStreaming();
    void onSrcReadyRecording(const TFlowBufPck::pck_fd* src_info);
    void onSrcReadyStreaming(const TFlowBufPck::pck_fd* src_info);

    int setStreamingSrc(int src, int en);
    int setRecordingSrc(int src, int en);


    int forced_split;   // Set upon vdump timer expiration or 
                        // upon reaches the maximum size or file write error.

    GSource* vdump_timeout_src;

    TFlowWsVStreamer *ws_streamer;
    TFlowUDPVStreamer *udp_streamer;

private:

    int isDumpRequired(const uint8_t* aux_data, size_t aux_data_len);
    void fileSplit();
    void fileClose(const struct tm* tm_info = nullptr);
    void fileCreateDir(const gchar* file_path);
    void jpEncClose();
    int  jpEncOpen(int width, int height, const std::vector<TFlowBuf> &bufs);

    TFlowCtrlVStream ctrl;

    std::vector<uint8_t*>   in_frames_process;

    std::vector<InFrameJP> in_frames_jp{};
    
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

    ssize_t recording_total_size;
    int recording_total_size_recalc;
    void recordingCheckTotalSize();

};


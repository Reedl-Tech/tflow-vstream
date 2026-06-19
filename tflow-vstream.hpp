#pragma once

#include <cassert>
#include <ctime>
#include <cstdint>
#include <cstdio>

#include <jpeglib.h>
#include <jerror.h>

#include "tflow-common.hpp"

#include "tflow-ctrl-vstream.hpp"
#include "tflow-buf-cli.hpp"

#include "streamer-rtsp/tflow-rtsp-vstreamer.hpp"
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

class TFlowJPEnc {
public:
    TFlowJPEnc(int width, int height, const std::vector<TFlowBuf> &bufs);
    ~TFlowJPEnc();
    const InFrameJP& jpEncode(int buff_idx, int qlty, unsigned char **jp_out_buf, long *jp_out_buf_sz);

    std::vector<InFrameJP> in_frames_jp;

    struct jpeg_compress_struct jp_cinfo;
    struct jpeg_error_mgr jp_err;
    unsigned char *jp_buf;
    unsigned long jp_buf_sz;

};

class TFlowVStream {
public:
    TFlowVStream(GMainContext *_context, const std::string cfg_fname);
    ~TFlowVStream();

    GMainContext *context;
    GMainLoop *main_loop;
    
    void AttachIdle();
    void OnIdle();

    TFlowBufCli *buf_cli_recording_cam0;
    TFlowBufCli *buf_cli_recording_cam1;
    TFlowBufCli *buf_cli_streaming;

    void onConnect(int src_id);
    void onDisconnect(int src_id);

    void onSrcGoneStreaming (int src_id);
    void onSrcReadyStreaming(int src_id, const TFlowBufPck::pck_src_info* src_info);
    void onFrameStreaming   (int src_id, const TFlowBufPck::pck_consume* msg_consume);
    
    void onSrcGoneRecording (int src_id);
    void onFrameRecording   (int src_id, const TFlowBufPck::pck_consume* msg_consume);
    void onSrcReadyRecording(int src_id, const TFlowBufPck::pck_src_info* src_info);

    int setStreamingSrc(TFlowCtrlVStreamUI::VIDEO_SRC src,
        TFlowCtrlVStreamUI::STREAMING_TYPE type);

    int setRecordingSrc(int src, int en);

    int forced_split;   // Set upon vdump timer expiration or 
                        // upon reaches the maximum size or file write error.

    GSource* vdump_timeout_src;

    TFlowWsVStreamer *ws_streamer;
    TFlowUDPVStreamer* udp_streamer;
    TFlowRTSPVStreamer* rtsp_streamer;

private:

    int isDumpRequired(const uint8_t* aux_data, size_t aux_data_len);
    void fileSplit();
    void fileClose(const struct tm* tm_info = nullptr);
    void fileCreateDir(const gchar* file_path);

    TFlowCtrlVStream ctrl;

    std::vector<uint8_t*> in_frames_process;

    TFlowJPEnc *jp_enc_cam0;
    TFlowJPEnc *jp_enc_cam1;

    FILE  *mjpeg_file = nullptr;
    std::string mjpeg_filename;
    ssize_t mjpeg_file_size;

    int mode_split_countdown;
    time_t split_last_ts;

    ssize_t recording_total_size;
    int recording_total_size_recalc;
    void recordingCheckTotalSize();

};


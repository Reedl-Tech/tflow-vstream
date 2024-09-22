#pragma once

#include <cassert>
#include <time.h>
#include <giomm.h>
#include <jpeglib.h>
#include <jerror.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#include "tflow-common.hpp"
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

#if 0
    /*
     * Data arrived from TFlowCpature -> TFlowBufSrv -> TFlowBufCli
     * will be copied to TFlowBuf on each frame and sent to all TFlowBuf clients
     */
#pragma pack(push, 1)
    struct imu_data {
        uint32_t sign;
        uint32_t tv_sec;      // Local timestamp
        uint32_t tv_usec;     // Local timestamp
        uint32_t log_ts;      // Timestamp received from AP
        int32_t roll;         // In degrees * 100
        int32_t pitch;        // In degrees * 100
        int32_t yaw;          // In degrees * 100
        int32_t altitude;     // In meters * 100
        int32_t pos_x;
        int32_t pos_y;
        int32_t pos_z;

        uint32_t flightModeFlags;
        uint32_t stateFlags;
        uint32_t hwHealthSatus;
        uint8_t  failsafePhase;
        uint8_t  receiver_status;

    } aux_imu_data;
#pragma pack(pop)
#endif
private:

    TFlowCtrlVStream ctrl;

    GSource* src_idle;

    std::vector<InFrame> in_frames{};
    
    struct jpeg_compress_struct jp_cinfo;
    struct jpeg_error_mgr jp_err;
    unsigned char *jp_buf;
    unsigned long jp_buf_sz;

    FILE  *mjpeg_file = nullptr;
    std::string mjpeg_filename = std::string("/tmp/tflow-vstream-dump.bin");
};


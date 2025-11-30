#pragma once

#include <cassert>
#include <vector>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

#include <linux/videodev2.h> //V4L2 stuff

#include <glib-unix.h>

#include "tflow-common.hpp"
#include "tflow-buf.hpp"
#include "tflow-buf-pck.hpp"

class TFlowBufCli {
public:
    TFlowBufCli(
        GMainContext* app_context,
        const char* _cli_name, const char* _srv_name,
        std::function<void(const TFlowBufPck::pck_consume* msg_consume)> app_onFrame,
        std::function<void(const TFlowBufPck::pck_fd* src_info)> app_onSrcReady,
        std::function<void()> app_onSrcGone,
        std::function<void()> app_onConnect,
        std::function<void()> app_onDisconnect);

    ~TFlowBufCli();
    
    void onIdle_no_ts();
    void onIdle(const struct timespec &now_ts);

    int Connect();
    void Disconnect();
    int onMsg();

    int sendMsg(TFlowBufPck::pck &msg, int msg_id, int msg_custom_len);
    int sendSignature();
    int sendPing();
    int sendRedeem(int index);      // Reedem TFlowBuf back to server and request new packet

    int sck_fd;
    Flag sck_state_flag;

    typedef struct {
        GSource g_source;
        TFlowBufCli* cli;
    } GSourceCli;

    GSourceCli* sck_src;
    gpointer sck_tag;
    GSourceFuncs sck_gsfuncs;

    std::vector<TFlowBuf> tflow_bufs;

    std::function<void(const TFlowBufPck::pck_consume* msg)> app_onFrame;
    std::function<void(const TFlowBufPck::pck_fd* src_info)> app_onSrcReady;
    std::function<void()> app_onSrcGone;
    std::function<void()> app_onConnect;
    std::function<void()> app_onDisconnect;

private:

    GMainContext* context;

    int pending_buf_request;
    int msg_seq_num;
    int cam_fd;
    uint8_t* shm_wr_ptr;
    size_t shm_wr_len;

    const std::string srv_name;
    const std::string cli_name;

    struct timespec last_send_ts;
    struct timespec last_conn_check_ts;

    int onSrcFD(TFlowBufPck::pck_fd* msg, int cam_fd);
    int onSrcFDShm(TFlowBufPck::pck_fd* msg_src_info, int shm_fd);
    int onSrcFDCam(TFlowBufPck::pck_fd* msg_src_info, int cam_fd);
    int onConsume(const TFlowBufPck::pck_consume* msg);

};

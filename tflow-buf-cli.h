#pragma once

#include <cassert>
#include <time.h>
#include <linux/videodev2.h> //V4L2 stuff

#include <glib-unix.h>

#include "tflow-buf.h"

#define TFLOWBUFSRV_SOCKET_NAME "com.reedl.tflow.buf-server"

class TFlowBufSrvPort {
    int dummy;
};

class TFlowVStream;

class TFlowBufCli {
public:
    TFlowBufCli(GMainContext* context);
    ~TFlowBufCli();
    
    TFlowVStream *app;

    void onIdle(clock_t now);
    int Connect();
    void Disconnect();
    int onMsg();

    int sendMsg(TFlowBuf::pck_t* msg, int msg_id);
    int sendSignature();
    int sendPing();
    int sendRedeem(int index);      // Reedem TFlowBuf back to server and request new packet

    int sck_fd;
    Flag sck_state_flag;

    typedef struct
    {
        GSource g_source;
        TFlowBufCli* cli;
    } GSourceCli;

    GSourceCli* sck_src;
    gpointer sck_tag;
    GSourceFuncs sck_gsfuncs;

    TFlowBufSrvPort* buf_srv;
    
    std::vector<TFlowBuf> tflow_bufs;
private:
    GMainContext* context;
    clock_t last_idle_check = 0;

    int msg_seq_num = 0;
    int cam_fd;

    clock_t last_send_ts;

    int onCamFD(struct TFlowBuf::pck_cam_fd *msg, int cam_fd);
    int onConsume(struct TFlowBuf::pck_consume* msg);

};


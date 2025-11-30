#pragma once

#include <ctime>
#include <string>
#include <glib-unix.h>
#include <json11.hpp>

class TFlowCtrlSrv;
class TFlowCtrlCliPort {

public:
    TFlowCtrlCliPort(GMainContext* context, TFlowCtrlSrv &srv, int fd);
    ~TFlowCtrlCliPort();

    std::string signature;

private:

    TFlowCtrlSrv &srv;      // is used to report socket error to the Server

    struct timespec last_idle_check;
    struct timespec last_send_ts;

    int pid;

    typedef struct {
        GSource g_source;
        TFlowCtrlCliPort* cli_port;
    } GSourceCliPort;

    int sck_fd;

    static gboolean tflow_ctrl_cli_port_dispatch(GSource* g_source, GSourceFunc callback, gpointer user_data);
    int onMsg();
    int onMsgSign(const json11::Json& j_params);

    int sendResp(const char *cmd, int err, const json11::Json::object& j_resp_params);
    GSourceCliPort* sck_src;
    gpointer        sck_tag;
    GSourceFuncs    sck_gsfuncs;

    size_t in_msg_size;
    char* in_msg;
};

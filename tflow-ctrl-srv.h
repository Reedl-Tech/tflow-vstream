#pragma once

#include <cassert>
#include <ctime>

#include <glib-unix.h>
#include <json11.hpp>

#include "tflow-common.hpp"
#include "tflow-ctrl-cli-port.h" 

class TFlowCtrlSrv {
public:

    TFlowCtrlSrv(const std::string &my_name, const std::string & srv_sck_name, GMainContext* context);
    ~TFlowCtrlSrv();
    int StartListening();
    void onIdle(struct timespec now_ts);

    virtual int onCliPortConnect(int fd) { return 0; };
    virtual void onCliPortError(int fd) {};

    virtual void onSignature(json11::Json::object& j_params, int& err) {};
    virtual void onTFlowCtrlMsg(const std::string& cmd, const json11::Json& j_in_params, json11::Json::object& j_out_params, int& err) {};
#if CODE_BROWSE
    TFlowCtrlSrvCapture::onTFlowCtrlMsg();
    TFlowCtrlSrvProcess::onTFlowCtrlMsg();
            TFlowCtrlProcess::cmd_cb_cfg_player();
    TFlowCtrlSrvVStream::onTFlowCtrlMsg();
#endif
    
    GMainContext* context;
    std::string my_name;
    struct timespec last_idle_check_ts;

private:
    std::string ctrl_srv_name;

    int sck_fd = -1;
    Flag sck_state_flag;

    typedef struct {
        GSource g_source;
        TFlowCtrlSrv* srv;
    } GSourceSrv;

    GSourceSrv* sck_src;
    gpointer sck_tag;
    GSourceFuncs sck_gsfuncs;

    static gboolean tflow_ctrl_srv_dispatch(GSource* g_source, GSourceFunc callback, gpointer user_data);
    void onConnect();
};

#pragma once

#include <cassert>
#include <ctime>

#include <unordered_map>
#include <glib-unix.h>
#include <json11.hpp>

#include "tflow-common.hpp"
#include "tflow-ctrl-cli-port.hpp" 

class TFlowCtrlSrv {
public:

    TFlowCtrlSrv(const std::string &my_name, const std::string &srv_sck_name, GMainContext* context);
    ~TFlowCtrlSrv();
    int StartListening();
    void onIdle(const struct timespec &now_ts);

    int onCliPortConnect(int fd);
    void onCliPortError(int fd);

    virtual void onSignature(json11::Json::object& j_params, int& err) {};
    virtual void onTFlowCtrlMsg(const std::string& cmd, const json11::Json& j_in_params, json11::Json::object& j_out_params, int& err) {};
#if CODE_BROWSE
    TFlowCtrlSrvCapture::onTFlowCtrlMsg();
    TFlowCtrlSrvVStream::onTFlowCtrlMsg();
    TFlowCtrlSrvProcess::onTFlowCtrlMsg();
        TFlowCtrlProcess::cmd_cb_cfg_player();
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
    gboolean onConnect();

    std::unordered_map<int, TFlowCtrlCliPort> ctrl_clis;    // TODO: can it be moved inside TFlowCtrlSrv ?

};

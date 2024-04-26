#pragma once 
#include <stdint.h>
#include <functional>
#include <vector>

#include "tflow-ctrl.h"

using std::placeholders::_1;

class TFlowCtrlVStream;
class TFlowVStream;

class TFlowCtrlVStream : private TFlowCtrl {
public:

    TFlowCtrlVStream(TFlowVStream& app);
    void Init();
    
    int vstreamer_param_1_get();
    int state_get();

    struct tflow_cmd_flds_version {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_version = {
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_config {
        tflow_cmd_field_t   state;
        tflow_cmd_field_t   vstreamer_param_1;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_config = {
        .state             = { "state",             CFT_NUM, 0, {.num = 0} },
        .vstreamer_param_1 = { "vstreamer_param_1", CFT_NUM, 0, {.num = 0} },
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_set_as_def {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_set_as_def = {
        TFLOW_CMD_EOMSG
    };

    //TFlowCtrlOnCmd cmd_cb_version;
    int cmd_cb_version(const json11::Json& j_in_params, Json::object& j_out_params);
    int cmd_cb_config(const json11::Json& j_in_params, Json::object& j_out_params);
    int cmd_cb_set_as_def(const json11::Json& j_in_params, Json::object& j_out_params);

    typedef struct {
        const char* name;
        tflow_cmd_field_t* fields;
        std::function<int(Json& json, Json::object& j_out_params)> cb;
    } tflow_cmd_t;

#define TFLOW_VSTREAM_RPC_CMD_VERSION    0
#define TFLOW_VSTREAM_RPC_CMD_CONFIG     1
#define TFLOW_VSTREAM_RPC_CMD_SET_AS_DEF 2
#define TFLOW_VSTREAM_RPC_CMD_LAST       3

#define THIS_M(_f) std::bind(&TFlowCtrlVStream::_f, this, std::placeholders::_1, std::placeholders::_2)

    tflow_cmd_t ctrl_process_rpc_cmds[TFLOW_VSTREAM_RPC_CMD_LAST + 1] = {
        [TFLOW_VSTREAM_RPC_CMD_VERSION   ] = { "version",     (tflow_cmd_field_t*)&cmd_flds_version,    THIS_M(cmd_cb_version) },
        [TFLOW_VSTREAM_RPC_CMD_CONFIG    ] = { "config",      (tflow_cmd_field_t*)&cmd_flds_config,     THIS_M(cmd_cb_config) },
        [TFLOW_VSTREAM_RPC_CMD_SET_AS_DEF] = { "set_as_def",  (tflow_cmd_field_t*)&cmd_flds_set_as_def, THIS_M(cmd_cb_set_as_def) },
        [TFLOW_VSTREAM_RPC_CMD_LAST] = { nullptr , nullptr, nullptr }
    };

private:

    TFlowVStream& app;      // AV: Why?

    const char* cfg_fname = "tflow-vstream-config.json";
};

#pragma once 
#include <stdint.h>
#include <functional>
#include <vector>

#include "tflow-ctrl.h"

using std::placeholders::_1;

#define TFLOW_CMD_EOMSG .eomsg = {.name = nullptr, .type = CFT_LAST, .max_len = 0, .v = {.u32 = 0} }

class TFlowCtrlVStream;
static int _cmd_cb_sign      (TFlowCtrlVStream* obj, Json& json);
static int _cmd_cb_config    (TFlowCtrlVStream* obj, Json& json);
static int _cmd_cb_set_as_def(TFlowCtrlVStream* obj, Json& json);

class TFlowVStream;
class TFlowCtrlVStream : private TFlowCtrl<TFlowCtrlVStream> {
public:

    TFlowCtrlVStream(TFlowVStream& app);
    void Init();

    int algo_param_1_get();
    int state_get();

    struct tflow_cmd_flds_sign {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_sign = {
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_config {
        tflow_cmd_field_t   state;
        tflow_cmd_field_t   algo_param_1;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_config = {
        .state             = { "state",             CFT_NUM, 0, {.u32 = 0} },
        .vstreamer_param_1 = { "vstreamer_param_1", CFT_TXT, 0, {.u32 = 0} },
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_set_as_def {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_set_as_def = {
        TFLOW_CMD_EOMSG
    };

    int cmd_cb_sign(Json& json);
    int cmd_cb_config(Json& json);
    int cmd_cb_set_as_def(Json& json);

#define TFLOW_PROCESS_RPC_CMD_SIGN       0
#define TFLOW_PROCESS_RPC_CMD_CONFIG     1
#define TFLOW_PROCESS_RPC_CMD_SET_AS_DEF 2
#define TFLOW_PROCESS_RPC_CMD_LAST       3

    tflow_cmd_t ctrl_process_rpc_cmds[TFLOW_PROCESS_RPC_CMD_LAST + 1] = {
        [TFLOW_PROCESS_RPC_CMD_SIGN      ] = { "sign",        (tflow_cmd_field_t*)&cmd_flds_sign,       _cmd_cb_sign },
        [TFLOW_PROCESS_RPC_CMD_CONFIG    ] = { "config",      (tflow_cmd_field_t*)&cmd_flds_config,     _cmd_cb_config },
        [TFLOW_PROCESS_RPC_CMD_SET_AS_DEF] = { "set_as_def",  (tflow_cmd_field_t*)&cmd_flds_set_as_def, _cmd_cb_set_as_def },
        [TFLOW_PROCESS_RPC_CMD_LAST] = { nullptr , nullptr, nullptr }
    };

    
    //std::function<int (Json& json)> f_add_display2 = std::bind(&TFlowCtrlProcess::cmd_cb_sign, this, _1);

private:

    TFlowVStream& app;      // AV: Why?

    const char* cfg_fname = "tflow-vstream-config.json";
};

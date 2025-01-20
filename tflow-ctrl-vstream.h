#pragma once 
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "tflow-ctrl.h"

using std::placeholders::_1;

class TFlowCtrlVStream;
class TFlowVStream;

class TFlowCtrlVStream : private TFlowCtrl {
public:

    TFlowCtrlVStream(TFlowVStream& app);
    TFlowVStream& app;      // AV: For access to context. Passed to CtrlServer

    void InitConfig();
    void InitServer();

    int vdump_get();
    int state_get();

    struct tflow_cmd_flds_version {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_version = {
        TFLOW_CMD_EOMSG
    };


    struct cfg_vdump {
        tflow_cmd_field_t   head;
        tflow_cmd_field_t   path;
        tflow_cmd_field_t   suffix_ts_start;           
        tflow_cmd_field_t   suffix_ts_stop;           
        tflow_cmd_field_t   suffix_mode;         
        tflow_cmd_field_t   dump_disarmed;       
        tflow_cmd_field_t   split_on_mode_change;
        tflow_cmd_field_t   split_size_mb;       
        tflow_cmd_field_t   split_time_sec;
        tflow_cmd_field_t   max_tot_size_mb;       
        tflow_cmd_field_t   jpeg_quality;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_vdump = {
        .head = { "vdump", CFT_STR, 0, {.str = nullptr} },
        .path                 = { "path",                 CFT_STR, 0, {.c_str = strdup("/home/root/tflow")} },
        .suffix_ts_start      = { "suffix_ts_start",      CFT_STR, 0, {.c_str = strdup("-%F--%H-%M")      } },
        .suffix_ts_stop       = { "suffix_ts_stop",       CFT_STR, 0, {.c_str = strdup("--%H-%M")         } },
        .suffix_mode          = { "suffix_mode",          CFT_STR, 0, {.c_str = nullptr           } },
        .dump_disarmed        = { "dump_disarmed",        CFT_NUM, 0, {.num = 0    } },       // Is used for goodFeaturesToTrack()
        .split_on_mode_change = { "split_on_mode_change", CFT_NUM, 0, {.num = 0    } },       // Is used for Contrast and Quality calculation
        .split_size_mb        = { "split_size_mb",        CFT_NUM, 0, {.num = 0    } },
        .split_time_sec       = { "split_time_sec",       CFT_NUM, 0, {.num = 0    } },
        .max_tot_size_mb      = { "max_tot_size_mb",      CFT_NUM, 0, {.num = 4000 } },
        .jpeg_quality         = { "jpeg_quality",         CFT_NUM, 0, {.num = 90   } },
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_config {
        tflow_cmd_field_t   state;
        tflow_cmd_field_t   vdump;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_config = {
        .state = { "state",     CFT_NUM, 0, {.num = 0} },
        .vdump = { "vdump",     CFT_REF, 0, {.ref = &(cmd_flds_cfg_vdump.head)} },
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_set_as_def {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_set_as_def = {
        TFLOW_CMD_EOMSG
    };

    //TFlowCtrlOnCmd cmd_cb_version;
    int cmd_cb_version(const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_config(const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_set_as_def(const json11::Json& j_in_params, json11::Json::object& j_out_params);

    typedef struct {
        const char* name;
        tflow_cmd_field_t* fields;
        std::function<int(json11::Json& json, json11::Json::object& j_out_params)> cb;
    } tflow_cmd_t;

#define TFLOW_VSTREAM_RPC_CMD_VERSION    0
#define TFLOW_VSTREAM_RPC_CMD_CONFIG     1
#define TFLOW_VSTREAM_RPC_CMD_SET_AS_DEF 2
#define TFLOW_VSTREAM_RPC_CMD_LAST       3

    tflow_cmd_t ctrl_process_rpc_cmds[TFLOW_VSTREAM_RPC_CMD_LAST + 1] = {
        [TFLOW_VSTREAM_RPC_CMD_VERSION   ] = { "version",     (tflow_cmd_field_t*)&cmd_flds_version,    THIS_M(&TFlowCtrlVStream::cmd_cb_version) },
        [TFLOW_VSTREAM_RPC_CMD_CONFIG    ] = { "config",      (tflow_cmd_field_t*)&cmd_flds_config,     THIS_M(&TFlowCtrlVStream::cmd_cb_config) },
        [TFLOW_VSTREAM_RPC_CMD_SET_AS_DEF] = { "set_as_def",  (tflow_cmd_field_t*)&cmd_flds_set_as_def, THIS_M(&TFlowCtrlVStream::cmd_cb_set_as_def) },
        [TFLOW_VSTREAM_RPC_CMD_LAST] = { nullptr , nullptr, nullptr }
    };

private:

    const char* cfg_fname = "tflow-vstream-config.json";
    const char* ctrl_srv_name = "_com.reedl.tflow.ctrl-server-vstream";
};

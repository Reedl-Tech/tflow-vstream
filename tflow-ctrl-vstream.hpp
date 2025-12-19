#pragma once 
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "tflow-ctrl.hpp"
#include "tflow-ctrl-srv-vstream.hpp"

#include "tflow-ctrl-vstream-ui.hpp"

#include "streamer-udp/tflow-udp-vstreamer-cfg.hpp"
#include "streamer-ws/tflow-ws-vstreamer-cfg.hpp"
#include "streamer-rtsp/tflow-rtsp-vstreamer-cfg.hpp"

using std::placeholders::_1;

class TFlowVStream;

class TFlowCtrlVStream : private TFlowCtrlVStreamUI, private TFlowCtrl {
public:

    TFlowCtrlVStream(TFlowVStream& app, const std::string _cfg_fname);
    TFlowVStream& app;      // AV: For access to context. Passed to CtrlServer

    void InitServer();

    struct cmd_flds_set_as_def {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_set_as_def = {
        TFLOW_CMD_EOMSG
    };

    struct cmd_flds_version {
        tflow_cmd_field_t   eomsg;
    } cmd_flds_version = {
        TFLOW_CMD_EOMSG
    };

    struct cfg_recording {
        tflow_cmd_field_t   head;
        tflow_cmd_field_t   en;
        tflow_cmd_field_t   src;
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
    } cmd_flds_cfg_recording = {
        TFLOW_CMD_HEAD("recording"),
        .en                   = { "rec_en",               CFT_NUM, 0, {.num = 0                           }, &ui_sw_en      },
        .src                  = { "rec_src",              CFT_NUM, 0, {.num = VIDEO_SRC_CAM0              }, &ui_video_src  },
        .path                 = { "rec_path",             CFT_STR, 0, {.c_str = strdup("/home/root/tflow")}, &ui_edit_def   },
        .suffix_ts_start      = { "suffix_ts_start",      CFT_STR, 0, {.c_str = strdup("-%F--%H-%M")      }, &ui_edit_def   },
        .suffix_ts_stop       = { "suffix_ts_stop",       CFT_STR, 0, {.c_str = strdup("--%H-%M")         }, &ui_edit_def   },
        .suffix_mode          = { "suffix_mode",          CFT_NUM, 0, {.num = 0                           }, &ui_switch_def },
        .dump_disarmed        = { "dump_disarmed",        CFT_NUM, 0, {.num = 0                           }, &ui_switch_def },
        .split_on_mode_change = { "split_on_mode_change", CFT_NUM, 0, {.num = 0                           }, &ui_switch_def },
        .split_size_mb        = { "split_size_mb",        CFT_NUM, 0, {.num = 0                           }, &ui_edit_def   },
        .split_time_sec       = { "split_time_sec",       CFT_NUM, 0, {.num = 0                           }, &ui_edit_def   },
        .max_tot_size_mb      = { "max_tot_size_mb",      CFT_NUM, 0, {.num = 4000                        }, &ui_edit_def   },
        .jpeg_quality         = { "jpeg_quality",         CFT_NUM, 0, {.num = 90                          }, &ui_edit_def   },
        TFLOW_CMD_EOMSG
    };

    struct cfg_streaming {
        tflow_cmd_field_t   head;
        tflow_cmd_field_t   type;
        tflow_cmd_field_t   src;
        tflow_cmd_field_t   udp_streamer;
        tflow_cmd_field_t   ws_streamer;
        tflow_cmd_field_t   rtsp_streamer;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_streaming = {
        TFLOW_CMD_HEAD("streaming"),
        .type          = { "streaming_type", CFT_NUM, 0, {.num = STREAMING_TYPE_DIS }, &ui_streaming_type },
        .src           = { "streaming_src",  CFT_NUM, 0, {.num = VIDEO_SRC_PROC }, &ui_video_src },
        .udp_streamer  = { "udp_streamer",   CFT_REF, 0, {.ref = &tflow_udp_streamer_cfg.cmd_flds_cfg_udp_streamer.head} /* , &ui_group_def */},   
        .ws_streamer   = { "ws_streamer",    CFT_REF, 0, {.ref = &tflow_ws_streamer_cfg.cmd_flds_cfg_ws_streamer.head  }, &ui_group_def},   
        .rtsp_streamer = { "rtsp_streamer",  CFT_REF, 0, {.ref = &tflow_rtsp_streamer_cfg.cmd_flds_cfg_rtsp_streamer.head  }, &ui_group_def},   
        TFLOW_CMD_EOMSG
    };

    struct tflow_cmd_flds_config {
        tflow_cmd_field_t   recording;
        tflow_cmd_field_t   streaming;
        tflow_cmd_field_t   eomsg;
    } cmd_flds_config = {
        .recording = { "recording",  CFT_REF, 0, {.ref = &(cmd_flds_cfg_recording.head)} },
        .streaming = { "streaming",  CFT_REF, 0, {.ref = &(cmd_flds_cfg_streaming.head)} },
        TFLOW_CMD_EOMSG
    };

    int cmd_cb_streaming_version   (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_streaming_config    (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_streaming_ui_sign   (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_streaming_set_as_def(const json11::Json& j_in_params, json11::Json::object& j_out_params);

    int cmd_cb_recording_version   (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_recording_config    (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_recording_ui_sign   (const json11::Json& j_in_params, json11::Json::object& j_out_params);
    int cmd_cb_recording_set_as_def(const json11::Json& j_in_params, json11::Json::object& j_out_params);

    enum {
        TFLOW_VSTREAM_RPC_CMD_STREAMING_VER,
        TFLOW_VSTREAM_RPC_CMD_STREAMING_DEF,
        TFLOW_VSTREAM_RPC_CMD_STREAMING_UI_SIGN,
        TFLOW_VSTREAM_RPC_CMD_STREAMING_CFG,
        TFLOW_VSTREAM_RPC_CMD_RECORDING_VER,
        TFLOW_VSTREAM_RPC_CMD_RECORDING_DEF,
        TFLOW_VSTREAM_RPC_CMD_RECORDING_UI_SIGN,
        TFLOW_VSTREAM_RPC_CMD_RECORDING_CFG,
        TFLOW_VSTREAM_RPC_CMD_LAST
    };

    tflow_cmd_t ctrl_vstream_rpc_cmds[TFLOW_VSTREAM_RPC_CMD_LAST + 1] = {
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_STREAMING_VER    ) { "streaming_version",    (tflow_cmd_field_t*)&cmd_flds_version,       THIS_M(&TFlowCtrlVStream::cmd_cb_streaming_version   ) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_STREAMING_DEF    ) { "streaming_set_as_def", (tflow_cmd_field_t*)&cmd_flds_set_as_def,    THIS_M(&TFlowCtrlVStream::cmd_cb_streaming_set_as_def) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_STREAMING_UI_SIGN) { "streaming_ui_sign",    (tflow_cmd_field_t*)&cmd_flds_cfg_streaming, THIS_M(&TFlowCtrlVStream::cmd_cb_streaming_ui_sign   ) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_STREAMING_CFG    ) { "streaming_config",     (tflow_cmd_field_t*)&cmd_flds_cfg_streaming, THIS_M(&TFlowCtrlVStream::cmd_cb_streaming_config    ) },

        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_RECORDING_VER    ) { "recording_version",    (tflow_cmd_field_t*)&cmd_flds_version,       THIS_M(&TFlowCtrlVStream::cmd_cb_recording_version   ) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_RECORDING_DEF    ) { "recording_set_as_def", (tflow_cmd_field_t*)&cmd_flds_set_as_def,    THIS_M(&TFlowCtrlVStream::cmd_cb_recording_set_as_def) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_RECORDING_UI_SIGN) { "recording_ui_sign",    (tflow_cmd_field_t*)&cmd_flds_cfg_recording, THIS_M(&TFlowCtrlVStream::cmd_cb_recording_ui_sign   ) },
        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_RECORDING_CFG    ) { "recording_config",     (tflow_cmd_field_t*)&cmd_flds_cfg_recording, THIS_M(&TFlowCtrlVStream::cmd_cb_recording_config    ) },

        ARRAY_INIT_IDX(TFLOW_VSTREAM_RPC_CMD_LAST) { nullptr , nullptr, nullptr }
    };

    TFlowCtrlSrvVStream ctrl_srv;

    void getSignResponse(json11::Json::object &j_params);
    void getStreamingUISignResponse(json11::Json::object &j_params);

    void getRecordingUISignResponse(json11::Json::object &j_params);

private:

    const std::string cfg_fname;
};

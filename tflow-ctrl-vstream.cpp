#include <cstring>
#include <sys/stat.h>
#include <glib-unix.h>

#include <json11.hpp>
#include "tflow-vstream.hpp"

using namespace json11;

static const char *raw_cfg_default =  R"( 
{
    "recording_config" : {
        "vstreamer_param_1" : "xz",
        "vdump" : {
            "path"                 : "/home/root/tflow-vdump", 
            "suffix_ts_start"      : "-%F--%H-%M",
            "suffix_ts_stop"       : "--%H-%M",
            "suffix_mode"          : "%s",
            "dump_disarmed"        : 1,
            "split_on_mode_change" : 1,
            "split_size_mb"        : 400,
            "split_time_sec"       : 1200,
            "jpeg_quality"         : 90
        }
    }
} 
)";

TFlowCtrlSrvVStream::TFlowCtrlSrvVStream(TFlowCtrlVStream& _ctrl_vstream, GMainContext* context) :
    TFlowCtrlSrv(
        std::string("VStream"),
        std::string("_com.reedl.tflow.ctrl-server-vstream"),
        context),
    ctrl_vstream(_ctrl_vstream)
{
}

TFlowCtrlVStream::TFlowCtrlVStream(TFlowVStream& _app, const std::string _cfg_fname) :
    app(_app),
    cfg_fname(_cfg_fname),
    ctrl_srv(*this, _app.context)
{
    parseConfig(ctrl_vstream_rpc_cmds, cfg_fname, raw_cfg_default);
    InitServer();
}

void TFlowCtrlVStream::InitServer()
{

}

/*********************************/
/*** Application specific part ***/
/*********************************/
void TFlowCtrlSrvVStream::onSignature(Json::object& j_out_params, int& err)
{
    err = 0;
    ctrl_vstream.getSignResponse(j_out_params);
    return;
}

void TFlowCtrlSrvVStream::onTFlowCtrlMsg(const std::string &cmd, 
    const Json& j_in_params, Json::object& j_out_params, int& err)
{
    // Find command by name
    // Call command's processor from table
    TFlowCtrl::tflow_cmd_t *ctrl_cmd_p = ctrl_vstream.ctrl_vstream_rpc_cmds;
    while (ctrl_cmd_p->name) {
        if (0 == strncmp(ctrl_cmd_p->name, cmd.c_str(), cmd.length())) {
            err = ctrl_cmd_p->cb(j_in_params, j_out_params);
#if CODE_BROWSE
            TFlowCtrlVStream::cmd_cb_streaming_config();
            TFlowCtrlVStream::cmd_cb_streaming_ui_sign();
            TFlowCtrlVStream::cmd_cb_recording_config();
            TFlowCtrlVStream::cmd_cb_recording_ui_sign();
#endif
            return;
        }
        ctrl_cmd_p++;
    }
    err = -100;
    return;
}

void TFlowCtrlVStream::getSignResponse(json11::Json::object &j_out_params)
{
    j_out_params.emplace("state", "OK");
    j_out_params.emplace("version", "v0");  // TODO: replace for version from git or signature hash or both?
    j_out_params.emplace("config_id", config_id);  
}

void TFlowCtrlVStream::getStreamingUISignResponse(json11::Json::object &j_out_params)
{
    getSignResponse(j_out_params);

    const tflow_cmd_t *cmd_config = &ctrl_vstream_rpc_cmds[TFLOW_VSTREAM_RPC_CMD_STREAMING_CFG];

    Json::array j_resp_controls_arr;
    collectCtrls(cmd_config->fields, j_resp_controls_arr);
    j_out_params.emplace("controls", j_resp_controls_arr);
}

int TFlowCtrlVStream::cmd_cb_streaming_version(const json11::Json& j_in_params, Json::object& j_out_params)
{
    j_out_params.emplace("version", "v0");
    return 0;
}

int TFlowCtrlVStream::cmd_cb_streaming_ui_sign(const json11::Json& j_in_params, Json::object& j_out_params)
{
    g_info("Streaming UI Sign command\n");

    getStreamingUISignResponse(j_out_params);

    return 0;
}

int TFlowCtrlVStream::cmd_cb_streaming_set_as_def(const json11::Json& j_in_params, Json::object& j_out_params)
{
    return 0;
}

int TFlowCtrlVStream::cmd_cb_streaming_config(const json11::Json& j_in_params, Json::object& j_out_params)
{
    tflow_cmd_field_t* rw_flds = (tflow_cmd_field_t*)&cmd_flds_cfg_streaming;

    g_info("Streaming config command: %s", j_in_params.dump().c_str());

    // Fill config fields with values from Json input object
    int was_changed = 0;
    int rc = setCmdFields(rw_flds, j_in_params, was_changed);

    if ( rc != 0 ) {
        j_out_params.emplace("error", "Can't parse");
        return rc;
        // TODO: Add notice or error to out_params in case of error.
        //       We can't just return from here, because some parameters
        //       might be already changed and we don't have rollback functionality.
    }

    //std::string indent("|");
    //dumpFieldFlags(flds, indent);

    if (cmd_flds_cfg_streaming.src.flags & FIELD_FLAG::CHANGED ||
        cmd_flds_cfg_streaming.type.flags & FIELD_FLAG::CHANGED) {
        app.setStreamingSrc(
            (TFlowCtrlVStreamUI::VIDEO_SRC)cmd_flds_cfg_streaming.src.v.num,
            (TFlowCtrlVStreamUI::STREAMING_TYPE)cmd_flds_cfg_streaming.type.v.num);
    }

    if (cmd_flds_cfg_streaming.ws_streamer.flags & FIELD_FLAG::CHANGED) {
        auto ws_streamer_rw_cfg = (TFlowWSStreamerCfg::cfg_ws_streamer*)
            cmd_flds_cfg_streaming.ws_streamer.v.ref;

        if (app.ws_streamer) {
            app.ws_streamer->onConfigValidate(j_out_params, ws_streamer_rw_cfg);
            app.ws_streamer->onConfig(j_out_params);
        }
    }

    if (cmd_flds_cfg_streaming.rtsp_streamer.flags & FIELD_FLAG::CHANGED) {
        auto rtsp_streamer_rw_cfg = (TFlowRTSPStreamerCfg::cfg_rtsp_streamer*)
            cmd_flds_cfg_streaming.rtsp_streamer.v.ref;

        if (app.rtsp_streamer) {
            app.rtsp_streamer->onConfigValidate(j_out_params, rtsp_streamer_rw_cfg);
            app.rtsp_streamer->onConfig(j_out_params);
        }
    }

    // Composes all required config params and clears changed flag.
    // Also advance config ID on configuration change.
    // If previous config_id doesn't match with one receive in the command, then
    // collect _all_ controls.
    // TODO: Collect UI exposed controls only?
    collectRequestedChangesTop(rw_flds, j_in_params, j_out_params);

    return 0;
}

void TFlowCtrlVStream::getRecordingUISignResponse(json11::Json::object &j_out_params)
{
    getSignResponse(j_out_params);

    const tflow_cmd_t *cmd_config = &ctrl_vstream_rpc_cmds[TFLOW_VSTREAM_RPC_CMD_RECORDING_CFG];

    Json::array j_resp_controls_arr;
    collectCtrls(cmd_config->fields, j_resp_controls_arr);
    j_out_params.emplace("controls", j_resp_controls_arr);
}

int TFlowCtrlVStream::cmd_cb_recording_version(const json11::Json& j_in_params, Json::object& j_out_params)
{
    j_out_params.emplace("version", "v0");
    return 0;
}

int TFlowCtrlVStream::cmd_cb_recording_ui_sign(const json11::Json& j_in_params, Json::object& j_out_params)
{
    g_info("Recording UI Sign command\n");

    getRecordingUISignResponse(j_out_params);

    return 0;
}

int TFlowCtrlVStream::cmd_cb_recording_set_as_def(const json11::Json& j_in_params, Json::object& j_out_params)
{
    return 0;
}

int TFlowCtrlVStream::cmd_cb_recording_config(const json11::Json& j_in_params, Json::object& j_out_params)
{
    tflow_cmd_field_t* rw_flds = (tflow_cmd_field_t*)&cmd_flds_cfg_recording;

    g_info("Recording config command: %s", j_in_params.dump().c_str());

    // Fill config fields with values from Json input object
    int was_changed = 0;
    int rc = setCmdFields(rw_flds, j_in_params, was_changed);

    if ( rc != 0 ) {
        j_out_params.emplace("error", "Can't parse");
        return rc;
        // TODO: Add notice or error to out_params in case of error.
        //       We can't just return from here, because some parameters
        //       might be already changed and we don't have rollback functionality.
    }

    //std::string indent("|");
    //dumpFieldFlags(flds, indent);

    if (cmd_flds_cfg_recording.src.flags & FIELD_FLAG::CHANGED ||
        cmd_flds_cfg_recording.en.flags & FIELD_FLAG::CHANGED) {
        app.setRecordingSrc(
            cmd_flds_cfg_recording.src.v.num, cmd_flds_cfg_recording.en.v.num);
    }

    // Composes all required config params and clears changed flag.
    // Also advance config ID on configuration change.
    // If previous config_id doesn't match with one receive in the command, then
    // collect _all_ controls.
    // TODO: Collect UI exposed controls only?
    collectRequestedChangesTop(rw_flds, j_in_params, j_out_params);

    return 0;
}


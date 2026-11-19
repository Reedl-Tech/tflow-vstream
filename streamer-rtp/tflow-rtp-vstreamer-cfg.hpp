#pragma once

#include "../tflow-ctrl.hpp"
#include "../encoder-v4l2/tflow-v4l2enc-cfg.hpp"

class TFlowRTPStreamerUI : private TFlowCtrlUI {

public:

    // TODO: add grep mask for IP adress
    struct uictrl ui_edit_remote_addr = {
        // 192.168.123.123:12345
        .label = "Destination addr <addr:port>",
        .type = TFlowCtrlUI::UICTRL_TYPE::EDIT,
        .size = 21
    };

    struct uictrl ui_edit_local_addr = {
        .label = "Bind to <addr:port>",
        .type = TFlowCtrlUI::UICTRL_TYPE::EDIT,
        .size = 21
    };

};

class TFlowRTPStreamerCfg : private TFlowRTPStreamerUI, private TFlowEncCfg {
public:
    TFlowEncCfg v4l2_enc;

    struct cfg_rtp_streamer {
        TFlowCtrl::tflow_cmd_field_t   head;
        TFlowCtrl::tflow_cmd_field_t   udp_local_addr;
        TFlowCtrl::tflow_cmd_field_t   udp_remote_addr;
        TFlowCtrl::tflow_cmd_field_t   v4l2_enc;
        TFlowCtrl::tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_rtp_streamer = {
        TFLOW_CMD_HEAD("UDP Streamer"),
        .udp_local_addr  = { "local_addr",   TFlowCtrl::CFT_STR, 0, {.str = strdup("0.0.0.0:21040")}, &ui_edit_local_addr},
        .udp_remote_addr = { "remote_addr",  TFlowCtrl::CFT_STR, 0, {.str = strdup("192.168.2.10:21040")}, &ui_edit_remote_addr},
        .v4l2_enc        = { "rtp_v4l2_enc", TFlowCtrl::CFT_REF, 0, {.ref = &v4l2_enc.cmd_flds_cfg_v4l2_enc.head}, &ui_group_def},
        TFLOW_CMD_EOMSG
    };
};

extern TFlowRTPStreamerCfg tflow_rtp_streamer_cfg;

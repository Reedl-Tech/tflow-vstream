#pragma once

#include "tflow-ctrl.hpp"
#include "encoder-v4l2/tflow-v4l2enc-cfg.hpp"

class TFlowRTPStreamerUI : private TFlowCtrlUI {

public:

    enum VIDEO_SRC {
        VIDEO_SRC_DIS   = 0,
        VIDEO_SRC_CAM0  = 1,
        VIDEO_SRC_CAM1  = 2,
        VIDEO_SRC_CAM01 = 3,
        VIDEO_SRC_PROC  = 4,
        VIDEO_SRC_LAST  = 5,
        VIDEO_SRC_NUM   = VIDEO_SRC_LAST + 1
    };

    const char *rtp_video_src_entries[VIDEO_SRC_NUM] = {
        [VIDEO_SRC_DIS  ] = "Disabled",
        [VIDEO_SRC_CAM0 ] = "CAM 0",
        [VIDEO_SRC_CAM1 ] = "CAM 1",
        [VIDEO_SRC_CAM01] = "CAM 0+1",
        [VIDEO_SRC_PROC ] = "Process",
        [VIDEO_SRC_LAST ] = nullptr 
    };

    TFlowCtrlUI::uictrl ui_rtp_video_src = {
        .label = "Video source",
        .label_pos = 0,
        .type = TFlowCtrlUI::UICTRL_TYPE::DROPDOWN,
        .size = 6,
        .dropdown = {.val = (const char **)&rtp_video_src_entries}
    };

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

class TFlowEncCfg_RTP : private TFlowEncCfg {
public:
    struct cfg_v4l2_enc cmd_flds_cfg_v4l2_enc = {
        TFLOW_CMD_HEAD("RTP v4l2 encoder"),
        .codec         = { "rtp_codec",        TFlowCtrl::CFT_NUM,  0, {.num = 0},               &ui_dd_enc_codec},
        .profile       = { "rtp_profile",      TFlowCtrl::CFT_NUM,  0, {.num = 0},               &ui_dd_hevc_profile},
        .qp            = { "rtp_qp",           TFlowCtrl::CFT_VNUM, 0, {.vnum = &hevc_qp_value}, &ui_sl2_hevc_qp },
        .qp_i          = { "rtp_qp_i",         TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_sl_qp },       // Lower better quality
        .qp_p          = { "rtp_qp_p",         TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_sl_qp },
        .gop_size      = { "rtp_gop_size",     TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_edit_def },
        .bitrate_mode  = { "rtp_bitrate_mode", TFlowCtrl::CFT_NUM,  0, {.num =  0},              &ui_dd_bitrate_mode },
        .bitrate       = { "rtp_bitrate",      TFlowCtrl::CFT_NUM,  0, {.num = 0x200000},        &ui_edit_def },
        TFLOW_CMD_EOMSG
    };
};

class TFlowRTPStreamerCfg : private TFlowRTPStreamerUI, private TFlowEncCfg_RTP {
public:

    struct cfg_rtp_streamer {
        TFlowCtrl::tflow_cmd_field_t   head;
        TFlowCtrl::tflow_cmd_field_t   src;
        TFlowCtrl::tflow_cmd_field_t   udp_local_addr;
        TFlowCtrl::tflow_cmd_field_t   udp_remote_addr;
        TFlowCtrl::tflow_cmd_field_t   v4l2_enc;
        TFlowCtrl::tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_rtp_streamer = {
        TFLOW_CMD_HEAD("RTP Streamer"),
        .src             = { "rtp_src",         TFlowCtrl::CFT_NUM, 0, {.num = VIDEO_SRC_PROC }, &ui_rtp_video_src },
        .udp_local_addr  = { "rtp_local_addr",  TFlowCtrl::CFT_STR, 0, {.str = strdup("0.0.0.0:21040")}, &ui_edit_local_addr},
        .udp_remote_addr = { "rtp_remote_addr", TFlowCtrl::CFT_STR, 0, {.str = strdup("192.168.2.10:21040")}, &ui_edit_remote_addr},
        .v4l2_enc        = { "rtp_v4l2_enc",    TFlowCtrl::CFT_REF, 0, {.ref = &cmd_flds_cfg_v4l2_enc.head}, &ui_group_def},
        TFLOW_CMD_EOMSG
    };

};

extern TFlowRTPStreamerCfg tflow_rtp_streamer_cfg;

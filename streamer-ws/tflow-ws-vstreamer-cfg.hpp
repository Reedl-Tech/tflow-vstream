#pragma once

#include "tflow-ctrl.hpp"
#include "encoder-v4l2/tflow-v4l2enc-cfg.hpp"

class TFlowWSStreamerUI : private TFlowCtrlUI {

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

    const char *ws_video_src_entries[VIDEO_SRC_NUM] = {
        [VIDEO_SRC_DIS  ] = "Disabled",
        [VIDEO_SRC_CAM0 ] = "CAM 0",
        [VIDEO_SRC_CAM1 ] = "CAM 1",
        [VIDEO_SRC_CAM01] = "CAM 0+1",
        [VIDEO_SRC_PROC ] = "Process",
        [VIDEO_SRC_LAST ] = nullptr 
    };

    TFlowCtrlUI::uictrl ui_ws_video_src = {
        .label = "Video source",
        .label_pos = 0,
        .type = TFlowCtrlUI::UICTRL_TYPE::DROPDOWN,
        .size = 6,
        .dropdown = {.val = (const char **)&ws_video_src_entries}
    };

    // TODO: add grep mask for IP adress
    //struct uictrl ui_edit_xxx = {
    //    .label = "xxxx",
    //    .type = TFlowCtrlUI::UICTRL_TYPE::EDIT,
    //    .size = xxx;
    //};
    int a;
};

class TFlowEncCfg_WS : private TFlowEncCfg {
public:
    struct cfg_v4l2_enc cmd_flds_cfg_v4l2_enc = {     // Att: Hides the child's config
        TFLOW_CMD_HEAD("WS v4l2 encoder"),
        .codec         = { "ws_codec",        TFlowCtrl::CFT_NUM,  0, {.num = 0},               &ui_dd_enc_codec},
        .profile       = { "ws_profile",      TFlowCtrl::CFT_NUM,  0, {.num = 0},               &ui_dd_hevc_profile},
        .qp            = { "ws_qp",           TFlowCtrl::CFT_VNUM, 0, {.vnum = &hevc_qp_value}, &ui_sl2_hevc_qp },
        .qp_i          = { "ws_qp_i",         TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_sl_qp },       // Lower better quality
        .qp_p          = { "ws_qp_p",         TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_sl_qp },
        .gop_size      = { "ws_gop_size",     TFlowCtrl::CFT_NUM,  0, {.num = 30},              &ui_edit_def },
        .bitrate_mode  = { "ws_bitrate_mode", TFlowCtrl::CFT_NUM,  0, {.num =  0},              &ui_dd_bitrate_mode },
        .bitrate       = { "ws_bitrate",      TFlowCtrl::CFT_NUM,  0, {.num = 0x200000},        &ui_edit_def },
        TFLOW_CMD_EOMSG
    };
};

class TFlowWSStreamerCfg : private TFlowWSStreamerUI, private TFlowEncCfg_WS {
public:

    struct cfg_ws_streamer {
        TFlowCtrl::tflow_cmd_field_t   head;
        TFlowCtrl::tflow_cmd_field_t   src;
        TFlowCtrl::tflow_cmd_field_t   v4l2_enc;
        TFlowCtrl::tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_ws_streamer = {
        TFLOW_CMD_HEAD("WS Streamer"),
        .src      = { "ws_src",      TFlowCtrl::CFT_NUM, 0, {.num = VIDEO_SRC_PROC }, &ui_ws_video_src },
        .v4l2_enc = { "ws_v4l2_enc", TFlowCtrl::CFT_REF, 0, {.ref = &cmd_flds_cfg_v4l2_enc.head}, &ui_group_def},
        TFLOW_CMD_EOMSG
    };
};

extern TFlowWSStreamerCfg tflow_ws_streamer_cfg;

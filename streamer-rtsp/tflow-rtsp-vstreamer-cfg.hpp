#pragma once

#include "../tflow-ctrl.hpp"
#include "../encoder-v4l2/tflow-v4l2enc-cfg.hpp"

class TFlowRTSPStreamerUI  {

public:
    enum ENC_TYPE {
        ENC_TYPE_H265 = 0,
        ENC_TYPE_H264 = 1,
        ENC_TYPE_LAST = 2,
        ENC_TYPE_NUM = ENC_TYPE_LAST + 1
    };

    const char* encoder_type_entries[ENC_TYPE_NUM] = {
        [ENC_TYPE_H265] = "H265",
        [ENC_TYPE_H264] = "H264",
        [ENC_TYPE_LAST] = nullptr
    };

    TFlowCtrlUI::uictrl ui_encoder_type = {
        .label = "Encoder type",
        .label_pos = 0,
        .type = TFlowCtrlUI::UICTRL_TYPE::DROPDOWN,
        .size = 6,
        .dropdown = {.val = (const char**)&encoder_type_entries}
    };

    std::vector<int> crop_h_value = { 10, 406 };      // 406 = 800 - 384 - 10 
    struct TFlowCtrlUI::uictrl ui_sl2_crop_h = {
        .label = "CROP H",
        .type = TFlowCtrlUI::SLIDER2,
        .size = -1,
        .slider = {.min = 0, .max = 1920}
    };

    std::vector<int> crop_v_value = { 10, 302 };      // 302 = 600 - 288 - 10 
    struct TFlowCtrlUI::uictrl ui_sl2_crop_v = {
        .label = "CROP V",
        .type = TFlowCtrlUI::SLIDER2,
        .size = -1,
        .slider = {.min = 0, .max = 1080}
    };

};

class TFlowRTSPStreamerCfg : private TFlowRTSPStreamerUI, private TFlowCtrl {
public:

    struct cfg_rtsp_streamer {
        TFlowCtrl::tflow_cmd_field_t   head;
        TFlowCtrl::tflow_cmd_field_t   enc_type;        // H264 or H265
        TFlowCtrl::tflow_cmd_field_t   port;
        TFlowCtrl::tflow_cmd_field_t   mount_name;
        TFlowCtrl::tflow_cmd_field_t   fps;
        TFlowCtrl::tflow_cmd_field_t   crop_en;
        TFlowCtrl::tflow_cmd_field_t   crop_h;
        TFlowCtrl::tflow_cmd_field_t   crop_v;
        TFlowCtrl::tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_rtsp_streamer = {
        TFLOW_CMD_HEAD("RTSP Streamer"),
        .enc_type   = { "enc_type",    CFT_NUM,  0, { .num  = ENC_TYPE_H264 }, &ui_encoder_type },
        .port       = { "port",        CFT_NUM,  0, { .num = 30554 }, &ui_edit_def},
        .mount_name = { "mount_name",  CFT_STR,  0, { .str = strdup("my_str_name") }, &ui_edit_def},
        .fps        = { "fps",         CFT_NUM,  0, { .num  = 25            }, &ui_edit_def     },
        .crop_en    = { "crop_en",     CFT_NUM,  0, { .num = 0              }, &ui_switch_def   },
        .crop_h     = { "crop_h",      CFT_VNUM, 0, { .vnum = &crop_h_value }, &ui_sl2_crop_h   },
        .crop_v     = { "crop_v",      CFT_VNUM, 0, { .vnum = &crop_v_value }, &ui_sl2_crop_v   },
        TFLOW_CMD_EOMSG
    };
};

extern TFlowRTSPStreamerCfg tflow_rtsp_streamer_cfg;

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

};

class TFlowRTSPStreamerCfg : private TFlowRTSPStreamerUI, private TFlowCtrl {
public:

    struct cfg_rtsp_streamer {
        TFlowCtrl::tflow_cmd_field_t   head;
        TFlowCtrl::tflow_cmd_field_t   enc_type;        // H264 or H265
        TFlowCtrl::tflow_cmd_field_t   fps;
        TFlowCtrl::tflow_cmd_field_t   eomsg;
    } cmd_flds_cfg_rtsp_streamer = {
        TFLOW_CMD_HEAD("RTSP Streamer"),
        .enc_type = { "enc_type", CFT_NUM, 0, { .num = ENC_TYPE_H264 }, &ui_encoder_type },
        .fps      = { "fps",      CFT_NUM, 0, { .num = 25 }, &ui_edit_def},
        TFLOW_CMD_EOMSG
    };
};

extern TFlowRTSPStreamerCfg tflow_rtsp_streamer_cfg;

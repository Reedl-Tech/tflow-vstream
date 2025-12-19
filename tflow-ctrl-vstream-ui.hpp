#pragma once
#include "tflow-ctrl.hpp"

class TFlowCtrlVStreamUI  {
private:

public:

    // TODO: add UDP/RTP streaming
    enum STREAMING_TYPE {
        STREAMING_TYPE_DIS  = 0,
        STREAMING_TYPE_WS   = 1,
        STREAMING_TYPE_RTSP = 2,
        STREAMING_TYPE_LAST = 3,
        STREAMING_TYPE_NUM = STREAMING_TYPE_LAST + 1
    };

    const char* streaming_type_entries[STREAMING_TYPE_NUM] = {
        [STREAMING_TYPE_DIS ] = "Disabled",
        [STREAMING_TYPE_WS  ] = "WebSocket",
        [STREAMING_TYPE_RTSP] = "RTSP",
        [STREAMING_TYPE_LAST] = nullptr
    };

    TFlowCtrlUI::uictrl ui_streaming_type = {
        .label = "Streaming type",
        .label_pos = 0,
        .type = TFlowCtrlUI::UICTRL_TYPE::DROPDOWN,
        .size = 10,
        .dropdown = {.val = (const char**)&streaming_type_entries}
    };

    /**************************************/

    enum VIDEO_SRC {
        VIDEO_SRC_CAM0  = 0,
        VIDEO_SRC_CAM1  = 1,
        VIDEO_SRC_PROC  = 2,
        VIDEO_SRC_LAST  = 3,
        VIDEO_SRC_NUM   = VIDEO_SRC_LAST + 1
    };

    const char *video_src_entries[VIDEO_SRC_NUM] = {
        [VIDEO_SRC_CAM0] = "CAM0",
        [VIDEO_SRC_CAM1] = "CAM1",
        [VIDEO_SRC_PROC] = "Process",
        [VIDEO_SRC_LAST] = nullptr 
    };

    TFlowCtrlUI::uictrl ui_video_src = {
        .label = "Video source",
        .label_pos = 0,
        .type = TFlowCtrlUI::UICTRL_TYPE::DROPDOWN,
        .dropdown = {.val = (const char **)&video_src_entries}
    };

    TFlowCtrlUI::uictrl ui_sw_en = {
        .label = "EN",
        .label_pos = 0,     // UP
        .type = TFlowCtrlUI::UICTRL_TYPE::SWITCH
    };

};



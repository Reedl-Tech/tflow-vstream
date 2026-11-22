#pragma once
#include "cstdint"

#ifndef OPENCV_CORE_HAL_INTERFACE_H
#define CV_CN_MAX     512
#define CV_CN_SHIFT   3
#define CV_8UC1 (((0)& ((1 << 3) - 1)) + (((1) - 1) << 3))
#define CV_8UC3 (((0)& ((1 << 3) - 1)) + (((3) - 1) << 3))
#endif

#define CLEAR(x) memset(&(x), 0, sizeof(x))

class Flag {
public:
    enum states {
        UNDEF,
        CLR,
        SET,
        FALL,
        RISE
    };
    enum states v = Flag::UNDEF;
};

#define DEG2RAD(deg) ((deg) * M_PI / 180)
#define RAD2DEG(rad) ((rad) / M_PI * 180)
#define RAD_NORM(rad) ( \
((rad) >  M_PI) ? (rad) - 2 * M_PI : \
((rad) < -M_PI) ? 2 * M_PI + (rad) : \
 (rad) )

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) \
  ((sizeof(a) / sizeof(*(a))) / \
  static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
#endif

#define PRESC(_x) static int presc##__COUNTER__##__func__ = 0;\
    if (0 == (presc##__COUNTER__##__func__++ & _x))

struct frame_info {
    uint32_t width;
    uint32_t height;
    uint32_t format;    // 4c V4L2_PIX_FMT_NV12
    int      pix_width; // Mat() channel nums
};

extern uint32_t fmt_frame2mat(uint32_t v4l2_4c_fmt);
extern int fmt_width(uint32_t v4l2_4c_fmt);

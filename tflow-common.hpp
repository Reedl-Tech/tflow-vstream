#pragma once

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

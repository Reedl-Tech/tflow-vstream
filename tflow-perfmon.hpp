#pragma once

#include <time.h>
#include <vector>

template<class T>
class TFlowMovAvg
{
public:
    TFlowMovAvg(int n) {
        buf = std::vector<T>(n, 0);
        it_buf = buf.begin();
    }
    T acc;
    std::vector<T> buf;
    std::vector<T>::iterator it_buf;
private:

};

template <class T>
TFlowMovAvg<T>& operator<<(TFlowMovAvg<T>& m, T s_new)
{
    T s_last = *(m.it_buf);
    m.acc += s_new;
    m.acc -= s_last;
    *m.it_buf = s_new;

    m.it_buf++;
    if (m.it_buf == m.buf.end()) m.it_buf = m.buf.begin();

    return m;
}

template <class T>
TFlowMovAvg<T>& operator>>(TFlowMovAvg<T>& m, double &avg)
{
    avg = (double)m.acc / m.buf.size();
    return m;
}

class TFlowPerfMon
{

public:

    TFlowPerfMon();

    void tickStart();
    void tickStop();

    static struct timespec diff_timespec(const struct timespec* time1, const struct timespec* time0);
    static double diff_timespec_msec(const struct timespec* time1, const struct timespec* time0);


private:
    clock_t clock_start;
    clock_t clock_end;
    struct timespec wall_time_tp;
    struct timespec wall_time_prev_tp;
    double dt_wall_time_ms;

    TFlowMovAvg<clock_t> avg_load { 8 };
    TFlowMovAvg<double> avg_fps { 8 };

};

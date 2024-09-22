#include <assert.h>

#include "tflow-perfmon.hpp"

using namespace std;

struct timespec TFlowPerfMon::diff_timespec(
    const struct timespec* time1,
    const struct timespec* time0)
{
    assert(time1);
    assert(time0);
    struct timespec diff = { .tv_sec = time1->tv_sec - time0->tv_sec, //
        .tv_nsec = time1->tv_nsec - time0->tv_nsec };
    if (diff.tv_nsec < 0) {
        diff.tv_nsec += 1000000000; // nsec/sec
        diff.tv_sec--;
    }
    return diff;
}

double TFlowPerfMon::diff_timespec_msec(
    const struct timespec* time1,
    const struct timespec* time0)
{
    struct timespec d_tp = diff_timespec(time1, time0);
    return d_tp.tv_sec * 1000 + (double)d_tp.tv_nsec / (1000 * 1000);
}
TFlowPerfMon::TFlowPerfMon()
{
    clock_gettime(CLOCK_MONOTONIC, &wall_time_prev_tp);
}

void TFlowPerfMon::tickStart()
{
    clock_start = clock();

    clock_gettime(CLOCK_MONOTONIC, &wall_time_tp);
    double dt_wall_time_ms = diff_timespec_msec(&wall_time_tp, &wall_time_prev_tp);

    avg_fps << (1000 / dt_wall_time_ms);
}

void TFlowPerfMon::tickStop()
{
    clock_t load_sample = clock() - clock_start;
    clock_t load_sample_ms = load_sample / (CLOCKS_PER_SEC / 1000);
    avg_load << load_sample_ms;
}

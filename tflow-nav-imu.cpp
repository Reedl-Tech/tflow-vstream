#include <cassert>
#include <cstring>

#include <json11.hpp>

#include "tflow-build-cfg.hpp"
#include "tflow-nav-imu.hpp"

using namespace json11;

using namespace std;

void TFlowImu::getIMU(uint8_t* aux_data, uint32_t aux_data_len)
{
    uint32_t sign = *(uint32_t*)aux_data;

    if (aux_data_len == 0) {
        // Mark IMU as invalid
        is_valid = false;
        return;
    } else if (sign == 0x32554D49) {  // RT.2 contains IMU v2 from TFlow dedicated packet
    TFlowImu::ap_imu_v2 *imu_in = (TFlowImu::ap_imu_v2*)aux_data;
        assert(aux_data_len == sizeof(TFlowImu::ap_imu_v2));
        getIMU_v2(imu_in);
        is_valid = true;
    } else if (sign == 0x33554D49) {  // RT.2 contains IMU v3 from TFlow dedicated packet
        TFlowImu::ap_imu_v3 *imu_in = (TFlowImu::ap_imu_v3*)aux_data;
        assert(aux_data_len == sizeof(TFlowImu::ap_imu_v3));
        getIMU_v3(imu_in);
        is_valid = true;
    }
    else {
        is_valid = false;
    }

}

void TFlowImu::getIMU_v2(const TFlowImu::ap_imu_v2* imu_in)
{
    assert(imu_in->sign == 0x32554D49);              // IMU2 => IMU_v2

//    memset(&imu, 0, sizeof(imu));

    if (ts_sec_init == 0) ts_sec_init = imu_in->tv_sec;
    ts_sec_epoch = imu_in->tv_sec;

    ts_usec = (imu_in->tv_sec - ts_sec_init) * 1000000 + imu_in->tv_usec;

    roll                 = (double)imu_in->board_attitude_roll  / 100;
    pitch                = (double)imu_in->board_attitude_pitch / 100;
    heading              = (double)imu_in->board_attitude_yaw   / 100;
    altitude_baro        = (double)imu_in->curr_pos_height      / 100;
    altitude_rangefinder = (double)imu_in->rangefinder_val_cm   / 100;

    altitude = altitude_baro;

    // Att: In fog or very wet weather the range finder's reading are fully
    //      messed. Need a more complex logic or config parameter to over
    //      overcome this effect.

    //if (altitude_rangefinder > 0) {
    //    altitude = altitude_rangefinder;
    //}

    x = (double)imu_in->position_x / 100;
    y = (double)imu_in->position_z / 100;

    yaw_raw = (double)imu_in->raw_yaw / 100;

    gps_state_prev = gps_state;

    int gps_state_in = (imu_in->hwHealthStatus >> TFlowImu::HW_SENSOR_STATUS_GPS_POS) & ((1 << TFlowImu::HW_SENSOR_STATUS_BITS) - 1);

    gps_state = TFlowImu::HW_SENSOR_STATUS(gps_state_in);

    mode = TFlowImu::IMU_MODE(imu_in->stabilization_mode);

}

void TFlowImu::getIMU_v3(const TFlowImu::ap_imu_v3* imu_in)
{
    assert(imu_in->sign == 0x33554D49);              // IMU3 => IMU_v3

//    memset(&imu, 0, sizeof(imu));

    if (ts_sec_init == 0) ts_sec_init = imu_in->tv_sec;
    ts_sec_epoch = imu_in->tv_sec;

    ts_usec = (imu_in->tv_sec - ts_sec_init) * 1000000 + imu_in->tv_usec;

    roll                 = (double)imu_in->board_attitude_roll  / 100;
    pitch                = (double)imu_in->board_attitude_pitch / 100;
    heading              = (double)imu_in->board_attitude_yaw   / 100;
    altitude_baro        = (double)imu_in->curr_pos_height      / 100;
    altitude_rangefinder = (double)imu_in->rangefinder_val_cm   / 100;

    altitude = altitude_baro;

    // Att: In fog or very wet weather the range finder's reading are fully
    //      messed. Need a more complex logic or config parameter to over
    //      overcome this effect.

    //if (altitude_rangefinder > 0) {
    //    altitude = altitude_rangefinder;
    //}

    x = (double)imu_in->position_x / 100;
    y = (double)imu_in->position_z / 100;

    yaw_raw = (double)imu_in->raw_yaw / 100;

    gps_state_prev = gps_state;

    int gps_state_in = (imu_in->hwHealthStatus >> TFlowImu::HW_SENSOR_STATUS_GPS_POS) & ((1 << TFlowImu::HW_SENSOR_STATUS_BITS) - 1);

    gps_state = TFlowImu::HW_SENSOR_STATUS(gps_state_in);

    mode = TFlowImu::IMU_MODE(imu_in->stabilization_mode);

}

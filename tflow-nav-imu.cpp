#include <cassert>
#include <cstring>

#include <json11.hpp>

#include "tflow-build-cfg.hpp"
#include "tflow-nav-imu.hpp"

using namespace json11;

//using namespace std;

int TFlowImu::getIMU(TFlowImu& imu, uint8_t* aux_data, uint32_t aux_data_len)
{
    TFlowImu::ap_imu_v2 *imu_in = (TFlowImu::ap_imu_v2*)aux_data;

    if (imu_in->sign == 0x32554D49) {  // RT.2 contains IMU v2 from TFlow dedicated packet
        getIMU_v2(imu, imu_in);
        return 0;
    }
    return -1;
}

void TFlowImu::getIMU_v2(TFlowImu& imu, const TFlowImu::ap_imu_v2* imu_in)
{
    assert(imu_in->sign == 0x32554D49);              // IMU2 => IMU_v2

    TFlowImu::HW_SENSOR_STATUS gps_state_prev = imu.gps_state;

    memset(&imu, 0, sizeof(imu));
    imu.ts = imu_in->tv_sec * 1000000 + imu_in->tv_usec;    // in usec

    imu.roll                 = (double)imu_in->board_attitude_roll  / 100;
    imu.pitch                = (double)imu_in->board_attitude_pitch / 100;
    imu.heading              = (double)imu_in->board_attitude_yaw   / 100;
    imu.altitude_baro        = (double)imu_in->curr_pos_height      / 100;
    imu.altitude_rangefinder = (double)imu_in->rangefinder_val_cm   / 100;

    imu.altitude = imu.altitude_baro;
    if (imu.altitude_rangefinder > 0) {
        imu.altitude = imu.altitude_rangefinder;
    }

    imu.x = (double)imu_in->position_x / 100;
    imu.y = (double)imu_in->position_z / 100;

    imu.yaw_raw = (double)imu_in->raw_yaw / 100;

    imu.gps_state_prev = gps_state_prev;

    int gps_state = (imu_in->hwHealthStatus >> TFlowImu::HW_SENSOR_STATUS_GPS_POS) & ((1 << TFlowImu::HW_SENSOR_STATUS_BITS) - 1);

    imu.gps_state = TFlowImu::HW_SENSOR_STATUS(gps_state);

    imu.mode = TFlowImu::IMU_MODE(imu_in->stabilization_mode);

}


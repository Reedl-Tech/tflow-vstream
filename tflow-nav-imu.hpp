#pragma once

#include <cstdint>

class TFlowImu {

public:

    TFlowImu() {
        ts = 0;
        yaw = 0;
        roll = 0;
        pitch = 0;
        heading = 0;
        x = 0;
        y = 0;
        altitude = 0;
        altitude_rangefinder = 0;
        altitude_baro = 0;
        yaw_raw = 0;

        gps_state = TFlowImu::HW_SENSOR_STATUS::NONE;
        gps_state_prev = TFlowImu::HW_SENSOR_STATUS::NONE;
    }

    enum class HW_SENSOR_STATUS {
            NONE = 0,
            OK = 1,
            UNAVAILABLE = 2,
            UNHEALTHY = 3
    };

    enum class IMU_MODE {
        COPTER = 0,
        PLANE  = 1,
        DISARMED = 255,
    };

    static constexpr int HW_SENSOR_STATUS_BITS = 2;

    static constexpr int HW_SENSOR_STATUS_GYRO_POS            =  0;
    static constexpr int HW_SENSOR_STATUS_AXEL_POS            =  2;
    static constexpr int HW_SENSOR_STATUS_COMPAS_POS          =  4;
    static constexpr int HW_SENSOR_STATUS_BARO_POS            =  6;
    static constexpr int HW_SENSOR_STATUS_GPS_POS             =  8;
    static constexpr int HW_SENSOR_STATUS_RANGEFINDER_POS     = 10;
    static constexpr int HW_SENSOR_STATUS_PITOT_POS           = 12;
    static constexpr int HW_SENSOR_STATUS_POSEST_POS          = 14;
    static constexpr int HW_SENSOR_STATUS_GPS_YAW_POS         = 16;
    static constexpr int HW_SENSOR_STATUS_IMU_MPU_POS         = 18;
    static constexpr int HW_SENSOR_STATUS_IMU_MTI_POS         = 20;

    static const char* hw_health_str(HW_SENSOR_STATUS s) {
        return 
            (s == HW_SENSOR_STATUS::NONE)        ? "NONE" :
            (s == HW_SENSOR_STATUS::OK)          ? "OK" :
            (s == HW_SENSOR_STATUS::UNAVAILABLE) ? "UNAVAILABLE" :
            (s == HW_SENSOR_STATUS::UNHEALTHY)   ? "UNHEALTHY" : 
            "unknown";
    }
#pragma pack(push,1)

    struct ap_imu_v2 {
        uint32_t sign;
        uint32_t tv_sec;      // Local timestamp
        uint32_t tv_usec;     // Local timestamp
        uint32_t hwHealthStatus;
        int16_t  rangefinder_val_cm;
        uint8_t  rangefinder_type;
        uint8_t  stabilization_mode;        // 0 - Copter, 1 - Plane, 255 - Disarmed
        int32_t  board_attitude_roll;
        int32_t  board_attitude_yaw;
        int32_t  board_attitude_pitch;
        int32_t  uav_attitude_roll;
        int32_t  uav_attitude_yaw;
        int32_t  uav_attitude_pitch;
        int32_t  pe_baro_alt;
        int32_t  curr_pos_height;
        int32_t  position_x;
        int32_t  position_y;
        int32_t  position_z;
        int32_t  raw_yaw;
    };

#pragma pack(pop)

    uint32_t ts;    //
    double yaw;     // 'CAS_board_att_yaw'
    double roll;    // 'CAS_board_att_roll' 
    double pitch;   // 'CAS_board_att_pitch'
    double heading; // 'cur_pos_az'
    double x;       // 'cur_pos_Xg'
    double y;       // 'cur_pos_Zg'
    double altitude;// 'cur_pos_Yg'
    double altitude_rangefinder;    
    double altitude_baro;    

    double yaw_raw;

    HW_SENSOR_STATUS gps_state_prev;
    HW_SENSOR_STATUS gps_state;

    IMU_MODE mode;

    static int getIMU(TFlowImu& imu, uint8_t* aux_data, uint32_t aux_data_len);

private:
    static void getIMU_v2(TFlowImu& imu, const TFlowImu::ap_imu_v2* imu_in);

};



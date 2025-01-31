#pragma once

#include <glib-unix.h>

#define TFLOWBUF_MSG_SIGN_ID 0x11
#define TFLOWBUF_MSG_PING_ID 0x12
#define TFLOWBUF_MSG_PONG_ID 0x13
#define TFLOWBUF_MSG_CAM_FD  0x21
#define TFLOWBUF_MSG_CONSUME 0x31
#define TFLOWBUF_MSG_REDEEM  0x32

/* Class shared between client and server */
class TFlowBuf {
public:
    TFlowBuf();
    TFlowBuf(int cam_fd, int index, int planes_num);
    ~TFlowBuf();

    /* Parameters passed from server */
    int index = -1;
    struct timeval ts = { 0 };
    uint32_t sequence;

    /* Parameters obtained from Kernel*/
    void* start = 0;
    size_t length = 0;
    uint32_t owners = 0;   // Bit mask of TFlowBufCli. Bit 0 - means buffer is in user space

    int age();

    /* Q: ? Should the CLI-SRV communication definition to be split from
     *      the buffer class ?
     */

#pragma pack(push,1)

    struct pck_hdr {
        int id;
        int seq;
    };

    struct pck_sign {
        struct pck_hdr hdr;
        char cli_name[32];
        int  cli_pid;
    };

    struct pck_ping {
        struct pck_hdr hdr;
        char cli_name[32];
        int  cnt;
    };

    struct pck_buff_info {
        struct pck_hdr hdr;
    };

    struct pck_buff_query {
        struct pck_hdr hdr;
    };

    struct pck_cam_fd {
        struct pck_hdr hdr;
        int buffs_num;
        int planes_num;
        uint32_t width;
        uint32_t height;
        uint32_t format;
    };

    struct pck_consume {
        struct pck_hdr hdr;
        int buff_index;
        struct timeval ts;
        uint32_t seq;
        uint32_t aux_data_len;
        uint8_t aux_data[256];     // Att: Variable length array. Must be last!
                                   //      Actual data to be sent specified in aux_data_len
    };

    struct pck_redeem {
        struct pck_hdr hdr;
        int buff_index;         // 0..buff_num for redeem, -1 for initial packet request
        int need_more;
    };

    typedef union {
        struct pck_hdr          hdr;
        struct pck_sign         sign;
        struct pck_ping         ping;
        struct pck_cam_fd       cam_fd;
        struct pck_consume      consume;
        struct pck_redeem       redeem;
        struct pck_buff_info    buff_info;
        struct pck_buff_query   buff_query;
    } pck_t;

#pragma pack(pop)

};


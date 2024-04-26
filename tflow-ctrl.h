#pragma once 

#include <stdint.h>
#include <string.h>
#include <vector>

#include <json11.hpp>
using namespace json11;

#include <glib-unix.h>

#define TFLOW_CMD_EOMSG .eomsg = {.name = nullptr, .type = CFT_LAST, .max_len = 0, .v = {.num = 0} }

class TFlowCtrl {
public:

    TFlowCtrl();

    typedef enum {
        CFT_NUM,
        CFT_STR,
        CFT_DBL,
        CFT_RESERVED = 4,
        CFT_LAST = -1
    } tflow_cmd_field_type_t;

    typedef struct {
        const char* name;
        tflow_cmd_field_type_t type;

        int max_len;
        union {
            int    num;
            char* str;
            double dbl;
        } v;
    } tflow_cmd_field_t;

    int set_cmd_fields(tflow_cmd_field_t* cmd_field, const Json& in_params);
private:
    int set_field(tflow_cmd_field_t* cmd_field, const Json& in_param);
};


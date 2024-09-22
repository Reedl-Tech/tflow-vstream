#include <sys/stat.h>
#include <glib-unix.h>

#include <json11.hpp>
#include "tflow-ctrl-vstream.h"

using namespace json11;

static const char *raw_cfg_default =  R"( 
    {"vstreamer_param_1" : "xz"} 
)";

//static int _cmd_cb_sign      (TFlowCtrlVStream* obj, Json& json) { return obj->cmd_cb_sign(json);       }
//static int _cmd_cb_config    (TFlowCtrlVStream* obj, Json& json) { return obj->cmd_cb_config(json);     }
//static int _cmd_cb_set_as_def(TFlowCtrlVStream* obj, Json& json) { return obj->cmd_cb_set_as_def(json); }

TFlowCtrlVStream::TFlowCtrlVStream(TFlowVStream& parent) :
    app(parent)
{
    InitServer();
}

void TFlowCtrlVStream::InitServer()
{
}

void TFlowCtrlVStream::InitConfig()
{
    struct stat sb;
    int cfg_fd = -1;
    bool use_default_cfg = 0;
    Json json_cfg;
    
    cfg_fd = open(cfg_fname, O_RDWR);

    if (fstat(cfg_fd, &sb) < 0) {
        g_warning("Can't open configuration file %s", cfg_fname);
        use_default_cfg = true;
    }
    else if (!S_ISREG(sb.st_mode)) {
        g_warning("Config name isn't a file %s", cfg_fname);
        use_default_cfg = true;
    }

    if (!use_default_cfg) {
        char* raw_cfg = (char*)g_malloc(sb.st_size);
        int bytes_read = read(cfg_fd, raw_cfg, sb.st_size);
        if (bytes_read != sb.st_size) {
            g_warning("Can't read config file %s", cfg_fname);
            use_default_cfg = true;
        }

        if (!use_default_cfg) {
            std::string err;
            json_cfg = Json::parse(raw_cfg, err);
            if (json_cfg.is_null()) {
                g_warning("Error in JSON format - %s\n%s", (char*)err.data(), raw_cfg);
                use_default_cfg = true;
            }
        }
        free(raw_cfg);
        close(cfg_fd);
    }

    if (use_default_cfg) {
        std::string err;
        json_cfg = Json::parse(raw_cfg_default, err);
    }

    set_cmd_fields((tflow_cmd_field_t*)&cmd_flds_config, json_cfg);
}

int TFlowCtrlVStream::vstreamer_param_1_get()
{
    return (int)cmd_flds_config.vstreamer_param_1.v.num;
}

int TFlowCtrlVStream::state_get()
{
    return (int)cmd_flds_config.state.v.num;
}

/*********************************/
/*** Application specific part ***/
/*********************************/
int TFlowCtrlVStream::cmd_cb_version(const json11::Json& j_in_params, Json::object& j_out_params)
{
    return 0;
}

int TFlowCtrlVStream::cmd_cb_set_as_def(const json11::Json& j_in_params, Json::object& j_out_params)
{
    return 0;
}

int TFlowCtrlVStream::cmd_cb_config(const json11::Json& j_in_params, Json::object& j_out_params)
{
    g_info("Config command\n    params:\t");

    int rc = set_cmd_fields((tflow_cmd_field_t*)&cmd_flds_config, j_in_params);

    if (rc != 0) return -1;

    return 0;
}



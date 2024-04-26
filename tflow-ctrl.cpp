#include "tflow-ctrl.h"

TFlowCtrl::TFlowCtrl()
{
}

int TFlowCtrl::set_cmd_fields(tflow_cmd_field_t* in_cmd_fields, const Json& in_params)
{
    // Loop over all config command fields and check json_cfg
    tflow_cmd_field_t* cmd_field = in_cmd_fields;

    while (cmd_field->name != nullptr) {

        const Json in_field_param = in_params[cmd_field->name];
        if (!in_field_param.is_null()) {
            // Configuration parameter is found in Json config
            // Check field type is valid
            if (0 != set_field(cmd_field, in_field_param)) {
                return -1;
            }
        }
        cmd_field++;
    }
    return 0;

}

int TFlowCtrl::set_field(tflow_cmd_field_t* cmd_field, const Json& cfg_param)
{
    switch (cmd_field->type) {
    case CFT_STR:
        if (!cfg_param.is_string()) return -1;
        if (cmd_field->v.str) free(cmd_field->v.str);
        cmd_field->v.str = strdup(cfg_param.string_value().data());
        return 0;
    case CFT_NUM:
        if (cfg_param.is_number()) {
            cmd_field->v.num = cfg_param.number_value();
        }
        else if (cfg_param.is_string()) {
            cmd_field->v.num = atoi(cfg_param.string_value().data());
        }
        else if (cfg_param.is_bool()) {
            cmd_field->v.num = cfg_param.bool_value();
        }
        else {
            return -1;
        }
        return 0;
    default:
        g_info("Ooops... at %s (%d) Data type mismatch. Field name: %s (%d != %d)", __FILE__, __LINE__,
            cmd_field->name, cmd_field->type, cfg_param.type());
    }

    return -1;
}

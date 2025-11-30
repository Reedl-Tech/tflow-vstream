#pragma once 
#include <cstring>
#include "tflow-glib.hpp"

#include "tflow-ctrl-srv.hpp"

class TFlowCtrlVStream;

class TFlowCtrlSrvVStream : public TFlowCtrlSrv {
public:
    
    TFlowCtrlSrvVStream(TFlowCtrlVStream &_ctrl_vstream, GMainContext* context);

    void onSignature(json11::Json::object& j_out_params, int& err) override;
    void onTFlowCtrlMsg(const std::string& cmd, const json11::Json& j_in_params, json11::Json::object& j_out_params, int& err) override;

private:
    TFlowCtrlVStream& ctrl_vstream;

};


#include <sys/socket.h>
#include <sys/un.h>

#include "tflow-ctrl-srv.h"

gboolean TFlowCtrlCliPort::tflow_ctrl_cli_port_dispatch(GSource* g_source, GSourceFunc callback, gpointer user_data)
{
    TFlowCtrlCliPort::GSourceCliPort* source = (TFlowCtrlCliPort::GSourceCliPort*)g_source;
    TFlowCtrlCliPort* cli_port = source->cli_port;

    int rc = cli_port->onMsg();
    if (rc) {
        cli_port->srv.onCliPortError(cli_port->sck_fd);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

TFlowCtrlCliPort::~TFlowCtrlCliPort()
{
    if (sck_fd != -1) {
        close(sck_fd);
    }

    if (sck_src) {
        if (sck_tag) {
            g_source_remove_unix_fd((GSource*)sck_src, sck_tag);
            sck_tag = nullptr;
        }
        g_source_destroy((GSource*)sck_src);
        g_source_unref((GSource*)sck_src);
        sck_src = nullptr;
    }

}

TFlowCtrlCliPort::TFlowCtrlCliPort(GMainContext* _context, TFlowCtrlSrv &_srv, int fd) :
    srv(_srv)
{
    sck_fd = fd;
    sck_tag = NULL;
    sck_src = NULL;

    CLEAR(sck_gsfuncs);

    /* Assign g_source on the socket */
    sck_gsfuncs.dispatch = tflow_ctrl_cli_port_dispatch;
    sck_src = (GSourceCliPort*)g_source_new(&sck_gsfuncs, sizeof(GSourceCliPort));
    sck_tag = g_source_add_unix_fd((GSource*)sck_src, sck_fd, (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP));
    sck_src->cli_port = this;
    g_source_attach((GSource*)sck_src, _context);
}
//    int TFlowCtrlCli::sendMsg(const char* cmd, json11::Json::object j_params)
int TFlowCtrlCliPort::sendResp(const char *cmd, int resp_err, const json11::Json::object& j_resp_params)
{
    ssize_t res;
//    if (sck_state_flag.v != Flag::SET) return 0;

    json11::Json j_resp;

    if (resp_err) {
        j_resp = json11::Json::object{
            { "cmd"    , cmd           },
            { "dir"    , "response"    },        // For better log readability only
            { "err"    , resp_err      }
        };
    }
    else {
        j_resp = json11::Json::object{
            { "cmd"    , cmd           },
            { "dir"    , "response"    },        // For better log readability only
            { "params" , j_resp_params }
        };
    }

    std::string s_msg = j_resp.dump();
        
    res = send(sck_fd, s_msg.c_str(), s_msg.length(), MSG_NOSIGNAL | MSG_DONTWAIT);
    int err = errno;
    if (res == -1) {
        if (err == EPIPE) {
            g_warning("TFlowCtrlCliPort: Can't send to [%s], %s (%d) - %s",
                signature.c_str(), cmd, err, strerror(err));
        }
        else {
            g_warning("TFlowCtrlCliPort: Send message error to [%s], %s (%d) - %s",
                signature.c_str(), cmd, err, strerror(err));
        }
        return -1;
    }
    g_warning("TFlowCtrlCli: [%s] ->> [%s]  %s",
        srv.my_name.c_str(),
        signature.c_str(), cmd);

    last_send_ts = clock();
    return 0;
}


int TFlowCtrlCliPort::onMsgSign(const json11::Json& j_params)
{
    int rc;

    signature = j_params["peer_signature"].string_value();
    pid = j_params["pid"].int_value();

    g_warning("TFlowCtrlCliPort: Signature for port %d - [%s]",
        sck_fd, signature.c_str());

    return 0;
}

int TFlowCtrlCliPort::onMsg()
{
    int res = recv(sck_fd, &in_msg, sizeof(in_msg)-1, 0); //MSG_DONTWAIT 
    int err = errno;

    if (res <= 0) {
        if (err == ECONNRESET || err == EAGAIN) {
            g_warning("TFlowCtrlCliPort: [%s] disconnected (%d) - closing",
                this->signature.c_str(), errno);
        }
        else {
            g_warning("TFlowCtrlCliPort: [%s] unexpected error (%d) - %s",
                this->signature.c_str(), errno, strerror(errno));
        }
        return -1;
    }
    in_msg[res] = 0;

    std::string j_err;
    const json11::Json j_in_msg = json11::Json::parse(in_msg, j_err);

    if (j_in_msg.is_null()) {
        g_warning("TFlowCtrlCliPort: [%s] Can't parse input message - %s",
            this->signature.c_str(), j_err.c_str());
    }

    const std::string in_cmd = j_in_msg["cmd"].string_value();
    const json11::Json j_in_params = j_in_msg["params"];

    /* Check Client specific commands first */
    int resp_err;
    json11::Json::object j_resp_params;

    if (in_cmd == "signature") {
        onMsgSign(j_in_params);
        srv.onSignature(j_resp_params, resp_err);
    }
    else {
        srv.onTFlowCtrlMsg(in_cmd, j_in_params, j_resp_params, resp_err);
    }

    return sendResp(in_cmd.c_str(), resp_err, j_resp_params );
}


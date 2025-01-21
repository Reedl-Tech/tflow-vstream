// TODO: check camera disconnection scenarion

#include <unistd.h>
#include <cassert>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <glib-unix.h>

#include "tflow-perfmon.hpp"
#include "tflow-vstream.h"  // is in use?
#include "tflow-buf-cli.h"

static gboolean tflow_buf_cli_on_idle_once(gpointer data);

TFlowBufCli::~TFlowBufCli()
{
    Disconnect();
    
}
int TFlowBufCli::onConsume(TFlowBuf::pck_consume* msg)
{
    // Sanity check for buffer index
    assert(msg->buff_index >= 0 && msg->buff_index < tflow_bufs.size());

    // new buffer received normally

    app->onFrame(msg->buff_index, msg->ts, msg->seq, msg->aux_data, msg->aux_data_len);

    {
        static int presc = 0;
        if ((++presc & 0xFF) == 0) g_warning("Processed %d frames", presc);
    }
    

    return 0;
}

int TFlowBufCli::onCamFD(TFlowBuf::pck_cam_fd* msg, int cam_fd)
{
    this->cam_fd = cam_fd;

    tflow_bufs.reserve(msg->buffs_num);

    for (int buf_idx = 0; buf_idx < msg->buffs_num; buf_idx++) {
        tflow_bufs.emplace_back(cam_fd, buf_idx, msg->planes_num);
    }

    return 0;
}

int TFlowBufCli::onMsg()
{
    struct msghdr   msg;
    struct iovec    iov[1];
    char buf[CMSG_SPACE(sizeof(int))];
    int cam_dev_fd;
    ssize_t res;
    int err;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    TFlowBuf::pck_t pck;

    iov[0].iov_base = (void*)&pck;
    iov[0].iov_len = sizeof(pck);

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
                         
    // Read-out all data from the socket 
    res = recvmsg(sck_fd, &msg, MSG_NOSIGNAL);

    if (res <= 0) {
        err = errno;    // AV: Is errno updated on (res < 0)?
        if (err == EPIPE || err == ECONNREFUSED || err == ENOENT || err == ECONNRESET) {
            // May happens on Server close
            g_warning("TFlowBufCli: TFlow Buffer Server closed");
        }
        else {
            g_warning("TFlowBufCli: unexpected error (%ld, %d) - %s",
                res, err, strerror(err));
        }

        sck_state_flag.v = Flag::FALL;
        g_main_context_invoke(context, (GSourceFunc)tflow_buf_cli_on_idle_once, this);
        return -1;
    }

    // Sanity
    if (iov[0].iov_len == 0) {
        g_warning("Oooops - Empty message");
        return 0;
    }

    switch (pck.hdr.id) {
    case TFLOWBUF_MSG_CAM_FD:
    {
        g_warning("---TFlowBufCli: Received - CAM_FD");
        if (msg.msg_controllen == 0) {
            g_warning("Oooops - Bad aux data");
            return -1;
        }

        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        int cam_fd = *(int*)CMSG_DATA(cmsg);
        TFlowBuf::pck_cam_fd *pck_cam_fd = (TFlowBuf::pck_cam_fd*)&pck;

        onCamFD(pck_cam_fd, cam_fd);
        app->onCamFD(pck_cam_fd);
        sendRedeem(-1); // Request a frame from TFlow Buffer Server
        break;
    }
    case TFLOWBUF_MSG_CONSUME:
    {
        TFlowBuf::pck_consume* pck_consume = (TFlowBuf::pck_consume*)&pck;

        // g_warning("---TFlowBufCli: Received - Consume %d", pck_consume->buff_index);
        onConsume(pck_consume);
        sendRedeem(pck_consume->buff_index); // Return current and request another frame from TFlow Buffer Server

        break;
    }
    case TFLOWBUF_MSG_PING_ID:
    case TFLOWBUF_MSG_PONG_ID:
    {
        break;
    }
    default:
        g_warning("Oooops - Unknown message received %d", pck.hdr.id);
    }

    return 0;
}

TFlowBufCli::TFlowBufCli(GMainContext* app_context)
{
    context = app_context;
    sck_state_flag.v = Flag::UNDEF;
    sck_tag = NULL;
    sck_src = NULL;
    CLEAR(sck_gsfuncs);

    clock_gettime(CLOCK_MONOTONIC, &last_send_ts);
    last_conn_check_ts.tv_sec = 0;
    last_conn_check_ts.tv_nsec = 0;

    buf_srv = NULL;
    msg_seq_num = 0;
    cam_fd = -1;
}

static gboolean tflow_buf_cli_on_idle_once(gpointer data)
{
    TFlowBufCli* buf_cli = (TFlowBufCli*)data;

    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    buf_cli->onIdle(now_ts);

    return G_SOURCE_REMOVE;
}

gboolean tflow_buf_cli_dispatch(GSource* g_source, GSourceFunc callback, gpointer user_data)
{
    int rc;
    TFlowBufCli::GSourceCli* source = (TFlowBufCli::GSourceCli*)g_source;
    TFlowBufCli* cli = source->cli;

    // g_debug("TFlowBufCli: Incoming message");

    rc = cli->onMsg();

    if (rc) {
        // Critical error on PIPE. 
        return G_SOURCE_REMOVE;
    }
    else {
        return G_SOURCE_CONTINUE;
    }
   
}

int TFlowBufCli::sendMsg(TFlowBuf::pck_t *msg, int msg_id)
{
    ssize_t res;
    size_t msg_len;
    const char* comment = NULL;

    if (sck_state_flag.v != Flag::SET) return 0;

    switch (msg_id) {
    case TFLOWBUF_MSG_REDEEM:
        msg_len = sizeof(msg->redeem);
        comment = "Redeem";
        break;
    case TFLOWBUF_MSG_SIGN_ID:
        msg_len = sizeof(msg->sign);
        comment = "Signature";
        break;
    case TFLOWBUF_MSG_PING_ID:
        msg_len = sizeof(msg->ping);
        comment = "Ping";
        break;
    default:
        g_warning("TFlowBufCli: Bad message ID - %d", msg_id);
        return 0;
    }

    msg->hdr.seq = msg_seq_num++;
    msg->hdr.id = msg_id;

    res = send(sck_fd, msg, msg_len, MSG_NOSIGNAL | MSG_DONTWAIT);

    clock_gettime(CLOCK_MONOTONIC, &last_send_ts);

    if (res == -1) {
        int err = errno;
        if (err == EPIPE) {
            g_warning("TFlowBufCli: Can't send");
        }
        else {
            g_warning("TFlowBufCli: Send message error on [%s] (%d) - %s",
                comment, err, strerror(err));
        }
        sck_state_flag.v = Flag::FALL;
        return -1;
    }

    return 0;
}

int TFlowBufCli::sendRedeem(int index)
{
    TFlowBuf::pck_redeem msg_redeem{};
   
    msg_redeem.buff_index = index;
    msg_redeem.need_more = true;

    sendMsg((TFlowBuf::pck_t*)&msg_redeem, TFLOWBUF_MSG_REDEEM);

    return 0;
}

int TFlowBufCli::sendPing()
{
    TFlowBuf::pck_ping msg_ping {};

    sendMsg((TFlowBuf::pck_t*)&msg_ping, TFLOWBUF_MSG_PING_ID);
    return 0;
}

int TFlowBufCli::sendSignature()
{
#define TFLOWBUFCLI_SIGNATURE "VStream"

    TFlowBuf::pck_sign msg_sign {};

    memcpy(msg_sign.cli_name, TFLOWBUFCLI_SIGNATURE, strlen(TFLOWBUFCLI_SIGNATURE));
    msg_sign.cli_pid = getpid();

    sendMsg((TFlowBuf::pck_t*)&msg_sign, TFLOWBUF_MSG_SIGN_ID);
    return 0;
}

void TFlowBufCli::Disconnect()
{
    if (sck_fd != -1) {
        close(sck_fd);
        sck_fd = -1;
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

    
    tflow_bufs.clear();
    if (cam_fd != -1) {
        close(cam_fd);
        cam_fd = -1;
    }

    return;
}

int TFlowBufCli::Connect()
{
    int rc;
    struct sockaddr_un sock_addr;
    
    // Open local UNIX socket
    sck_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (sck_fd == -1) {
        g_warning("TFlowBufCli: Can't create socket for local client (%d) - %s", errno, strerror(errno));
        return -1;
    }

    //int flags = fcntl(sck_fd, F_GETFL, 0);
    //fcntl(sck_fd, F_SETFL, flags | O_NONBLOCK);

    // Initialize socket address
    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    memcpy(sock_addr.sun_path, "\0" TFLOWBUFSRV_SOCKET_NAME, strlen(TFLOWBUFSRV_SOCKET_NAME) + 1);  // NULL termination excluded

    socklen_t sck_len = sizeof(sock_addr.sun_family) + strlen(TFLOWBUFSRV_SOCKET_NAME) + 1;
    rc = connect(sck_fd, (const struct sockaddr*)&sock_addr, sck_len);
    if (rc == -1) {
        static int presc = 0;
        if ((++presc & 0x07) == 0) {
            g_warning("TFlowBufCli: Can't connect to the server %s (%d) - %s",
                TFLOWBUFSRV_SOCKET_NAME, errno, strerror(errno));
        }

        close(sck_fd);
        sck_fd = -1;

        return -1;
    }

    g_warning("---TFlowBufCli: Connected to the server " TFLOWBUFSRV_SOCKET_NAME);

    /* Assign g_source on the socket */
    sck_gsfuncs.dispatch = tflow_buf_cli_dispatch;
    sck_src = (GSourceCli*)g_source_new(&sck_gsfuncs, sizeof(GSourceCli));
    sck_tag = g_source_add_unix_fd((GSource*)sck_src, sck_fd, (GIOCondition)(G_IO_IN /* | G_IO_ERR | G_IO_HUP */));
    sck_src->cli = this;
    g_source_attach((GSource*)sck_src, context);

    return 0;
}

void TFlowBufCli::onIdle(struct timespec now_ts)
{
    if (sck_state_flag.v == Flag::CLR) {
        if (TFlowPerfMon::diff_timespec_msec(&now_ts, &last_conn_check_ts) > 1000) {
            last_conn_check_ts = now_ts;
            sck_state_flag.v = Flag::RISE;
        }
        return;
    }

    if (sck_state_flag.v == Flag::SET) {
        // Check idle connection. VStream is active, but camera is not.
        if (TFlowPerfMon::diff_timespec_msec(&now_ts, &last_send_ts) > 1000) {
            sendPing();
        }
        return;
    }

    if (sck_state_flag.v == Flag::UNDEF || sck_state_flag.v == Flag::RISE) {
        int rc;

        rc = Connect();
        if (rc) {
            sck_state_flag.v = Flag::RISE;
        }
        else {
            sck_state_flag.v = Flag::SET;
            sendSignature();
            // TODO: ? Add user configurable call back for connect\disconnect ?
        }
        return;
    }

    if (sck_state_flag.v == Flag::FALL) {
        // Connection aborted.
        // Most probably TFlow Buffer Server is closed
        Disconnect();

        // TODO: ? Add user configurable call back for connect\disconnect ?

        // Try to reconnect
        sck_state_flag.v = Flag::RISE;
    }

}

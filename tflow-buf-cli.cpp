// TODO: check camera disconnection scenario

#include <unistd.h>
#include <cassert>
#include <functional>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

#include <glib-unix.h>

#include "tflow-perfmon.hpp"
#include "tflow-buf-cli.hpp"

static gboolean tflow_buf_cli_on_idle_once(gpointer data);

#define TFLOWBUFCLI_SIGNATURE "VStream"

TFlowBufCli::~TFlowBufCli()
{
    Disconnect();
}

int TFlowBufCli::onConsume(const TFlowBufPck::pck_consume* msg)
{
    // Sanity check for buffer index
    assert(msg->buff_index >= 0 && msg->buff_index < tflow_bufs.size());

#if 1
    {
        static int presc = 0;
        if ((++presc & 0xFF) == 0) g_warning("Processed %d frames", presc);
    }
#endif    

    return 0;
}

int TFlowBufCli::onSrcFDShm(TFlowBufPck::pck_fd* msg_src_info, int shm_fd)
{
    /*
     * Get total memory size
     *
     *   gap       0xCAFE0000
     *      gap1       0xCAFE0001
     *      frame      w x h x pixel_size
     *      gap2       0xCAFE0002
     *      aux_data        fixed structure
     *      gap3       0xCAFE0003
     *
     *      gap1       0xCAFE0011
     *      frame      w x h x pixel_size
     *      gap2       0xCAFE0012
     *      aux_data        fixed structure
     *      gap3       0xCAFE0013
     *         ....
     *   gap       0xCAFEFFFF
     */

    long frame_size = (msg_src_info->width * msg_src_info->height *
        ((msg_src_info->format == V4L2_PIX_FMT_GREY  ) ? 8 : 
         (msg_src_info->format == V4L2_PIX_FMT_BGR24 ) ? 24 :
         (msg_src_info->format == V4L2_PIX_FMT_ABGR32) ? 32 :
         (msg_src_info->format == V4L2_PIX_FMT_NV12  ) ? 12 : 0)) / 8;

    // Get filesize  
    struct stat shm_statbuf;
    int rc = fstat(shm_fd, &shm_statbuf);
        
    // Calculate aux_data_len
    div_t aux_data_len = { 0 };
    int non_aux_size = 2 * sizeof(uint32_t) + msg_src_info->buffs_num * (frame_size + 3 * sizeof(uint32_t));
    if (non_aux_size < shm_statbuf.st_size) {
        aux_data_len = std::div((int)(shm_statbuf.st_size - non_aux_size), msg_src_info->buffs_num);
        if (aux_data_len.rem != 0) {
            // bad format
            goto on_shm_err;
        }
        else {
            // 
            
        }
    }
    else if (non_aux_size == shm_statbuf.st_size) {
        // No aux data - it is OK
    }
    else {
        // Bad format
        goto on_shm_err;
    }

    shm_wr_len = shm_statbuf.st_size;
    shm_wr_ptr = (uint8_t*)mmap(nullptr, shm_wr_len, PROT_READ, MAP_SHARED, shm_fd, 0);

    if (!shm_wr_ptr) goto on_shm_err;

    // Validate memory magic words and initialize TFlowBuf accordignly
    if (*(uint32_t*)shm_wr_ptr != 0xCAFE0000 ) {
        goto on_shm_err;
    }

    shm_wr_ptr += sizeof(uint32_t);

    tflow_bufs.reserve(msg_src_info->buffs_num);
    for (int buf_idx = 0; buf_idx < msg_src_info->buffs_num; buf_idx++) {
        
        // Magic 1
        if (*(uint32_t*)shm_wr_ptr != 0xCAFE0001 + buf_idx * 0x10) goto on_shm_err;
        shm_wr_ptr += sizeof(uint32_t);

        // Frame body            
        TFlowBuf &tflow_buf = tflow_bufs.emplace_back();
#if CODE_BROWSE
        TFlowBuf();
#endif
        tflow_buf.index = buf_idx;
        tflow_buf.length = frame_size;
        tflow_buf.start = shm_wr_ptr;
        shm_wr_ptr += frame_size;

        // Magic 2
        if (*(uint32_t*)shm_wr_ptr != 0xCAFE0002 + buf_idx * 0x10) {
            goto on_shm_err;
        }
        shm_wr_ptr += sizeof(uint32_t);

        // Aux data
        tflow_buf.aux_data = shm_wr_ptr;
        tflow_buf.aux_data_len = aux_data_len.quot;
        shm_wr_ptr += aux_data_len.quot;

        // Magic 3
        if (*(uint32_t*)shm_wr_ptr != 0xCAFE0003 + buf_idx * 0x10) {
            goto on_shm_err;
        }
        shm_wr_ptr += sizeof(uint32_t);
    }

    // Last Magic
    if (*(uint32_t*)shm_wr_ptr != 0xCAFEFFFF) {
        goto on_shm_err;
    }

    return 0;

on_shm_err:

    tflow_bufs.clear();
    if (shm_wr_ptr != (uint8_t*)MAP_FAILED) {
        munmap(shm_wr_ptr, shm_wr_len);
        shm_wr_ptr = (uint8_t*)MAP_FAILED;
        shm_wr_len = 0;
    }
    return -1;
}

int TFlowBufCli::onSrcFDCam(TFlowBufPck::pck_fd* msg, int cam_fd)
{
    tflow_bufs.reserve(msg->buffs_num);

    for (int buf_idx = 0; buf_idx < msg->buffs_num; buf_idx++) {
        tflow_bufs.emplace_back(cam_fd, buf_idx, msg->planes_num);
#if CODE_BROWSE
        TFlowBuf(cam_fd, buf_idx, msg->planes_num);
#endif
    }

    return 0;
}

int TFlowBufCli::onSrcFD(TFlowBufPck::pck_fd* msg_src_info, int src_fd)
{
    this->cam_fd = src_fd;

    //tflow_bufs.reserve(msg_src_info->buffs_num);

    // TODO: ? Use planes_num as the packet's type marker?
    //       or introduce a yet another field - Source Type (Cam/Shm/Enc/...)
    //       ? or introduce a new packet for shm source?
    if (msg_src_info->planes_num < 0) {
        return onSrcFDShm(msg_src_info, cam_fd);
    }
    else {
        return onSrcFDCam(msg_src_info, cam_fd);
    }
}

int TFlowBufCli::onMsg()
{
    struct msghdr   msg;
    struct iovec    iov[1];
    char buf[CMSG_SPACE(sizeof(int))];
    int cam_dev_fd;
    ssize_t res;
    int err;

    msg.msg_name = nullptr;
    msg.msg_namelen = 0;

    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    /* Constructs shared pointer to the incoming packet and 
     * link with redeem function which will be called then
     * shared pointer counter reach zero.
     */
    std::shared_ptr<TFlowBufPck> sp_pck = std::make_shared<TFlowBufPck>(
        std::bind(&TFlowBufCli::sendRedeem, this, std::placeholders::_1));

    iov[0].iov_base = (void*)&sp_pck->d;
    iov[0].iov_len = sizeof(sp_pck->d);

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
                         
    // Read-out all data from the socket 
    res = recvmsg(sck_fd, &msg, MSG_NOSIGNAL);

    if (res <= 0) {
        err = errno;
        if (err == EPIPE || err == ECONNREFUSED || err == ENOENT) {
            // May happens on Server close
            g_warning("TFlowBufCli: TFlow Buffer Server closed");
        }
        else {
            g_warning("TFlowBufCli: unexpected error (%ld, %d) - %s",
                res, err, strerror(err));
        }

        sck_state_flag.v = Flag::FALL;
//        g_main_context_invoke(context, (GSourceFunc)tflow_buf_cli_on_idle_once, this);
        return G_SOURCE_REMOVE;
    }

    // Sanity
    if (iov[0].iov_len == 0) {
        g_warning("Oooops - Empty message");
        return G_SOURCE_CONTINUE;
    }

    switch (sp_pck->d.hdr.id) {
    case TFlowBufPck::TFLOWBUF_MSG_CAM_FD:
    {
        /* TODO: If another src already exist, then close it */
        // ...if (app_onSrcGone) app_onSrcGone();

        g_warning("---TFlowBufCli: Received - CAM_FD");
        if (msg.msg_controllen == 0) {
            g_warning("Oooops - Bad aux data");
            return G_SOURCE_CONTINUE;
        }

        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        int cam_fd = *(int*)CMSG_DATA(cmsg);
        TFlowBufPck::pck_fd *pck_src_info = (TFlowBufPck::pck_fd*)&sp_pck->d;

        if (0 == onSrcFD(pck_src_info, cam_fd)) {

            if (app_onSrcReady) app_onSrcReady(pck_src_info);
#if CODE_BROWSE
            TFlowVStream::onSrcReadyStreaming(const TFlowBufPck::pck_fd* pck_src_info);
            TFlowVStream::onSrcReadyRecording(const TFlowBufPck::pck_fd* pck_src_info);
#endif
            // Request initial frame
            sendRedeem(-1);
        }
        else {
            // Something wrong while memory/buffers allocation
            // TODO: what todo?
        }

        break;
    }
    case TFlowBufPck::TFLOWBUF_MSG_CONSUME:
    {
        const TFlowBufPck::pck_consume* msg_consume = &sp_pck->d.consume;
		pending_buf_request--;

        onConsume(msg_consume);      // Nothing todo here. Update statistics?
        if (app_onFrame) app_onFrame(msg_consume);
#if CODE_BROWSE
        TFlowVStream::onFrameStreaming(msg_consume);
        TFlowVStream::onFrameRecording(msg_consume);
#endif

        // Upon sp_pck.reset, if no other objects held the packet, the REDEEM
        // request will be sent from the packet destructor. 
        sp_pck.reset();
        
        if (pending_buf_request == 0) {
            // The packet was consumed and held by a consumer and no more 
            // pending requests left, so send additional one explicitly.
            sendRedeem(-1);
        }
        break;
    }
    case TFlowBufPck::TFLOWBUF_MSG_PING:
    case TFlowBufPck::TFLOWBUF_MSG_PONG:
    {
        break;
    }
    default:
        g_warning("Oooops - Unknown message received %d", sp_pck->d.hdr.id);
    }

    return G_SOURCE_CONTINUE;
}

TFlowBufCli::TFlowBufCli(
    GMainContext* app_context,
    const char* _cli_name, const char* _srv_name,
    std::function<void(const TFlowBufPck::pck_consume* msg_consume)> _app_onFrame,
    std::function<void(const TFlowBufPck::pck_fd* src_info)> _app_onSrcReady,
    std::function<void()> _app_onSrcGone,
    std::function<void()> _app_onConnect, 
    std::function<void()> _app_onDisconnect) 
    :
    cli_name(_cli_name), srv_name(_srv_name), 
    app_onFrame(_app_onFrame),
    app_onSrcReady(_app_onSrcReady),
    app_onSrcGone(_app_onSrcGone),
    app_onConnect(_app_onConnect),
    app_onDisconnect(_app_onDisconnect)
{
    context = app_context;
    sck_state_flag.v = Flag::UNDEF;
    sck_tag = NULL;
    sck_src = NULL;
    CLEAR(sck_gsfuncs);

    clock_gettime(CLOCK_MONOTONIC, &last_send_ts);
    last_conn_check_ts.tv_sec = 0;
    last_conn_check_ts.tv_nsec = 0;

    msg_seq_num = 0;
    pending_buf_request = 0;
    cam_fd = -1;
    shm_wr_ptr = (uint8_t*)MAP_FAILED;
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

    return cli->onMsg();
}

int TFlowBufCli::sendMsg(TFlowBufPck::pck &msg, int msg_id, int msg_custom_len = -1)
{
    ssize_t res;
    size_t msg_len;
    const char* comment = nullptr;

    if (sck_state_flag.v != Flag::SET) return 0;

    switch (msg_id) {
    case TFlowBufPck::TFLOWBUF_MSG_REDEEM:
        msg_len = sizeof(msg.redeem);
        comment = "Redeem";
        break;
    case TFlowBufPck::TFLOWBUF_MSG_SIGN:
        msg_len = sizeof(msg.sign);
        comment = "Signature";
        break;
    case TFlowBufPck::TFLOWBUF_MSG_PING:
        msg_len = sizeof(msg.ping);
        comment = "Ping";
        break;
    default:
        if (msg_id > TFlowBufPck::TFLOWBUF_MSG_CUSTOM_ && msg_custom_len > 0) {
            comment = "Custom";
            msg_len = msg_custom_len;
            break;
        }
        g_warning("TFlowBufCli: Bad message - %d, %d", msg_id, msg_custom_len);
        return 0;
    }

    // If message length specified, then it must correspond to the real one
    if (msg_custom_len > 1 && msg_len != msg_custom_len) {
        g_warning("TFlowBufCli: Ooops  @%d", __LINE__);
        return 0;
    }

    msg.hdr.seq = msg_seq_num++;
    msg.hdr.id = msg_id;

    res = send(sck_fd, &msg, msg_len, MSG_NOSIGNAL | MSG_DONTWAIT);
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

    if (msg_id > TFlowBufPck::TFLOWBUF_MSG_CUSTOM_) {
        static int presc = 0;
        if ( (presc++ & 0xFF) == 0 ) {
            g_warning("TFlowBufCli: Msg sent [%s] (%d) #%d", comment,
                msg.hdr.id, msg.hdr.seq);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &last_send_ts);

    return 0;
}

int TFlowBufCli::sendRedeem(int index)
{
    struct TFlowBufPck::pck_redeem msg_redeem{};
    msg_redeem.buff_index = index;
    msg_redeem.need_more = true;

    int res = sendMsg(msg_redeem, TFlowBufPck::TFLOWBUF_MSG_REDEEM);
    if (res) return res;

    pending_buf_request++;
    return 0;
}

int TFlowBufCli::sendPing()
{
    static int cnt = 0;
    TFlowBufPck::pck_ping msg_ping = { 0 };

    strncpy(msg_ping.cli_name, cli_name.c_str(), sizeof(msg_ping.cli_name) - 1);
    msg_ping.cnt = cnt++;

    sendMsg(msg_ping, TFlowBufPck::TFLOWBUF_MSG_PING);
    return 0;
}

int TFlowBufCli::sendSignature()
{
    TFlowBufPck::pck_sign msg_sign = { 0 };
    strncpy(msg_sign.cli_name, cli_name.c_str(), sizeof(msg_sign.cli_name) - 1);
    msg_sign.cli_pid = getpid();

    sendMsg(msg_sign, TFlowBufPck::TFLOWBUF_MSG_SIGN);
    return 0;
}

void TFlowBufCli::Disconnect()
{

    if (app_onSrcGone) {
        app_onSrcGone();
#if CODE_BROWSE
        TFlowVStream::onSrcGoneRecording();
        TFlowVStream::onSrcGoneStreaming();
#endif
    }

    if (app_onDisconnect) {
        app_onDisconnect();
#if CODE_BROWSE
            TFlowVStream::onDisconnect();
#endif
    }

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
    
    if (shm_wr_ptr != (uint8_t*)MAP_FAILED) {
        munmap(shm_wr_ptr, shm_wr_len);
        shm_wr_ptr = (uint8_t*)MAP_FAILED;
        shm_wr_len = 0;
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

    // Initialize socket address
    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    sock_addr.sun_path[0] = 0;
    memcpy(sock_addr.sun_path+1, srv_name.c_str(), srv_name.length());  // nullptr termination excluded

    socklen_t sck_len = sizeof(sock_addr.sun_family) + srv_name.length() + 1;   // +1 for leading zero
    rc = connect(sck_fd, (const struct sockaddr*)&sock_addr, sck_len);

    if (rc == -1) {
        static int presc = 0;
        if ((++presc & 0x07) == 0) {
            g_warning("TFlowBufCli: Can't connect to the server %s (%d) - %s",
                srv_name.c_str(), errno, strerror(errno));
        }

        close(sck_fd);
        sck_fd = -1;

        return -1;
    }

    g_warning("---TFlowBufCli: Connected to the server %s", srv_name.c_str());

    /* Assign g_source on the socket */
    sck_gsfuncs.dispatch = tflow_buf_cli_dispatch;
    sck_src = (GSourceCli*)g_source_new(&sck_gsfuncs, sizeof(GSourceCli));
    sck_tag = g_source_add_unix_fd((GSource*)sck_src, sck_fd, (GIOCondition)(G_IO_IN /* | G_IO_ERR | G_IO_HUP */));
    sck_src->cli = this;
    g_source_attach((GSource*)sck_src, context);

    return 0;
}

void TFlowBufCli::onIdle_no_ts()
{
    // Called as a kick 
    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    onIdle(now_ts);
}

void TFlowBufCli::onIdle(const struct timespec &now_ts)
{
    if (sck_state_flag.v == Flag::CLR) {
        if (TFlowPerfMon::diff_timespec_msec(&now_ts, &last_conn_check_ts) > 1000) {
            last_conn_check_ts = now_ts;
            sck_state_flag.v = Flag::RISE;
        }
        return;
    }

    if (sck_state_flag.v == Flag::SET) {
        // Normal operation. Check buffers are occupied for too long time.
        // ...
        //for (auto &tflow_buf : tflow_bufs) {
        //    if (tflow_buf.age() > 3000) {
        //        g_warning("Processing algo stall - force redeem");
        //        // Disqualify the client, close connection or ???
        //    }
        //}

        // Check idle connection. Capture is active, but camera is not.
        if (TFlowPerfMon::diff_timespec_msec(&now_ts, &last_send_ts) > 1000) {
            sendPing();
        }
        return;
    }

    if (sck_state_flag.v == Flag::UNDEF || sck_state_flag.v == Flag::RISE) {
        int rc;

        rc = Connect();
        if (rc) {
            sck_state_flag.v = Flag::FALL;
        }
        else {
            sck_state_flag.v = Flag::SET;
            /* Note: Using FIFO Streamer. 
             *       In case of Streamer reuse existing fifo for
             *       different Tflow Capture connection (for ex. Capture was
             *       closed and reopened), the gstreamer at another fifo's end
             *       generates video with delay >1sec. Therefore, the gstreamer
             *       need to be restarted. Closing TFlowStreamer will close
             *       gstreamer if active.
             * TODO: replace with Glib message/event to APP
             */
            if (app_onConnect) app_onConnect();

            sendSignature();
        }
        return;
    }

    if (sck_state_flag.v == Flag::FALL) {
        // Connection aborted.
        // Most probably TFlow Buffer Server is closed
        Disconnect();

        // Try to reconnect later
        sck_state_flag.v = Flag::CLR;
    }
}

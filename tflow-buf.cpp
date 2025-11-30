#include "tflow-build-cfg.hpp"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cassert>
#include <linux/videodev2.h> //V4L2 stuff

#include <glib-unix.h>

#include "tflow-common.hpp"
#include "tflow-buf.hpp"
#include "tflow-buf-pck.hpp"

TFlowBuf::~TFlowBuf()
{
    // VStream do mmap for all buffers at once but not for a simgle buffer 
    // And v4l2_buf isn't in use
    
    if (start != MAP_FAILED) {
        if (mem_type == 0) {
            munmap(start, length);
            start = MAP_FAILED;
            length = 0;
        }
    }

    if (v4l2_buf.m.planes) {
        free(v4l2_buf.m.planes);
    }
}

TFlowBuf::TFlowBuf(int enc_fd, enum v4l2_buf_type buf_type, int index, int planes_num)
{
    v4l2_plane *planes = (struct v4l2_plane*)calloc(VIDEO_MAX_PLANES, sizeof(struct v4l2_plane)); // TODO: ??? planes_num allocate number of planes used only ???
    assert(planes);

    CLEAR(v4l2_buf);
    v4l2_buf.type     = buf_type;
    v4l2_buf.memory   = V4L2_MEMORY_MMAP;
    v4l2_buf.m.planes = planes;
    v4l2_buf.length   = planes_num;
    v4l2_buf.index    = index;

    this->owners = 0;
    this->index = -1;   
    this->length = 0;
    this->start = MAP_FAILED;
    this->state = BUF_STATE_BAD;
    this->mem_type = 0; // SHM  - needs to be released

    int rc = ioctl(enc_fd, VIDIOC_QUERYBUF, &v4l2_buf);
    if (rc) {
        g_warning("Can't VIDIOC_QUERYBUF type=%d %d (%d) - %s",
            buf_type, rc, errno, strerror(errno));
    }
    else {
        // Record the length and mmap buffer to user space
        this->length = v4l2_buf.m.planes[0].length;
        this->start = mmap(nullptr, this->length,
            PROT_READ | PROT_WRITE, MAP_SHARED, enc_fd, v4l2_buf.m.planes[0].m.mem_offset);
        this->index = index;
        this->state = BUF_STATE_FREE;
    }
}

TFlowBuf::TFlowBuf()
{
    memset(this, 0, sizeof(*this));

    sequence = 0;

    /* Parameters obtained from Kernel on the Client side */
    index = -1;
    length = 0;
    start = MAP_FAILED;
    mem_type = 1; // don't release memory on desctructor

    owners = 0;             // Bit mask of TFlowBufCli. Bit 0 - means buffer is in user space

    aux_data_len = 0;
    aux_data = nullptr;
}

int TFlowBuf::age() {
    int rc;
    struct timespec tp;
    unsigned long proc_frame_ms, now_ms;

    rc = clock_gettime(CLOCK_MONOTONIC, &tp);
    now_ms = tp.tv_sec * 1000 + tp.tv_nsec / 1000000;
    proc_frame_ms = ts.tv_sec * 1000 + ts.tv_usec / 1000;

    return (now_ms - proc_frame_ms);
}

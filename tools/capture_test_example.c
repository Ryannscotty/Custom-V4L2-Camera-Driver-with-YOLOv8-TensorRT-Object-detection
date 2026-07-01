/*
 * capture.c — A minimal, heavily-commented V4L2 capture program.
 *
 * Goal: open a UVC webcam (e.g. Logitech C270), stream YUYV frames using the
 * mmap I/O method, convert one frame to RGB by hand, and write it out as a
 * binary PPM image. No external libraries — just the kernel's V4L2 API.
 *
 * Build:   gcc -O2 -Wall -o capture capture.c
 * Run:     ./capture                 (uses /dev/video0, saves frame.ppm)
 *          ./capture /dev/video2     (pick a different device)
 *
 * View the result:  any image viewer that reads PPM, or convert it:
 *          (PPM "P6" is raw RGB; eog/feh/GIMP open it directly)
 *
 * The flow, in one sentence: we hand the kernel a pool of buffers, the driver
 * fills them with frames as they arrive over USB, and we cycle each buffer
 * out (DQBUF) to read it and back in (QBUF) to be refilled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>          /* open, O_RDWR */
#include <unistd.h>         /* close, read */
#include <sys/ioctl.h>      /* ioctl */
#include <sys/mman.h>       /* mmap, munmap */
#include <sys/select.h>     /* not used directly, poll.h below */
#include <poll.h>           /* poll — wait until a frame is ready */
#include <linux/videodev2.h>/* the V4L2 API: structs + VIDIOC_* ioctl codes */

#define DEFAULT_DEVICE  "/dev/video0"
#define WIDTH           640
#define HEIGHT          480
#define BUFFER_COUNT    4      /* how many frames the kernel can buffer for us */
#define WARMUP_FRAMES   30     /* discard early frames so auto-exposure settles */
#define OUTPUT_PPM      "frame.ppm"

/* One mmap'd buffer: where it lives in OUR address space, and how big it is. */
struct buffer {
    void   *start;
    size_t  length;
};

/* -------------------------------------------------------------------------
 * xioctl: ioctl() can be interrupted by a signal and return EINTR before it
 * actually did anything. The correct, idiomatic thing is to simply retry.
 * Every V4L2 call goes through this wrapper.
 * ---------------------------------------------------------------------- */
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

/* Print the failing call + errno, then bail. errno is the *why* — read it. */
static void fail(const char *what)
{
    fprintf(stderr, "%s failed: %d, %s\n", what, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

/* Clamp an int to a valid 0..255 byte. YUV->RGB math can overshoot. */
static inline unsigned char clamp(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

/* -------------------------------------------------------------------------
 * YUYV -> RGB, written out by hand once for understanding.
 *
 * YUYV is YUV 4:2:2 *packed*: each 4 bytes encode TWO pixels that SHARE one
 * U and one V (chroma is subsampled horizontally). Layout per group:
 *      [ Y0 ][ U ][ Y1 ][ V ]
 * so pixel0 = (Y0,U,V) and pixel1 = (Y1,U,V).
 *
 * We use the standard BT.601 limited-range integer conversion. Webcams emit
 * "limited range" (Y in 16..235), hence the -16 / -128 offsets.
 *
 * NOTE: we walk rows using `stride` (bytesperline), NOT width*2, because the
 * driver may pad each row. Respecting stride is a real-world V4L2 habit.
 * ---------------------------------------------------------------------- */
static void yuyv_to_rgb(const unsigned char *yuyv, unsigned char *rgb,
                        int width, int height, int stride)
{
    for (int y = 0; y < height; y++) {
        const unsigned char *row = yuyv + (size_t)y * stride;
        unsigned char *out = rgb + (size_t)y * width * 3;

        /* Two source pixels per 4-byte group, so step the row in pairs. */
        for (int x = 0; x < width; x += 2) {
            int y0 = row[0];
            int u  = row[1];
            int y1 = row[2];
            int v  = row[3];
            row += 4;

            int c0 = y0 - 16, c1 = y1 - 16;
            int d  = u  - 128, e  = v  - 128;

            /* pixel 0 */
            *out++ = clamp((298 * c0 + 409 * e + 128) >> 8);            /* R */
            *out++ = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);  /* G */
            *out++ = clamp((298 * c0 + 516 * d + 128) >> 8);            /* B */

            /* pixel 1 (shares u,v) — guard the edge if width is odd */
            if (x + 1 < width) {
                *out++ = clamp((298 * c1 + 409 * e + 128) >> 8);
                *out++ = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
                *out++ = clamp((298 * c1 + 516 * d + 128) >> 8);
            }
        }
    }
}

static void write_ppm(const char *path, const unsigned char *rgb,
                      int width, int height)
{
    FILE *f = fopen(path, "wb");
    if (!f) fail("fopen(output)");
    /* P6 = binary RGB. Header is text, then raw width*height*3 bytes. */
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb, 1, (size_t)width * height * 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : DEFAULT_DEVICE;

    /* --- 1. open the device ------------------------------------------------
     * O_RDWR because streaming needs both. O_NONBLOCK is optional; we instead
     * use poll() to wait, so a plain blocking fd is fine here.
     */
    int fd = open(dev, O_RDWR);
    if (fd == -1) fail("open(device)");

    /* --- 2. VIDIOC_QUERYCAP: what can this device do? ----------------------
     * Confirms it's actually a capture device and supports streaming I/O.
     * A common gotcha: a /dev/videoN node can be a metadata/control node that
     * does NOT have VIDEO_CAPTURE — querying caps catches that early.
     */
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) fail("VIDIOC_QUERYCAP");
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is not a video capture device\n", dev);
        exit(EXIT_FAILURE);
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming I/O\n", dev);
        exit(EXIT_FAILURE);
    }
    printf("Device : %s (%s)\n", cap.card, cap.driver);

    /* --- 3. VIDIOC_S_FMT: set resolution + pixel format -------------------
     * We ASK for 640x480 YUYV. The driver may return something different if it
     * can't honor the request, so we re-read the struct and verify. Always
     * trust what the driver gives back, never what you asked for.
     */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;   /* progressive, not interlaced */
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) fail("VIDIOC_S_FMT");

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        fprintf(stderr,
            "Driver would not give YUYV (this cam may only offer it at certain\n"
            "resolutions; higher modes are MJPEG). Try a smaller size, or run\n"
            "`v4l2-ctl --list-formats-ext` to see what's available.\n");
        exit(EXIT_FAILURE);
    }
    /* Use the negotiated geometry from here on. */
    int width  = fmt.fmt.pix.width;
    int height = fmt.fmt.pix.height;
    int stride = fmt.fmt.pix.bytesperline;   /* row length incl. any padding */
    printf("Format : %dx%d YUYV, stride=%d, image=%u bytes\n",
           width, height, stride, fmt.fmt.pix.sizeimage);

    /* --- 4. VIDIOC_REQBUFS: ask the kernel to allocate frame buffers -------
     * memory = MMAP means the kernel allocates them and we'll map them in.
     * The driver may give fewer than we asked for; check the returned count.
     */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count  = BUFFER_COUNT;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) fail("VIDIOC_REQBUFS");
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory (got %u)\n", req.count);
        exit(EXIT_FAILURE);
    }

    struct buffer *buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) fail("calloc(buffers)");

    /* --- 5. VIDIOC_QUERYBUF + mmap: map each kernel buffer into our space --
     * QUERYBUF tells us each buffer's offset+length; mmap() then makes the
     * kernel's frame memory directly readable by us with no copy.
     */
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) fail("VIDIOC_QUERYBUF");

        buffers[i].length = buf.length;
        buffers[i].start  = mmap(NULL, buf.length,
                                 PROT_READ | PROT_WRITE, MAP_SHARED,
                                 fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) fail("mmap");
    }

    /* --- 6. VIDIOC_QBUF (x N): hand every buffer to the kernel ------------
     * Queuing = "this buffer is yours to fill." We queue all of them before
     * starting so the driver always has somewhere to write.
     */
    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) fail("VIDIOC_QBUF (initial)");
    }

    /* --- 7. VIDIOC_STREAMON: the camera starts producing frames ----------- */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) fail("VIDIOC_STREAMON");

    /* --- 8. the capture loop ----------------------------------------------
     * For each frame: poll() until the fd is readable (a frame is ready),
     * DQBUF to take the filled buffer, do something with the pixels, then
     * QBUF to give the buffer back so it can be refilled. We discard the
     * first WARMUP_FRAMES (sensor/auto-exposure ramp), then save the next one.
     */
    unsigned char *rgb = malloc((size_t)width * height * 3);
    if (!rgb) fail("malloc(rgb)");

    for (int frame = 0; frame <= WARMUP_FRAMES; frame++) {
        /* Wait up to 2s for a frame. poll() returning 0 = timeout. */
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 2000);
        if (pr == -1) {
            if (errno == EINTR) { frame--; continue; }
            fail("poll");
        }
        if (pr == 0) {
            fprintf(stderr, "Timeout waiting for frame\n");
            exit(EXIT_FAILURE);
        }

        /* DQBUF: pull a filled buffer out of the kernel's done-queue. The
         * returned buf.index tells us WHICH of our mmap'd buffers it is, and
         * buf.bytesused how many bytes are valid in it. */
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
            /* EAGAIN here would mean "no buffer ready yet" on a nonblocking
             * fd; with poll() gating us it shouldn't happen, but handle it. */
            if (errno == EAGAIN) { frame--; continue; }
            fail("VIDIOC_DQBUF");
        }

        if (frame == WARMUP_FRAMES) {
            /* This is the frame we keep. buffers[buf.index].start points at
             * raw YUYV bytes the driver just wrote. Convert and save. */
            yuyv_to_rgb(buffers[buf.index].start, rgb, width, height, stride);
            write_ppm(OUTPUT_PPM, rgb, width, height);
            printf("Saved %s (%dx%d, %u bytes captured)\n",
                   OUTPUT_PPM, width, height, buf.bytesused);
        }

        /* QBUF: return the buffer to the kernel to be refilled. Forgetting
         * this is the classic bug — the queue drains and capture stalls. */
        if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) fail("VIDIOC_QBUF (recycle)");
    }

    /* --- 9. teardown: stop, unmap, close ----------------------------------- */
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1) fail("VIDIOC_STREAMOFF");
    for (unsigned i = 0; i < req.count; i++)
        munmap(buffers[i].start, buffers[i].length);
    free(buffers);
    free(rgb);
    close(fd);

    return EXIT_SUCCESS;
}

#ifndef _C270CAPTURE_H
#define _C270CAPTURE_H
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <poll.h>
#include <assert.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/videodev2.h>

#define C270_DEVICE "/dev/video2"
#define WIDTH 640
#define HEIGHT 480
#define BUFF_COUNT 4 
#define WARM_UP_FRAMES 30
#define OUTPUT_FRAME "frame.ppm"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

class C270CameraDriver
{
    public:
        int CameraFD = -1;
        C270CameraDriver()
        {
        }
        void openDevice(const char *Campath);
        void closeDevice(void);
        static void errno_exit(const char *s)
        {
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
        }
        
};





#endif


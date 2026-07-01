#include "C270Capture.h"
#include <asm-generic/errno-base.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>



void C270CameraDriver::openDevice(const char *Campath)
{
    struct stat CamfileStructure;

    /* check the camera device file structure */ 
     if(stat(Campath,&CamfileStructure) == -1)
     {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", Campath, errno, strerror(errno));
                exit(EXIT_FAILURE);
     }
    /* check if the device is open/present */ 
   if(!S_ISCHR(CamfileStructure.st_mode))
   {
        fprintf(stderr, "%s is no device\n", Campath);
        exit(EXIT_FAILURE);
   }
    
    /* open the camera */ 
   CameraFD = open(Campath,O_RDWR | O_NONBLOCK, 0); 

   if(CameraFD == -1)
   {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", Campath, errno, strerror(errno));
        exit(EXIT_FAILURE);
   }
    printf( "open device\n");
}

void C270CameraDriver::closeDevice(void)
{
    if(close(CameraFD) == -1)
    {
        errno_exit("close");
    }

    CameraFD = -1;
    printf( "close device\n");
}

int C270CameraDriver::Cam_IO_CTL(int CamFilePointer, unsigned long request, void *arg)
{
    int r;
    do 
    {
        r = ioctl(CamFilePointer,request, arg);

    }
    while (r == -1 && errno == EINTR);

    return r;
}


void C270CameraDriver::checkDeviceCapability(int CamFilePointer)
{
    struct v4l2_capability CameraCapability;
    struct v4l2_cropcap CameraCropCapability;
    struct v4l2_crop CameraCrop;
    struct v4l2_format CameraFormat;

    unsigned int minimun;
    /* check the camera capabilities: capture or stream or n/a */ 
    CLEAR(CameraCapability);
    if(Cam_IO_CTL(CamFilePointer,VIDIOC_QUERYCAP, &CameraCapability) == -1)
    {
        if(errno == EINVAL)
        {
            fprintf(stderr,"%s is no v4l2 device\n",C270_DEVICE);
            exit(EXIT_FAILURE);
        }
        else {
             errno_exit("VIDIOC_QUERYCAP");
        }
    }


    if(!(CameraCapability.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr,"%s is not a video capture device\n",C270_DEVICE);
        exit(EXIT_FAILURE);
    }


   if(!(CameraCapability.capabilities & V4L2_CAP_STREAMING))
   {
       fprintf(stderr,"%s does not support streaming I/O\n", C270_DEVICE);
       exit(EXIT_FAILURE);
   }

    printf("Device : %s (%s)\n", CameraCapability.card,CameraCapability.driver);

}















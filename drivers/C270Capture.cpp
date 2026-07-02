#include "C270Capture.h"
#include <asm-generic/errno-base.h>
#include <cerrno>
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

void C270CameraDriver::Set_Cam_Res_Format(int CamFilePointer)
{
    struct v4l2_cropcap CameracropCapability;
    struct v4l2_crop Cameracrop;
    struct v4l2_format CameraFormat;
    unsigned int minimun;
    int formattedwidth,formattedheight,formattedstride;

    CLEAR(CameracropCapability);
    CameracropCapability.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(Cam_IO_CTL(CamFilePointer, VIDIOC_CROPCAP, &CameracropCapability) == 0)
    {
        Cameracrop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        Cameracrop.c = CameracropCapability.defrect;
        
        if(Cam_IO_CTL(CamFilePointer, VIDIOC_S_CROP, &Cameracrop) == -1)
        {           
            switch(errno)
            {
                case EINVAL:
                    /* Cropping not supported. */
                    fprintf(stderr,"Cropping not supported for %s\n", C270_DEVICE);
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }        
        }
    }

    /*specify our resolution  640 X 480*/
    CLEAR(CameraFormat);
    CameraFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CameraFormat.fmt.pix.width = WIDTH;
    CameraFormat.fmt.pix.height = HEIGHT;
    CameraFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    CameraFormat.fmt.pix.field = V4L2_FIELD_NONE;

    if(Cam_IO_CTL(CamFilePointer, VIDIOC_S_FMT, &CameraFormat) == -1)
    {
                            
        fprintf(stderr, "VIDIOC_S_FMT failed: %d, %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

   /* keep frames in bounds*/ 
    minimun = CameraFormat.fmt.pix.width * 2;

    if(CameraFormat.fmt.pix.bytesperline < minimun)
    {
        CameraFormat.fmt.pix.bytesperline = minimun;
    }
    
    minimun = CameraFormat.fmt.pix.bytesperline * CameraFormat.fmt.pix.height;

    if(CameraFormat.fmt.pix.sizeimage < minimun)
    {
        CameraFormat.fmt.pix.sizeimage = minimun;
    }

    formattedheight = CameraFormat.fmt.pix.height;
    formattedwidth = CameraFormat.fmt.pix.width;
    formattedstride = CameraFormat.fmt.pix.bytesperline;

    printf("Format : %dx%d YUYV, stride=%d, image=%u bytes\n"
            ,formattedwidth,formattedheight,formattedstride,CameraFormat.fmt.pix.sizeimage);
}













#include "C270Capture.h"
#include <fcntl.h>



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









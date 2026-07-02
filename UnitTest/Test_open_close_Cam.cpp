#include "Test_open_close_Cam.h"
#include "C270Capture.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <thread> // Required for std::this_thread
#include <chrono> // Required for std::chrono alternatives






void Cam_test::test1(void)
{
	C270CameraDriver C270Device;
	C270Device.openDevice(C270_DEVICE);
	std::this_thread::sleep_for(std::chrono::seconds(1));
    C270Device.checkDeviceCapability(C270Device.CameraFD);
	std::this_thread::sleep_for(std::chrono::seconds(1));
    C270Device.Set_Cam_Res_Format(C270Device.CameraFD);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	C270Device.closeDevice();
}









#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
//
#include <LEPTON_OEM.h>
#include <LEPTON_SDK.h>
#include <LEPTON_SYS.h>
#include <LEPTON_Types.h>
#include <chrono>
#include <thread>

constexpr LEP_UINT16 kI2CPortID{1};
constexpr LEP_UINT16 kI2CPortBaudRate{400};
constexpr LEP_CAMERA_PORT_E kI2CPortType{LEP_CCI_TWI};

int main() {
    static LEP_CAMERA_PORT_DESC_T _port;
    LEP_RESULT result = LEP_OpenPort(kI2CPortID, kI2CPortType, kI2CPortBaudRate, &_port);
    if (result !=  LEP_OK) {
        std::cerr << "Unable to open I2C communication.";
        return 1;
    }
    for (int i = 0; i < 5; i++)
    {
        LEP_UINT32 uptime;
        result = LEP_GetSysCameraUpTime(&_port, &uptime);
        if (result != LEP_OK) {
            std::cerr << "Unable to get uptime.";
            return 1;
        }
        std::cout << "Uptime: " << uptime << " seconds\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    result = LEP_SetOemGpioMode(&_port, LEP_OEM_GPIO_MODE_VSYNC);
    if (result != LEP_OK) {
        std::cerr << "LEP_SetOemGpioMode.";
        return 1;
    }

    // get
    LEP_OEM_GPIO_MODE_E pMode;
    LEP_GetOemGpioMode(&_port, & pMode);
    std::cout << "Mode: " << pMode << std::endl;
    LEP_ClosePort(&_port);
}
// #include "cci.h"
// #include <chrono>
// #include <thread>
// int main()
// {
//     int fd = open("/dev/i2c-1", O_RDWR);
//     if (fd < 0) {
//         return 1;
//     }
//     cci_init(fd);
//     for (;;) {
//         auto uptime = cci_get_uptime(fd);
//         std::cout << "Uptime: " << uptime << " seconds\n";
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         // close shutter
//
//     }
//     close(fd);
//     return 0;
// }

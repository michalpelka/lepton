#pragma once
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
#include <gpiod.h>
#include "voisp.h"
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include "pgm.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
// OpenCV display
#include <opencv2/opencv.hpp>
#include <functional>

namespace lepton {
    class Lepton
    {
    public:
        Lepton();
        ~Lepton();

        // open i2c (LEP) communication to camera
        bool startI2c(const char* devicePath = "", int address = 0x2a);
        // open/configure spi device (e.g. /dev/spidev0.0)
        bool startSpi(const char* devicePath = "/dev/spidev0.0", uint32_t speed_hz = 25000000);
        // initialize GPIO (chip name like "gpiochip0", vsync and debug line numbers)
        bool initGpio(const char* chipName = "gpiochip0", int vsync = 21, int debug = 20);

        // configure LEP OEM GPIO mode (calls LEP_SetOemGpio*). Returns false on error.
        bool configureOemGpio();

        // start capture threads (returns immediately)
        void capture();
        // request shutdown and join threads
        void shutdown();

        // query camera uptime (returns false if SDK call fails)
        bool getCameraUptime(uint32_t &uptime);

        void setFrameCallback(std::function<void(cv::Mat&)> cb)
        {
            frameCallback = cb;
        }

        bool GPIO_GetVsync() ;

        void GPIO_DebugSet(bool high);

    private:
        // config
        int m_gpioVsync = 0;
        int m_gpioDebug = 0;
        std::string m_spiDevice = "/dev/spidev0.0";
        uint8_t m_mode = SPI_CPOL | SPI_CPHA;
        uint8_t m_bits = 8;
        uint32_t m_speed = 25000000;
        uint16_t m_delay = 10;

        // fds and libs
        int spiFd = -1;
        int i2cFd = -1; // not directly used, LEP_OpenPort provides descriptor
        LEP_CAMERA_PORT_DESC_T m_lepPort{};

        struct gpiod_chip *chip = nullptr;
        struct gpiod_line *vs_line = nullptr;
        struct gpiod_line *dbg_line = nullptr;

        // capture/frame handling
        std::function<void(cv::Mat&)> frameCallback;

        // internal threads and coordination
        std::atomic<bool> m_running{false};

        // segment buffers (4 segments)
        static constexpr size_t VOSPI_FRAME_SIZE = 164;
        static constexpr size_t BUFFER_VOSPI_FRAMES = 100;
        static constexpr size_t LEP_SPI_BUFFER = VOSPI_FRAME_SIZE * BUFFER_VOSPI_FRAMES;
        using SegmentData = std::array<uint8_t, LEP_SPI_BUFFER>;
        std::array<SegmentData, 4> segments{};
        int segmentCount = 0;
        std::mutex segmentsMtx;
        std::condition_variable segmentCv;
        std::thread m_savenetThread;
        std::thread m_gpioThread;

    };
}

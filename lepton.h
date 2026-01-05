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
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
// OpenCV display
#include <opencv2/opencv.hpp>
#include <functional>
#include <span>

namespace lepton {
    struct TelemetryRowC
    {
        uint16_t gainMode;              // 0 high, 1 low, 2 auto
        uint16_t effectiveGainMode;     // 0 high, 1 low
        uint16_t gainModeDesiredFlag;   // 0 ok, 1 switch requested

        uint16_t tempGainHighToLow_C;
        uint16_t tempGainLowToHigh_C;

        uint16_t tempGainHighToLow_K;
        uint16_t tempGainLowToHigh_K;

        uint16_t populationHighToLow;
        uint16_t populationLowToHigh;

        uint16_t roiStartRow;
        uint16_t roiStartCol;
        uint16_t roiEndRow;
        uint16_t roiEndCol;

        bool tLinearEnabled;
        uint16_t tLinearResolution; // 0=0.1K 1=0.01K

        uint16_t spotMeanKelvin;
        uint16_t spotMaxKelvin;
        uint16_t spotMinKelvin;
        uint16_t spotPopulation;
        uint16_t spotRoiStartRow;
        uint16_t spotRoiStartCol;
    };
    struct TelemetryRowB
    {
        uint16_t emissivity;                 // scaled ×8192
        uint16_t backgroundTempKelvinX100;
        uint16_t atmosphericTransmission;    // ×8192
        uint16_t atmosphericTempKelvinX100;
        uint16_t windowTransmission;         // ×8192
        uint16_t windowReflection;           // ×8192
        uint16_t windowTempKelvinX100;
        uint16_t windowReflectedTempKelvinX100;
    };
    struct TelemetryRowA
    {
        uint8_t revMajor;
        uint8_t revMinor;

        uint32_t timeCounterMs;
        uint32_t statusBits;

        uint16_t moduleSerial[8];
        uint16_t softwareRevision[4];

        uint32_t frameCounter;
        uint16_t frameMean;

        uint16_t fpaTempCounts;
        uint16_t fpaTempKelvinX100;

        uint16_t housingTempCounts;
        uint16_t housingTempKelvinX100;

        uint16_t fpaTempLastFFCKelvinX100;

        uint32_t timeLastFFC;
        uint16_t housingTempLastFFCKelvinX100;

        uint16_t agcRoiTop;
        uint16_t agcRoiLeft;
        uint16_t agcRoiBottom;
        uint16_t agcRoiRight;

        uint16_t agcClipHigh;
        uint16_t agcClipLow;

        uint16_t videoOutputFormatHi;
        uint16_t videoOutputFormatLo;

        uint16_t log2FfcFrames;
    };

    struct Telemetry
    {
        TelemetryRowA A;
        TelemetryRowB B;
        TelemetryRowC C;
    };



    class Lepton
    {
    public:


        static constexpr size_t VOSPI_FRAME_SIZE = 164;
        static constexpr size_t BUFFER_VOSPI_FRAMES_MIN = 60;

        static constexpr size_t BUFFER_VOSPI_FRAMES = 75;

        static constexpr size_t LEP_SPI_BUFFER = VOSPI_FRAME_SIZE * BUFFER_VOSPI_FRAMES;
        static std::pair<cv::Mat, std::optional<Telemetry>> ProcessDataSegmentsToMatU16(const std::vector<std::span<const uint8_t>>& segmentsToProcess, bool is_telemetry = false);
        static cv::Mat ScaleToU8(const cv::Mat& mat);
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
        void setFrameWithoutScale(std::function<void(const cv::Mat&)> cb) {
            frameCallbackNoScale = std::move(cb);
        }
        void setFrameCallback(std::function<void(const cv::Mat&)> cb)
        {
            frameCallback = std::move(cb);
        }
        void setRawFrameCallback(std::function<void(std::vector<uint8_t>&)> cb) {
            frameRawData = std::move(cb);
        }

        bool ProcessBlob(uint8_t* blob, size_t blobSize);

        bool GPIO_GetVsync() ;

        void GPIO_DebugSet(bool high);
        static void printTelemetry(const Telemetry& t);

    private:
        static Telemetry parseTelemetry(const uint16_t* a, const uint16_t* b, const uint16_t* c);

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
        std::function<void(const cv::Mat&)> frameCallback;
        std::function<void(const cv::Mat&)> frameCallbackNoScale;
        std::function<void(std::vector<uint8_t>&)> frameRawData;

        // internal threads and coordination
        std::atomic<bool> m_running{false};

        // segment buffers (4 segments)

        using SegmentData = std::array<uint8_t, LEP_SPI_BUFFER>;
        std::array<SegmentData, 4> segments{};
        int segmentCount = 0;
        std::mutex segmentsMtx;
        std::condition_variable segmentCv;
        std::thread m_savenetThread;
        std::thread m_gpioThread;

    };
}

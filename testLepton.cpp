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

constexpr LEP_UINT16 kI2CPortID{1};
constexpr LEP_UINT16 kI2CPortBaudRate{400};
constexpr LEP_CAMERA_PORT_E kI2CPortType{LEP_CCI_TWI};

#define VSYNC_GPIO 21
#define DEBUG_GPIO_OUT 20
struct gpiod_chip *chip = nullptr;
struct gpiod_line *vs_line = nullptr;
struct gpiod_line *dbg_line = nullptr;

int spiFd = 0;
static const char *device = "/dev/spidev0.0";
static uint8_t mode = SPI_CPOL | SPI_CPHA;
static uint8_t bits = 8;
static uint32_t speed = 25000000;
static uint16_t delay = 0;
static uint8_t status_bits = 0;
#include <sched.h>
#include <iostream>

void set_realtime_priority(int prio = 90)
{
    struct sched_param sp;
    sp.sched_priority = prio;

    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("sched_setscheduler");
    } else {
        std::cout << "Real-time priority set to " << prio << "\n";
    }
}

void GPIO_Init() {
    chip = gpiod_chip_open_by_name("gpiochip0");
    if (chip == nullptr) {
        std::cerr << "Unable to open gpiochip0" << std::endl;
        std::exit(1);
    }
    vs_line = gpiod_chip_get_line(chip, VSYNC_GPIO);
    if (!vs_line) {
        std::cerr << "Unable to open CS_GPIO" << std::endl;
        std::exit(1);
    }
    // Request as input
    if (gpiod_line_request_input(vs_line, "vsync-reader") < 0) {
        std::cerr << "Failed to request line as input\n";
        std::exit(1);
    }

    // --- DEBUG OUTPUT LINE ---
    dbg_line = gpiod_chip_get_line(chip, DEBUG_GPIO_OUT);
    if (!dbg_line) {
        std::cerr << "Unable to open DEBUG_GPIO_OUT\n";
        std::exit(1);
    }
    if (gpiod_line_request_output(dbg_line, "debug-out", 0) < 0) {
        std::cerr << "Failed to request DEBUG line as output\n";
        std::exit(1);
    }

}

bool GPIO_GetVsync()
{
    int val = gpiod_line_get_value(vs_line);
    if (val < 0) {
        std::cerr << "Failed to read GPIO\n";
        return false;  // or handle error
    }
    return val == 1;
}

void GPIO_DebugSet(bool high)
{
    if (gpiod_line_set_value(dbg_line, high ? 1 : 0) < 0) {
        std::cerr << "Failed to set DEBUG GPIO\n";
    }
}


void setupSpi() {

    spiFd = open(device, O_RDWR);
    int ret;
    if (spiFd < 0)
    {
        std::cerr << "can't open device" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1)
    {
        std::cerr << "can't set spi mode" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_RD_MODE, &mode);
    if (ret == -1)
    {
        std::cerr << "can't get spi mode" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        std::cerr << "can't set bits per word" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        std::cerr << "can't get bits per word" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        std::cerr << "can't set max speed hz" << std::endl;
        exit(1);
    }

    ret = ioctl(spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        std::cerr << "can't get max speed hz" << std::endl;
        exit(1);
    }

    printf("spi mode: %d\n", mode);
    printf("bits per word: %d\n", bits);
    printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);

}


int main() {
    struct sched_param sp;
    sp.sched_priority = 20;

    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        int e = errno;
        std::cerr << "sched_setscheduler failed: (errno="<< e << ")\n";
        if (e == EPERM) {
            std::cerr << "Need root or cap_sys_nice. Options:\n"
                      << "  * sudo ./your_program\n"
                      << "  * sudo chrt -f 90 ./your_program\n"
                      << "  * setcap 'cap_sys_nice=ep' /path/to/your_program\n";
        }
    }

    GPIO_Init();
    setupSpi();

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

    result = LEP_SetOemGpioVsyncPhaseDelay(&_port, LEP_OEM_VSYNC_DELAY_NONE);
    if (result != LEP_OK) {
        std::cerr << "LEP_SetOemGpioVsyncPhaseDelay.";
        return 1;
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


    uint8_t packet_number = 0;
    uint8_t segment = 0;
    uint8_t current_segment = 0;
    int packet = 0;
    int state = 0;  //set to 1 when a valid segment is found
    int pixel = 0;

    const size_t VOSPI_FRAME_SIZE (164);
    const size_t BUFFER_VOSPI_FRAMES = 100;
    const size_t  LEP_SPI_BUFFER  = VOSPI_FRAME_SIZE * BUFFER_VOSPI_FRAMES;
    // Ok, I need to wait for GPIO
    uint8_t rx_buf[LEP_SPI_BUFFER];

    bool ready = false;  // shared state, protected by mtx
    uint8_t tx_dummy[LEP_SPI_BUFFER] = {0};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_dummy,
        .rx_buf = (unsigned long)rx_buf,
        .len = LEP_SPI_BUFFER,
        .speed_hz = speed,
        .delay_usecs = 10,
        .bits_per_word = 8
    };



    using SegmentData = std::array<uint8_t, LEP_SPI_BUFFER>;
    std::array<SegmentData, 4> segments;
    int segmentCount =0;
    std::mutex segmentsMtx;
    std::condition_variable segmentCv;

    std::thread savenetThread([&]() {

        for (;;) {
            std::array<SegmentData, 4> segmentsCopy {};
            {
                std::unique_lock<std::mutex> lk(segmentsMtx);
                segmentCv.wait(lk, [&]() {
                    return segmentCount == 4;
                });

                std::swap(segmentsCopy, segments);
                segmentCount = 0;
            }

            unsigned int lepton_image[240][80];
            // initialize image to zero to avoid uninitialized pixels
            for (int r = 0; r < 240; ++r) for (int c = 0; c < 80; ++c) lepton_image[r][c] = 0;
            for (int segmentId = 0; segmentId < segmentsCopy.size(); segmentId++) {
                const auto &segmentData = segmentsCopy[segmentId];

                // get package 20
                const auto *packetPtr = segmentData.data() + 20 * VOSPI_FRAME_SIZE;
                const auto header = VoISP::packet_id(packetPtr);
                const auto segmentId2 = VoISP::getSegmentNumber(header);
                if (segmentId2.has_value()) {
                    std::cout << "Segment: " << (int)segmentId2.value() << " " << segmentId << std::endl;
                } else {
                    std::cout << "Segment: (unknown) " << segmentId << std::endl;
                }


                for (int rowInSegment = 0; rowInSegment < BUFFER_VOSPI_FRAMES; rowInSegment++) {
                     const auto *packetPtr = segmentData.data() + rowInSegment * VOSPI_FRAME_SIZE;
                     // check if discard
                     bool isDiscard = VoISP::is_discard_packet(VoISP::packet_id(packetPtr));
                     if (isDiscard) {
                         continue;
                     }
                     const auto crcA = VoISP::packet_crc(packetPtr);
                     const auto crcC = VoISP::computeCRC(packetPtr, VOSPI_FRAME_SIZE);
                     bool isCRCValid = (crcA == crcC);

                    const auto packetNo = VoISP::getPacketNumber(VoISP::packet_id(packetPtr));
                    if (packetNo >= 60) continue; // ignore out-of-range packet numbers
                    const int row = static_cast<int>(packetNo) + 60 * segmentId;
                    if (row < 0 || row >= 240) continue;
                    const auto lineData = VoISP::GetImageLine(packetPtr);
                    for (int col = 0; col < 80; col++) {
                        lepton_image[row][col] = lineData[col];
                    }
                 }
             }
            std::cout << "Image received, preparing display..." << std::endl;

            // Create the PGM-style interleaved image that save_pgm_file produced:
            // PGM code wrote width=160, height=120 by writing pairs of rows (i and i+1)
            const int pgm_w = 160;
            const int pgm_h = 120;
            cv::Mat pgm_img(pgm_h, pgm_w, CV_16UC1);

            for (int i = 0; i < 240; i += 2) {
                int out_row = i / 2; // 0..119
                for (int j = 0; j < 80; ++j) {
                    // left half (0..79) comes from row i
                    pgm_img.at<uint16_t>(out_row, j) = static_cast<uint16_t>(lepton_image[i][j]);
                    // right half (80..159) comes from row i+1
                    pgm_img.at<uint16_t>(out_row, j + 80) = static_cast<uint16_t>(lepton_image[i + 1][j]);
                }
            }

            // Convert to 8-bit for display, scale using min/max like save_pgm_file does
            uint16_t minv = UINT16_MAX, maxv = 0;
            for (int r = 0; r < pgm_img.rows; ++r) {
                for (int c = 0; c < pgm_img.cols; ++c) {
                    uint16_t v = pgm_img.at<uint16_t>(r, c);
                    if (v > maxv) maxv = v;
                    if (v < minv) minv = v;
                }
            }
            if (minv == maxv) maxv = minv + 1;

            cv::Mat display_8u;
            // scale to 0..255
            pgm_img.convertTo(display_8u, CV_8U, 255.0 / (maxv - minv), - (minv * 255.0 / (maxv - minv)));

            // Also produce a small preview 120x60 by resizing the 160x120 down
            cv::Mat preview;
            cv::resize(display_8u, preview, cv::Size(120, 60), 0, 0, cv::INTER_AREA);

            static const std::string winName = "Lepton PGM-style";
            static const std::string previewWin = "Lepton preview 120x60";
            cv::imshow(winName, display_8u);
            cv::imshow(previewWin, preview);
            // wait a short time to allow window update (non-blocking)
            cv::waitKey(1);

            std::cout << "Displayed image (pgm-style) and preview." << std::endl;
         }
     });

    std::thread gpioThread([&]()
    {
        for (;;) {
            int packetsOk = 0;
            int packetsDiscarded = 0;
            int crcErrors = 0;
            while (1) {
                //read gpio
                if (GPIO_GetVsync()) {
                    break;
                }
            }
           GPIO_DebugSet(true);
           std::this_thread::sleep_for(std::chrono::microseconds(100));
           GPIO_DebugSet(false);
           const auto spiIoRet = ioctl(spiFd, SPI_IOC_MESSAGE(1), &tr);

            std::optional<uint8_t> segment = std::nullopt;
            for (int i =0; i < BUFFER_VOSPI_FRAMES; i++) {
                const auto packetPtr = rx_buf + i * VOSPI_FRAME_SIZE;
                const auto header = VoISP::packet_id(packetPtr);
                bool isDiscard = VoISP::is_discard_packet(header);
                if (isDiscard) {
                    packetsDiscarded++;
                    continue;
                }

                const auto crcA = VoISP::packet_crc(packetPtr);
                const auto crcC = VoISP::computeCRC(packetPtr, VOSPI_FRAME_SIZE);
                bool isCRCValid = (crcA == crcC);
                if (!isCRCValid) {
                    crcErrors++;
                }
                const auto packetNo = VoISP::getPacketNumber(header);
                std::cout << packetNo << " ";
                // get segment
                if (!segment.has_value()) {
                    segment = VoISP::getSegmentNumber(header);
                }
                if (segment == 0) {
                    std::unique_lock<std::mutex> lk(segmentsMtx);
                    segmentCount =0;
                }
                packetsOk++;
            }
            if (segment.has_value() && segment.value() > 0 && segment.value() <= 5) {
                std::unique_lock<std::mutex> lk(segmentsMtx);
                std::memcpy(segments[segment.value() - 1].data(), rx_buf, LEP_SPI_BUFFER);
                segmentCount++;
            }
            std::cout << "\n";
            std::cout << "Packets: " << packetsOk << " Discared " << packetsDiscarded << " crc errors " << crcErrors << " seg " << (int)segment.value_or(-1)  << "\n";
            std::unique_lock<std::mutex> lk(segmentsMtx);
            if (segmentCount == 4) {
                std::cout << "\nImage! " << std::endl;
                segmentCv.notify_one();
            }
       }
    });
    std::this_thread::sleep_for(std::chrono::seconds(3600));
}

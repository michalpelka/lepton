#include "lepton.h"

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

#include <sched.h>
#include <cstring>
#include <cerrno>

namespace lepton {
    Lepton::Lepton() {
    }

    Lepton::~Lepton() {
        shutdown();
    }

    bool Lepton::startI2c(const char *devicePath, int address) {
        // Open LEP port - keep using SDK call
        LEP_RESULT result = LEP_OpenPort(1, LEP_CCI_TWI, 400, &m_lepPort);
        if (result != LEP_OK) {
            std::cerr << "LEP_OpenPort failed: " << result << std::endl;
            return false;
        }
        return true;
    }

    bool Lepton::startSpi(const char *devicePath, uint32_t speed_hz) {
        m_spiDevice = devicePath;
        m_speed = speed_hz;
        spiFd = ::open(m_spiDevice.c_str(), O_RDWR);
        if (spiFd < 0) {
            std::cerr << "can't open spi device " << m_spiDevice << " errno=" << errno << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_WR_MODE, &m_mode) == -1) {
            std::cerr << "can't set spi mode" << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_RD_MODE, &m_mode) == -1) {
            std::cerr << "can't get spi mode" << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_WR_BITS_PER_WORD, &m_bits) == -1) {
            std::cerr << "can't set bits per word" << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_RD_BITS_PER_WORD, &m_bits) == -1) {
            std::cerr << "can't get bits per word" << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &m_speed) == -1) {
            std::cerr << "can't set max speed hz" << std::endl;
            return false;
        }
        if (ioctl(spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &m_speed) == -1) {
            std::cerr << "can't get max speed hz" << std::endl;
            return false;
        }

        std::cout << "spi mode: " << (int) m_mode << "\n";
        std::cout << "bits per word: " << (int) m_bits << "\n";
        std::cout << "max speed: " << m_speed << " Hz (" << (m_speed / 1000) << " KHz)\n";
        return true;
    }

    bool Lepton::initGpio(const char *chipName, int vsync, int debug) {
        m_gpioVsync = vsync;
        m_gpioDebug = debug;
        chip = gpiod_chip_open_by_name(chipName);
        if (!chip) {
            std::cerr << "Unable to open gpio chip " << chipName << std::endl;
            return false;
        }
        vs_line = gpiod_chip_get_line(chip, m_gpioVsync);
        if (!vs_line) {
            std::cerr << "Unable to get vsync line" << std::endl;
            return false;
        }
        if (gpiod_line_request_input(vs_line, "vsync-reader") < 0) {
            std::cerr << "Failed to request vsync input" << std::endl;
            return false;
        }
        dbg_line = gpiod_chip_get_line(chip, m_gpioDebug);
        if (!dbg_line) {
            std::cerr << "Unable to get debug line" << std::endl;
            return false;
        }
        if (gpiod_line_request_output(dbg_line, "debug-out", 0) < 0) {
            std::cerr << "Failed to request debug output" << std::endl;
            return false;
        }
        return true;
    }

    bool Lepton::GPIO_GetVsync() {
        int val = gpiod_line_get_value(vs_line);
        if (val < 0) return false;
        return val == 1;
    }

    void Lepton::GPIO_DebugSet(bool high) {
        if (dbg_line) gpiod_line_set_value(dbg_line, high ? 1 : 0);
    }

    void Lepton::capture() {
        if (m_running) return;
        m_running = true;
        uint8_t packet_number = 0;
        uint8_t segment = 0;
        uint8_t current_segment = 0;
        int packet = 0;
        int state = 0; //set to 1 when a valid segment is found
        int pixel = 0;

        const size_t VOSPI_FRAME_SIZE(164);
        const size_t BUFFER_VOSPI_FRAMES = 75;
        const size_t LEP_SPI_BUFFER = VOSPI_FRAME_SIZE * BUFFER_VOSPI_FRAMES;

        uint8_t rx_buf[LEP_SPI_BUFFER];

        bool ready = false; // shared state, protected by mtx
        uint8_t tx_dummy[LEP_SPI_BUFFER] = {0};
        struct spi_ioc_transfer tr = {
            .tx_buf = (unsigned long) tx_dummy,
            .rx_buf = (unsigned long) rx_buf,
            .len = LEP_SPI_BUFFER,
            .speed_hz = m_speed,
            .delay_usecs = 10,
            .bits_per_word = 8
        };


        using SegmentData = std::array<uint8_t, LEP_SPI_BUFFER>;
        std::array<SegmentData, 4> segments;
        int segmentCount = 0;
        std::mutex segmentsMtx;
        std::condition_variable segmentCv;

        std::thread savenetThread([&]() {
            for (;;) {
                std::array<SegmentData, 4> segmentsCopy{}; {
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
                    if (!segmentId2.has_value() || segmentId2.value() != segmentId + 1) {
                        // segment number mismatch
                        std::cerr << "Segment number mismatch: expected " << (segmentId + 1)
                                  << " got " << (segmentId2.has_value() ? std::to_string(segmentId2.value()) : "none")
                                  << std::endl;
                        continue;
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
                pgm_img.convertTo(display_8u, CV_8U, 255.0 / (maxv - minv), -(minv * 255.0 / (maxv - minv)));
                if (frameCallback) {
                    frameCallback(display_8u);
                }
            }
        });

        std::thread gpioThread([&]() {
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
                const auto VsyncUpRise = std::chrono::steady_clock::now();
                const auto GoodUntilNextVsync = VsyncUpRise  + std::chrono::milliseconds(8);
                GPIO_DebugSet(true);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                GPIO_DebugSet(false);
                const auto spiIoRet = ioctl(spiFd, SPI_IOC_MESSAGE(1), &tr);

                std::optional<uint8_t> segment = std::nullopt;
                for (int i = 0; i < BUFFER_VOSPI_FRAMES; i++) {

                    const auto *packetPtr = rx_buf + i * VOSPI_FRAME_SIZE;
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
                    // get segment
                    if (!segment.has_value()) {
                        segment = VoISP::getSegmentNumber(header);
                    }
                    if (segment == 0) {
                        std::unique_lock<std::mutex> lk(segmentsMtx);
                        segmentCount = 0;
                    }
                    packetsOk++;
                }
                if (segment.has_value() && segment.value() > 0 && segment.value() <= 5) {
                    std::unique_lock<std::mutex> lk(segmentsMtx);
                    std::memcpy(segments[segment.value() - 1].data(), rx_buf, LEP_SPI_BUFFER);
                    segmentCount++;
                }

                const auto ProcessingDone = std::chrono::steady_clock::now();
                const auto Delta = ProcessingDone - VsyncUpRise;
                std::cout << "SPI READ: OK: " << packetsOk << " Discared: " << packetsDiscarded << " CRC:" << crcErrors
                        << " SEG: " << (int) segment.value_or(-1) << "Proc time " << Delta.count() << "\n";
                std::unique_lock<std::mutex> lk(segmentsMtx);
                if (segmentCount == 4) {
                    segmentCv.notify_one();
                }
                GPIO_DebugSet(true);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                GPIO_DebugSet(false);
                // check if missed a deadline
                const auto now = std::chrono::steady_clock::now();
                if (now > GoodUntilNextVsync) {
                    std::cerr << "We missed vsync deadline\n";
                }else {
                    std::this_thread::sleep_until(GoodUntilNextVsync);
                }

            }
        });
        std::this_thread::sleep_for(std::chrono::seconds(3600));
    }

    bool Lepton::configureOemGpio() {
        // Configure OEM GPIO mode to VSYNC and set delay none as in testLepton
        LEP_RESULT result = LEP_SetOemGpioVsyncPhaseDelay(&m_lepPort, LEP_OEM_VSYNC_DELAY_NONE);
        if (result != LEP_OK) {
            std::cerr << "LEP_SetOemGpioVsyncPhaseDelay failed: " << result << std::endl;
            return false;
        }
        result = LEP_SetOemGpioMode(&m_lepPort, LEP_OEM_GPIO_MODE_VSYNC);
        if (result != LEP_OK) {
            std::cerr << "LEP_SetOemGpioMode failed: " << result << std::endl;
            return false;
        }
        return true;
    }

    bool Lepton::getCameraUptime(uint32_t &uptime) {
        if (LEP_GetSysCameraUpTime(&m_lepPort, &uptime) != LEP_OK) return false;
        return true;
    }

    void Lepton::shutdown() {
        if (!m_running) return;
        // m_running = false;
        // if (m_gpioThread.joinable()) m_gpioThread.join();
        // if (m_.joinable()) m_.join();
        // if (spiFd >= 0) {
        //     ::close(spiFd);
        //     spiFd = -1;
        // }
        // if (vs_line) {
        //     gpiod_line_release(vs_line);
        //     vs_line = nullptr;
        // }
        // if (dbg_line) {
        //     gpiod_line_release(dbg_line);
        //     dbg_line = nullptr;
        // }
        // if (chip) {
        //     gpiod_chip_close(chip);
        //     chip = nullptr;
        // }
        LEP_ClosePort(&m_lepPort);
    }
} // namespace lepton

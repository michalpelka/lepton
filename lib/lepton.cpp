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
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
// OpenCV display
#include <opencv2/opencv.hpp>

#include <sched.h>
#include <cstring>
#include <cerrno>
#include <deque>

namespace lepton {
    Lepton::Lepton() {
    }

    Lepton::~Lepton() {
        shutdown();
    }

    inline uint16_t swap16(uint16_t v)
    {
        return (v >> 8) | (v << 8);
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

    std::pair<cv::Mat, std::optional<Telemetry>> Lepton::ProcessDataSegmentsToMatU16(const std::vector<std::span<const uint8_t>>& segmentsToProcess, bool is_telemetry) {
        int const expected_lines = is_telemetry ? 244 : 240;
        int const expected_packets = is_telemetry ? 61 : 60;

        uint16_t lepton_image[expected_lines][80];
        // initialize image to zero to avoid uninitialized pixels
        for (int r = 0; r < expected_lines; ++r) for (int c = 0; c < 80; ++c) lepton_image[r][c] = 0;

        for (int segmentId = 0; segmentId < segmentsToProcess.size(); segmentId++) {
            const auto& segmentVec = segmentsToProcess[segmentId];
            const auto segmentPtr = segmentsToProcess[segmentId].data();

            // get package 20
            const auto *packetPtr = segmentPtr + 20 * Lepton::VOSPI_FRAME_SIZE;
            const auto header = VoISP::packet_id(packetPtr);
            const auto segmentId2 = VoISP::getSegmentNumber(header);
            if (!segmentId2.has_value() || segmentId2.value() != segmentId + 1) {
                // segment number mismatch
                std::cerr << "Segment number mismatch: expected " << (segmentId + 1)
                          << " got " << (segmentId2.has_value() ? std::to_string(segmentId2.value()) : "none")
                          << std::endl;
                continue;
            }
            const size_t packetsInSegment = segmentsToProcess[segmentId].size() / Lepton::VOSPI_FRAME_SIZE;
            if (packetsInSegment< BUFFER_VOSPI_FRAMES_MIN) {
                std::cerr << "Segment too small: expected at least " << BUFFER_VOSPI_FRAMES_MIN << " packets, got " << packetsInSegment << std::endl;
                continue;
            }
            for (int rowInSegment = 0; rowInSegment < expected_packets; rowInSegment++) {
                const auto *packetPtr = segmentPtr + rowInSegment * Lepton::VOSPI_FRAME_SIZE;
                // check if discard
                bool isDiscard = VoISP::is_discard_packet(VoISP::packet_id(packetPtr));
                if (isDiscard) {
                    continue;
                }
                const auto crcA = VoISP::packet_crc(packetPtr);
                const auto crcC = VoISP::computeCRC(packetPtr, Lepton::VOSPI_FRAME_SIZE);
                bool isCRCValid = (crcA == crcC);
                if (!isCRCValid) {
                    std::cerr << "CRC mismatch at segment " << segmentId << ", packet " << rowInSegment << std::endl;
                    continue;
                }
                const auto packetNo = VoISP::getPacketNumber(VoISP::packet_id(packetPtr));

                const int row = static_cast<int>(packetNo) + expected_packets * segmentId;
                if (row < 0 || row >= expected_lines) continue;
                const auto lineData = VoISP::GetImageLine(packetPtr);
                for (int col = 0; col < 80; col++) {
                    lepton_image[row][col] = lineData[col];
                }
            }
        }

        std::optional<Telemetry> telemetry;
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

        // deal with telemetry
        if (is_telemetry) {
            uint16_t rowA[80];
            uint16_t rowB[80];
            uint16_t rowC[80];
            memccpy(rowA, lepton_image[240], 1, sizeof(rowA));
            memccpy(rowB, lepton_image[241], 1, sizeof(rowB));
            memccpy(rowC, lepton_image[242], 1, sizeof(rowC));


            telemetry = parseTelemetry(rowA, rowB, rowC);
        }
        return {pgm_img,telemetry};
    }

    uint32_t make32(uint16_t hi, uint16_t lo)
    {
        return (static_cast<uint32_t>(hi) << 16) | lo;
    }

    Telemetry Lepton::parseTelemetry(const uint16_t* Araw, const uint16_t* Braw, const uint16_t* Craw)
    {


        Telemetry t{};

        // ==== ROW A ====
        t.A.revMajor = Araw[0] >> 8;
        t.A.revMinor = Araw[0] & 0xFF;

        t.A.timeCounterMs = make32(Araw[1], Araw[2]);
        t.A.statusBits    = make32(Araw[3], Araw[4]);

        std::memcpy(t.A.moduleSerial, &Araw[5], 8*sizeof(uint16_t));
        std::memcpy(t.A.softwareRevision, &Araw[13], 4*sizeof(uint16_t));

        t.A.frameCounter = make32(Araw[20], Araw[21]);
        t.A.frameMean = Araw[22];

        t.A.fpaTempCounts = Araw[23];
        t.A.fpaTempKelvinX100 = Araw[24];

        t.A.housingTempCounts = Araw[25];
        t.A.housingTempKelvinX100 = Araw[26];

        t.A.fpaTempLastFFCKelvinX100 = Araw[29];
        t.A.timeLastFFC = make32(Araw[30], Araw[31]);
        t.A.housingTempLastFFCKelvinX100 = Araw[32];

        t.A.agcRoiTop    = Araw[34];
        t.A.agcRoiLeft   = Araw[35];
        t.A.agcRoiBottom = Araw[36];
        t.A.agcRoiRight  = Araw[37];

        t.A.agcClipHigh = Araw[38];
        t.A.agcClipLow  = Araw[39];

        t.A.videoOutputFormatHi = Araw[72];
        t.A.videoOutputFormatLo = Araw[73];

        t.A.log2FfcFrames = Araw[74];

        // ==== ROW B ====
        t.B.emissivity = Braw[19];
        t.B.backgroundTempKelvinX100 = Braw[20];
        t.B.atmosphericTransmission = Braw[21];
        t.B.atmosphericTempKelvinX100 = Braw[22];
        t.B.windowTransmission = Braw[23];
        t.B.windowReflection = Braw[24];
        t.B.windowTempKelvinX100 = Braw[25];
        t.B.windowReflectedTempKelvinX100 = Braw[26];

        // ==== ROW C ====
        t.C.gainMode = Craw[5];
        t.C.effectiveGainMode = Craw[6];
        t.C.gainModeDesiredFlag = Craw[7];

        t.C.tempGainHighToLow_C = Craw[8];
        t.C.tempGainLowToHigh_C = Craw[9];
        t.C.tempGainHighToLow_K = Craw[10];
        t.C.tempGainLowToHigh_K = Craw[11];

        t.C.populationHighToLow = Craw[14];
        t.C.populationLowToHigh = Craw[15];

        t.C.roiStartRow = Craw[22];
        t.C.roiStartCol = Craw[23];
        t.C.roiEndRow   = Craw[24];
        t.C.roiEndCol   = Craw[25];

        t.C.tLinearEnabled = (Craw[48] != 0);
        t.C.tLinearResolution = Craw[49];

        t.C.spotMeanKelvin = Craw[50];
        t.C.spotMaxKelvin  = Craw[51];
        t.C.spotMinKelvin  = Craw[52];
        t.C.spotPopulation = Craw[53];
        t.C.spotRoiStartRow = Craw[54];
        t.C.spotRoiStartCol = Craw[55];

        return t;
    }

    inline double kx100_to_C(uint16_t kx100)
    {
        return (kx100 / 100.0) - 273.15;
    }

    inline double scaled8192(uint16_t v)
    {
        return v / 8192.0;
    }

    void Lepton::printTelemetry(const Telemetry& t)
    {
    std::cout << "================= LEPTON TELEMETRY =================\n";

    // -------- Row A --------
    std::cout << "\n[ Row A — System / Temps / AGC / FFC ]\n";

    std::cout << "Telemetry Rev: "
              << int(t.A.revMajor) << "."
              << int(t.A.revMinor) << "\n";

    std::cout << "Uptime: " << t.A.timeCounterMs << " ms\n";
    std::cout << "Frame #: " << t.A.frameCounter << "\n";
    std::cout << "Frame Mean: " << t.A.frameMean << "\n";

    std::cout << "\nTemperatures:\n";
    std::cout << "  FPA Temp:         "
              << kx100_to_C(t.A.fpaTempKelvinX100) << " °C\n";
    std::cout << "  Housing Temp:     "
              << kx100_to_C(t.A.housingTempKelvinX100) << " °C\n";
    std::cout << "  FPA Temp (last FFC): "
              << kx100_to_C(t.A.fpaTempLastFFCKelvinX100) << " °C\n";
    std::cout << "  Housing Temp (last FFC): "
              << kx100_to_C(t.A.housingTempLastFFCKelvinX100) << " °C\n";

    std::cout << "\nLast FFC at: " << t.A.timeLastFFC << " ms\n";

    std::cout << "\nAGC ROI: "
              << "(" << t.A.agcRoiTop << ", " << t.A.agcRoiLeft
              << ") → (" << t.A.agcRoiBottom << ", " << t.A.agcRoiRight << ")\n";

    std::cout << "AGC Clip Limits: High=" << t.A.agcClipHigh
              << "  Low=" << t.A.agcClipLow << "\n";

    std::cout << "Log2(FFC Frames): " << t.A.log2FfcFrames << "\n";


    // -------- Row B --------
    std::cout << "\n[ Row B — Radiometry Parameters ]\n";

    std::cout << "Emissivity:               "
              << scaled8192(t.B.emissivity) << "\n";

    std::cout << "Background Temp:          "
              << kx100_to_C(t.B.backgroundTempKelvinX100) << " °C\n";

    std::cout << "Atmospheric Transmission: "
              << scaled8192(t.B.atmosphericTransmission) << "\n";

    std::cout << "Atmospheric Temp:         "
              << kx100_to_C(t.B.atmosphericTempKelvinX100) << " °C\n";

    std::cout << "Window Transmission:      "
              << scaled8192(t.B.windowTransmission) << "\n";

    std::cout << "Window Reflection:        "
              << scaled8192(t.B.windowReflection) << "\n";

    std::cout << "Window Temp:              "
              << kx100_to_C(t.B.windowTempKelvinX100) << " °C\n";

    std::cout << "Window Reflected Temp:    "
              << kx100_to_C(t.B.windowReflectedTempKelvinX100) << " °C\n";


    // -------- Row C --------
    std::cout << "\n[ Row C — Gain / ROI / Spotmeter / TLinear ]\n";

    static const char* gainNames[] = {"High", "Low", "Auto"};

    std::cout << "Gain Mode:           "
              << gainNames[std::min<uint16_t>(t.C.gainMode,2)] << "\n";

    std::cout << "Effective Gain Mode: "
              << (t.C.effectiveGainMode==0 ? "High" : "Low") << "\n";

    std::cout << "Gain Switch Desired: "
              << (t.C.gainModeDesiredFlag ? "YES" : "No") << "\n";

    std::cout << "\nAuto Gain Thresholds (°C):\n";
    std::cout << "  High → Low: " << t.C.tempGainHighToLow_C << " °C\n";
    std::cout << "  Low → High: " << t.C.tempGainLowToHigh_C << " °C\n";

    std::cout << "\nPopulation Thresholds (% ROI area):\n";
    std::cout << "  High → Low: " << t.C.populationHighToLow << "%\n";
    std::cout << "  Low → High: " << t.C.populationLowToHigh << "%\n";

    std::cout << "\nGain Mode ROI: "
              << "(" << t.C.roiStartRow << ", " << t.C.roiStartCol
              << ") → (" << t.C.roiEndRow << ", " << t.C.roiEndCol << ")\n";

    std::cout << "\nTLinear: "
              << (t.C.tLinearEnabled ? "Enabled" : "Disabled")
              << "  Resolution: "
              << (t.C.tLinearResolution==0 ? "0.1 K" : "0.01 K")
              << "\n";

    std::cout << "\nSpotmeter:\n";
    std::cout << "  Mean:  " << (t.C.spotMeanKelvin/100.0) << " K\n";
    std::cout << "  Max:   " << (t.C.spotMaxKelvin/100.0) << " K\n";
    std::cout << "  Min:   " << (t.C.spotMinKelvin/100.0) << " K\n";
    std::cout << "  Pixels: " << t.C.spotPopulation << "\n";
    std::cout << "  ROI Start: (" << t.C.spotRoiStartRow
              << ", " << t.C.spotRoiStartCol << ")\n";

    std::cout << "\n====================================================\n";
}

    cv::Mat Lepton::ScaleToU8(const cv::Mat& pgm_img) {
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
        return display_8u;
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

        m_savenetThread = std::thread([&]() {
            for (;m_running;) {
                std::array<SegmentData, 4> segmentsThreadLocal;
                {
                    std::unique_lock<std::mutex> lk(segmentsMtx);
                    segmentCv.wait(lk, [&]() {
                        return segmentCount == 4;
                    });

                    std::swap(segmentsThreadLocal, segments);
                    segmentCount = 0;
                }

                if (frameRawData) {
                    std::vector<uint8_t> rawData;
                    for (const auto &segmentData : segmentsThreadLocal) {
                        rawData.insert(rawData.end(), segmentData.begin(), segmentData.end());
                    }
                    frameRawData(rawData);
                }
                std::vector<std::span<const uint8_t>> spans;
                for (const auto &segmentData : segmentsThreadLocal) {
                    spans.emplace_back(segmentData.data(), segmentData.size());
                }
                const auto [pgm_img,_] = ProcessDataSegmentsToMatU16(spans);

                if (frameCallbackNoScale) {
                    frameCallbackNoScale(pgm_img);
                }

                if (frameCallback) {
                    const auto display_8u = ScaleToU8(pgm_img);
                    frameCallback(display_8u);
                }

            }
        });

        m_gpioThread = std::thread ([&]() {
            for (;m_running;) {
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
        m_running = false;
         if (m_gpioThread.joinable()) m_gpioThread.join();
         if (m_gpioThread.joinable()) m_gpioThread.join();
        if (spiFd >= 0) {
            ::close(spiFd);
            spiFd = -1;
        }
        if (vs_line) {
            gpiod_line_release(vs_line);
            vs_line = nullptr;
        }
        if (dbg_line) {
            gpiod_line_release(dbg_line);
            dbg_line = nullptr;
        }
        if (chip) {
            gpiod_chip_close(chip);
            chip = nullptr;
        }
        LEP_ClosePort(&m_lepPort);
    }
} // namespace lepton

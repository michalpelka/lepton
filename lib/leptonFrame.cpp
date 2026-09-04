#include "leptonFrame.h"
#include "voisp.h"

#include <cstring>
#include <iostream>

namespace leptonframe {

    namespace {
        uint32_t make32(uint16_t hi, uint16_t lo)
        {
            return (static_cast<uint32_t>(hi) << 16) | lo;
        }

        Telemetry parseTelemetry(const uint16_t* Araw, const uint16_t* Braw, const uint16_t* Craw)
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
    } // namespace

    std::pair<cv::Mat, std::optional<Telemetry>> ProcessDataSegmentsToMatU16(
        const std::vector<std::span<const uint8_t>>& segmentsToProcess, bool is_telemetry)
    {
        int const expected_lines = is_telemetry ? 244 : 240;
        int const expected_packets = is_telemetry ? 61 : 60;

        uint16_t lepton_image[244][80];
        // initialize image to zero to avoid uninitialized pixels
        for (int r = 0; r < expected_lines; ++r) for (int c = 0; c < 80; ++c) lepton_image[r][c] = 0;

        for (int segmentId = 0; segmentId < static_cast<int>(segmentsToProcess.size()); segmentId++) {
            const auto segmentPtr = segmentsToProcess[segmentId].data();

            // get package 20
            const auto *packetPtr = segmentPtr + 20 * VOSPI_FRAME_SIZE;
            const auto header = VoISP::packet_id(packetPtr);
            const auto segmentId2 = VoISP::getSegmentNumber(header);
            if (!segmentId2.has_value() || segmentId2.value() != segmentId + 1) {
                // segment number mismatch
                std::cerr << "Segment number mismatch: expected " << (segmentId + 1)
                          << " got " << (segmentId2.has_value() ? std::to_string(segmentId2.value()) : "none")
                          << std::endl;
                continue;
            }
            const size_t packetsInSegment = segmentsToProcess[segmentId].size() / VOSPI_FRAME_SIZE;
            if (packetsInSegment < BUFFER_VOSPI_FRAMES_MIN) {
                std::cerr << "Segment too small: expected at least " << BUFFER_VOSPI_FRAMES_MIN << " packets, got " << packetsInSegment << std::endl;
                continue;
            }
            for (int rowInSegment = 0; rowInSegment < expected_packets; rowInSegment++) {
                const auto *rowPacketPtr = segmentPtr + rowInSegment * VOSPI_FRAME_SIZE;
                // check if discard
                bool isDiscard = VoISP::is_discard_packet(VoISP::packet_id(rowPacketPtr));
                if (isDiscard) {
                    continue;
                }
                const auto crcA = VoISP::packet_crc(rowPacketPtr);
                const auto crcC = VoISP::computeCRC(rowPacketPtr, VOSPI_FRAME_SIZE);
                bool isCRCValid = (crcA == crcC);
                if (!isCRCValid) {
                    std::cerr << "CRC mismatch at segment " << segmentId << ", packet " << rowInSegment << std::endl;
                    continue;
                }
                const auto packetNo = VoISP::getPacketNumber(VoISP::packet_id(rowPacketPtr));

                const int row = static_cast<int>(packetNo) + expected_packets * segmentId;
                if (row < 0 || row >= expected_lines) continue;
                const auto lineData = VoISP::GetImageLine(rowPacketPtr);
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
        return {pgm_img, telemetry};
    }

    cv::Mat ScaleToU8(const cv::Mat& pgm_img) {
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

    void printTelemetry(const Telemetry& t)
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
                  << gainNames[std::min<uint16_t>(t.C.gainMode, 2)] << "\n";

        std::cout << "Effective Gain Mode: "
                  << (t.C.effectiveGainMode == 0 ? "High" : "Low") << "\n";

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
                  << (t.C.tLinearResolution == 0 ? "0.1 K" : "0.01 K")
                  << "\n";

        std::cout << "\nSpotmeter:\n";
        std::cout << "  Mean:  " << (t.C.spotMeanKelvin / 100.0) << " K\n";
        std::cout << "  Max:   " << (t.C.spotMaxKelvin / 100.0) << " K\n";
        std::cout << "  Min:   " << (t.C.spotMinKelvin / 100.0) << " K\n";
        std::cout << "  Pixels: " << t.C.spotPopulation << "\n";
        std::cout << "  ROI Start: (" << t.C.spotRoiStartRow
                  << ", " << t.C.spotRoiStartCol << ")\n";

        std::cout << "\n====================================================\n";
    }

} // namespace leptonframe

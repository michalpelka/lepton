#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

// Portable Lepton VoSPI frame decoding: turns the raw byte segments received
// over USB-CDC from a Pico into a 16-bit thermal image + telemetry. No SPI,
// I2C, GPIO or Lepton-SDK dependency, so it builds on any platform OpenCV
// supports (used by the CDC viewers).
namespace leptonframe {

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

    constexpr size_t VOSPI_FRAME_SIZE = 164;
    constexpr size_t BUFFER_VOSPI_FRAMES_MIN = 60;

    // Decode 4 VoSPI segments (as received over CDC) into a 16-bit raw image
    // plus optional telemetry (when the last segment carries telemetry rows).
    std::pair<cv::Mat, std::optional<Telemetry>> ProcessDataSegmentsToMatU16(
        const std::vector<std::span<const uint8_t>>& segmentsToProcess, bool is_telemetry = false);

    // Scale a 16-bit raw image to 8-bit using its own min/max, for display.
    cv::Mat ScaleToU8(const cv::Mat& mat);

    void printTelemetry(const Telemetry& t);

} // namespace leptonframe

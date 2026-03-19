#include "lepton.h"
#include "voisp.h"

#include <SerialPort.h>
#include <SerialStream.h>
#include <SerialPort.h>
#include <iostream>
#include <vector>
#include <array>
#include <optional>
#include <chrono>
#include <thread>
#include <cstring>
#include "lepton.h"
#include "fstream"
#include <algorithm>


#include <opencv2/videoio.hpp>
static void writeFrameToVideo(const cv::Mat &frame) {
    static cv::VideoWriter writer;
    static bool writerExpectsColor = false;
    static bool writerOpened = false;

    if (!writerOpened) {
        std::ostringstream fname;
        fname << "lepton_" << std::time(nullptr) << ".avi";
        int fourcc = cv::VideoWriter::fourcc('M','J','P','G');
        cv::Size sz(frame.cols, frame.rows);
        bool isColor = frame.channels() != 1;

        if (!writer.open(fname.str(), fourcc, 30.0, sz, isColor)) {
            if (frame.channels() == 1) {
                cv::Mat bgr;
                cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
                if (!writer.open(fname.str(), fourcc, 30.0, sz, true)) {
                    std::cerr << "Failed to open VideoWriter\n";
                    return;
                }
                writerExpectsColor = true;
                writerOpened = true;
                writer.write(bgr);
                return;
            } else {
                std::cerr << "Failed to open VideoWriter\n";
                return;
            }
        }

        writerExpectsColor = isColor;
        writerOpened = true;
    }

    if (!writerOpened) return;

    if (frame.channels() == 1 && !writerExpectsColor) {
        // writer was opened for grayscale frames
        writer.write(frame);
    } else if (frame.channels() == 1) {
        // writer expects color, convert grayscale to BGR
        cv::Mat bgr;
        cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
        writer.write(bgr);
    } else {
        writer.write(frame);
    }
}

cv::Mat imgScaled;
cv::Mat imgRaw;
cv::Point mousePos(-1,-1);

void onMouse(int event, int ix, int iy, int, void*)
{
    if (event == cv::EVENT_MOUSEMOVE)
    {
        mousePos = cv::Point(ix, iy);
        // get temperature


    }
}


void processLinesBinary(const  std::vector<uint8_t> & blob) {
    bool isTelemetryEnabled = false;
    const int numberOfLines = blob.size() / VoISP::VoISPPacketSize;
    if (numberOfLines == 244) {
        isTelemetryEnabled = true;
    }

    int segmentCountSize = blob.size()/4;

    std::cout << "Segment count size: " << blob.size() << " lines " << numberOfLines << "\n";

    std::vector<std::span<const uint8_t>> spans;
    for (int i = 0; i < 4; ++i) {
        spans.emplace_back(blob.data() + i * segmentCountSize, segmentCountSize);
    }

    const auto [unscaled, telemetry] = lepton::Lepton::ProcessDataSegmentsToMatU16(spans, isTelemetryEnabled);
    cv::Mat scaled = lepton::Lepton::ScaleToU8(unscaled);

    cv::resize(unscaled, imgRaw, cv::Size(640, 480));
    cv::resize(scaled, imgScaled, cv::Size(640, 480));

    writeFrameToVideo(scaled);

    cv::Mat imgColor;
    cv::applyColorMap(imgScaled, imgColor, cv::COLORMAP_JET);

    // report pixel
    if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < imgRaw.cols && mousePos.y < imgRaw.rows)
    {
        uint16_t raw = imgRaw.at<uint16_t>(mousePos);

        double tempC = (raw * 0.01) - 273.15;

        // draw cross hair
        cv::line(imgColor, cv::Point(mousePos.x, 0), cv::Point(mousePos.x, imgScaled.rows-1), cv::Scalar(0, 255, 0), 1);
        cv::line(imgColor, cv::Point(0, mousePos.y), cv::Point(imgScaled.cols-1, mousePos.y), cv::Scalar(0, 255, 0), 1);

        // print temp
        cv::putText(imgColor, std::to_string(tempC), cv::Point(mousePos.x + 20, mousePos.y + 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0));
    }
    if (telemetry) {
        cv::putText(imgColor, "Uptime : " + std::to_string(telemetry->A.timeCounterMs), cv::Point(10,20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));

        cv::putText(imgColor, "Frame " + std::to_string(telemetry->A.frameCounter), cv::Point(10,40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));

        cv::putText(imgColor, "Housing " + std::to_string( float(telemetry->A.housingTempKelvinX100) / 100.0 - 273.0 ), cv::Point(10,60), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));
        cv::putText(imgColor, "FPA:  " + std::to_string( float(telemetry->A.fpaTempKelvinX100) / 100.0 - 273.15 ), cv::Point(10,80), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));
        cv::putText(imgColor, "Frame mean:  " + std::to_string( telemetry->A.frameMean), cv::Point(10,120), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255));

    }


    cv::imshow("Scaled", imgColor);
    cv::setMouseCallback("Scaled", onMouse, nullptr);
    cv::waitKey(1);

}
bool ends_with(const std::deque<uint8_t>& buffer,
               const std::vector<uint8_t>& pattern)
{
    if (pattern.size() > buffer.size())
        return false;

    return std::equal(
        pattern.rbegin(), pattern.rend(),
        buffer.rbegin()
    );
}

int main(int argc, char** argv)
{
    const char* dev = "/dev/serial/by-id/usb-Raspberry_Pi_Pico_E6611C08CB754E22-if00";


    std::cout << "Opening device: " << dev << "bps\n";
    LibSerial::SerialPort serial;
    try {
        serial.Open(dev, std::ios_base::in|std::ios_base::out);
        serial.SetBaudRate(LibSerial::BaudRate::BAUD_4000000);
    } catch (const std::exception &e) {
        std::cerr << "Failed to open serial device: " << e.what() << "\n";
        return 1;
    }

    std::deque<std::string> lines;
    int lastFrame = -1;
    auto lastFrameTs = std::chrono::high_resolution_clock::now();

    std::deque<uint8_t> buffer;
    const std::vector<uint8_t> headerStartBytes = {'L', 'E','P', 'T', 'O', 'N', ' '};
    for (;;) {
        try {
            char c;
            serial.ReadByte(c, 1000);
            buffer.push_back(c);

            if (ends_with(buffer, headerStartBytes))
            {
                // finish reading line
                std::string line;
                serial.ReadLine(line);
                std::cout << "Found header " << line << std::endl;

                int frameNo = 0;
                int dataSize = 0;
                std::string sizeWord;
                std::stringstream ss(line);
                ss >> frameNo;
                ss >> dataSize;

                std::vector<uint8_t> data;
                serial.Read(data, dataSize, 1000);

                std::cout << "Got frame " << frameNo << " size " << dataSize << "\n";



                if (frameNo > lastFrame && lastFrame != -1 ) {
                    std::cout << "Processing frame " << frameNo << std::endl;

                    processLinesBinary(data);

                    auto now = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> elapsed_seconds = now - lastFrameTs;
                    uint64_t bytes = 0;
                    bytes += dataSize + line.size();
                    double bps = double(bytes) / elapsed_seconds.count();
                    std::cout << "Elapsed time: " << elapsed_seconds.count() << "s kbps " << bps / 1024 << "\n";
                    lines.clear();
                    lastFrameTs = now;
                }
                //lines.push_back(line);
                lastFrame = frameNo;


            }
            if (buffer.size() > 128) {
                buffer.pop_front();
            }
            // std::string line;
            //
            // serial.ReadLine(line, 2);
            // std::string tag, sizeWord;
            //
            // int frameNo = 0;
            // std::stringstream ss(line);
            // ss >> tag;
            // ss >> frameNo;
            // ss >> sizeWord;
            // int dataSize = 0;
            // ss >> dataSize;
            // if (tag != "LEPTON" || sizeWord != "SIZE") {
            //     std::cerr << "Bad header: " << line << "\n";
            //     continue;
            // }
            // std::vector<uint8_t> data;
            // serial.Read(data, dataSize, 1000);
            //
            // std::cout << "Got frame " << frameNo << " size " << dataSize << "\n";
            //
            //
            //
            // if (frameNo > lastFrame && lastFrame != -1 ) {
            //     std::cout << "Processing frame " << frameNo << std::endl;
            //
            //     processLinesBinary(data);
            //
            //     auto now = std::chrono::high_resolution_clock::now();
            //     std::chrono::duration<double> elapsed_seconds = now - lastFrameTs;
            //     uint64_t bytes = 0;
            //     bytes += dataSize + line.size();
            //     double bps = double(bytes) / elapsed_seconds.count();
            //     std::cout << "Elapsed time: " << elapsed_seconds.count() << "s kbps " << bps / 1024 << "\n";
            //     lines.clear();
            //     lastFrameTs = now;
            // }
            // //lines.push_back(line);
            // lastFrame = frameNo;
        }
        catch (const std::exception &e) {
            std::cerr << "Error reading serial port: " << e.what() << "\n";
        }
    }
    return 0;
}

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
#include "base64/base64.h"

#include <algorithm>


void processLinesB64(const std::deque<std::string>& lines) {
    std::vector<std::vector<uint8_t>> data;
    data.resize(4);

    for (const auto line : lines) {
        std::stringstream ss(line);
        int frame, segment, lineno;
        std::string payload;

        ss >> frame >> segment >> lineno >> payload;
        const auto lineDataLen = b64d_size(payload.size()-1);
        std::vector<uint8_t> lineData;
        lineData.resize(lineDataLen);
        b64_decode((const unsigned char*) payload.c_str(), payload.size(), lineData.data());
        data[segment].insert(data[segment].end(), lineData.begin(), lineData.end());
    }
    // for (const auto textPtr : lines ) {
    //     const auto inputLen = textPtr.size();
    //     const auto inputDecodedLen = b64d_size(inputLen);
    //     std::vector<uint8_t> vec;
    //     vec.resize(inputDecodedLen);
    //     b64_decode((const unsigned char*) textPtr.c_str(), inputLen, vec.data());
    //     data.push_back(vec);
    //     spans.push_back(std::span<const uint8_t>(vec));
    //     std::cout << vec.size() << std::endl;
    // }
    for (const auto& segment : data) {
        std::cout << segment.size() << std::endl;
    }


    std::vector<std::span<const uint8_t>> spans;
    for (int i = 0; i < 4; ++i) {
        spans.emplace_back(data[i].data(), data[i].size());
    }

    cv::Mat unscaled = lepton::Lepton::ProcessDataSegmentsToMatU16(spans);
    cv::Mat scaled = lepton::Lepton::ScaleToU8(unscaled);
    cv::imshow("Foo", scaled);
    //cv::waitKey(0);


}

void processLinesBinary(const  std::vector<uint8_t> & blob) {

    int segmentCountSize = blob.size()/4;



    std::vector<std::span<const uint8_t>> spans;
    for (int i = 0; i < 4; ++i) {
        spans.emplace_back(blob.data() + i * segmentCountSize, segmentCountSize);
    }

    cv::Mat unscaled = lepton::Lepton::ProcessDataSegmentsToMatU16(spans);
    cv::Mat scaled = lepton::Lepton::ScaleToU8(unscaled);
    cv::imshow("Foo", scaled);
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
    return 0;
}

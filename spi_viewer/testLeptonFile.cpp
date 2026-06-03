#include <iostream>
#include <chrono>
#include <thread>
#include <sched.h>
#include <errno.h>

#include "lepton.h"
#include "fstream"
#include "base64/base64.h"

int main()
{

    // std::vector<uint8_t> data;
    // std::ifstream reader("frame0212.bin", std::ios::binary);
    // reader.seekg(0, std::ios::end);
    // data.resize(reader.tellg());
    // reader.seekg(0, std::ios::beg);
    // reader.read((char*)data.data(), data.size());
    //
    // // split to segments
    // const int segSize = data.size() / 4;
    // if (segSize * 4 != data.size()) {
    //     std::cerr << "Invalid file size," << std::endl;
    // }
    //
    // std::vector<std::span<const uint8_t>> spans;
    // for (int i = 0; i < 4; ++i) {
    //     spans.emplace_back(data.data() + i * segSize, segSize);
    // }


    std::ifstream reader("blob.txt"
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(reader, line)) {
        lines.push_back(line);
    }

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
    cv::waitKey(0);



    return 0;
}

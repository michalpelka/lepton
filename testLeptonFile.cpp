#include <iostream>
#include <chrono>
#include <thread>
#include <sched.h>
#include <errno.h>

#include "lepton.h"
#include "fstream"


int main()
{

    std::vector<uint8_t> data;
    std::ifstream reader("frame0212.bin", std::ios::binary);
    reader.seekg(0, std::ios::end);
    data.resize(reader.tellg());
    reader.seekg(0, std::ios::beg);
    reader.read((char*)data.data(), data.size());

    // split to segments
    const int segSize = data.size() / 4;
    if (segSize * 4 != data.size()) {
        std::cerr << "Invalid file size," << std::endl;
    }

    std::vector<std::span<const uint8_t>> spans;
    for (int i = 0; i < 4; ++i) {
        spans.emplace_back(data.data() + i * segSize, segSize);
    }

    cv::Mat unscaled = lepton::Lepton::ProcessDataSegmentsToMatU16(spans);
    cv::Mat scaled = lepton::Lepton::ScaleToU8(unscaled);
    cv::imwrite("frame0182.png", scaled);
    cv::imshow("Foo", scaled);
    cv::waitKey(0);



    return 0;
}

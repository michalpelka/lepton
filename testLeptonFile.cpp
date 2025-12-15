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
    std::ifstream reader("frame0182.bin", std::ios::binary);
    reader.seekg(0, std::ios::end);
    data.resize(reader.tellg());
    reader.seekg(0, std::ios::beg);
    reader.read((char*)data.data(), data.size());

    lepton::Lepton cam;

    const auto fun = [&](cv::Mat& img) {
        cv::imshow("Foo", img);
        cv::waitKey(1);

    };

    cam.setFrameCallback(fun);
    cam.processBlob(data.data(), data.size());
    return 0;
}

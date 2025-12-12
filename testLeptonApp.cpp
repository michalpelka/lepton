#include <iostream>
#include <chrono>
#include <thread>
#include <sched.h>
#include <errno.h>

#include "lepton.h"

constexpr int VSYNC_GPIO = 21;
constexpr int DEBUG_GPIO_OUT = 20;
static const char* SPI_DEVICE = "/dev/spidev0.0";
static uint32_t SPI_SPEED = 25000000;

void set_realtime_priority(int prio = 90)
{
    struct sched_param sp;
    sp.sched_priority = prio;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("sched_setscheduler");
    }
}

int main()
{
    lepton::Lepton cam;

    // try to set a modest realtime priority
    set_realtime_priority(20);

    if (!cam.initGpio("gpiochip0", VSYNC_GPIO, DEBUG_GPIO_OUT)) {
        std::cerr << "Failed to init GPIO" << std::endl;
        return 1;
    }

    if (!cam.startSpi(SPI_DEVICE, SPI_SPEED)) {
        std::cerr << "Failed to start SPI" << std::endl;
        return 1;
    }

    if (!cam.startI2c()) {
        std::cerr << "Failed to start I2C (LEP)" << std::endl;
        return 1;
    }

    if (!cam.configureOemGpio()) {
        std::cerr << "Failed to configure OEM GPIO" << std::endl;
        return 1;
    }

    // print a few uptime samples
    for (int i = 0; i < 5; ++i) {
        uint32_t uptime = 0;
        if (!cam.getCameraUptime(uptime)) {
            std::cerr << "Unable to get uptime." << std::endl;
            return 1;
        }
        std::cout << "Uptime: " << uptime << " seconds\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    cv::VideoWriter writer(
        "output.mp4",
        cv::VideoWriter::fourcc('m','p','4','v'),  // codec
        10,
        cv::Size(180, 120)
    );

    const auto fun = [&](cv::Mat& img) {
        cv::imshow("Foo", img);
        cv::waitKey(1);
        writer.write(img);

    };
    cam.setFrameCallback(fun);

    cam.capture();

    // keep process alive to let capture run (or replace with a more graceful loop)
    std::this_thread::sleep_for(std::chrono::minutes(1));

    cam.shutdown();
    return 0;
}

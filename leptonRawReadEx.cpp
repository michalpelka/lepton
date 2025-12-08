


#include <chrono>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <limits.h>
#include <ostream>
#include <string.h>
#include <time.h>
#include <iostream>
#include <thread>
#include <iomanip>
#include "voisp.h"
#include <gpiod.h>
#include <fstream>
#include <optional>
#include <deque>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
    perror(s);
    abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode = 3;
static uint8_t bits = 8;
static uint32_t speed = 2000000;
static uint16_t delay = 0;
static uint8_t status_bits = 0;

int8_t last_packet = -1;

#define VOSPI_FRAME_SIZE (164)
#define LEP_SPI_BUFFER (118080) //(118080)39360
struct gpiod_chip *chip = nullptr;
struct gpiod_line *cs_line = nullptr;
#define CS_GPIO 21
void cs_init()
{
    chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) pabort("gpiod_chip_open_by_name");
    cs_line = gpiod_chip_get_line(chip, CS_GPIO);
    if (!cs_line) pabort("gpiod_get_line");
    if (gpiod_line_request_output(cs_line, "lepton_cs", 1) < 0) // 1 => deassert (HIGH)
        pabort("gpiod_line_request_output");
}

void cs_release()
{
    if (cs_line) gpiod_line_release(cs_line);
    if (chip) gpiod_chip_close(chip);
}

inline void cs_deassert() { gpiod_line_set_value(cs_line, 1); } // /CS = HIGH
inline void cs_assert()   { gpiod_line_set_value(cs_line, 0); } // /CS = LOW

int main(int argc, char *argv[])
{
    int ret = 0;
    int spi_fd;

    spi_fd = open(device, O_RDWR);
    if (spi_fd < 0)
    {
        pabort("can't open device");
    }

    ret = ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    if (ret == -1)
    {
        pabort("can't set spi mode");
    }

    ret = ioctl(spi_fd, SPI_IOC_RD_MODE, &mode);
    if (ret == -1)
    {
        pabort("can't get spi mode");
    }

    ret = ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        pabort("can't set bits per word");
    }

    ret = ioctl(spi_fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
    if (ret == -1)
    {
        pabort("can't get bits per word");
    }

    ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        pabort("can't set max speed hz");
    }

    ret = ioctl(spi_fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
    if (ret == -1)
    {
        pabort("can't get max speed hz");
    }
    cs_init();

    printf("spi mode: %d\n", mode);
    printf("bits per word: %d\n", bits);
    printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);



    int dataRecieved = 0;
    int valid = 0;
    int crc_errors = 0;
    std::thread t([&]() {
        for (;;) {
            std::cout << "Received data : " << dataRecieved << " bytes" << std::endl;
            std::cout << "Valid packets : " << valid << std::endl;
            std::cout << "CRC Errors   : " << crc_errors << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    std::deque<std::array<uint8_t, VoISP::VoISPPacketSize>> rollingBuffer;
    while (1)
    {

        std::optional<uint8_t> segment = 0;
        // read from spi
        std::array<uint8_t, VoISP::VoISPPacketSize> buffer{};
        if (read(spi_fd, buffer.data(), VoISP::VoISPPacketSize) == VoISP::VoISPPacketSize) {
            const auto packetId = VoISP::packet_id(buffer.data());
             bool isDiscard = VoISP::is_discard_packet(packetId);
            if (isDiscard) {
                continue;
            }
            auto crcA = VoISP::packet_crc(buffer.data());
            auto crcC = VoISP::computeCRC(buffer.data(), VoISP::VoISPPacketSize);
            bool isCRCValid = (crcA == crcC)  ;
            if (isCRCValid) {
                valid++;
            }else {
                crc_errors++;
            }
            const uint16_t packetNo = VoISP::getPacketNumber(packetId);
        }

        //
        // if (ret>0) {
        //     const auto packetId = VoISP::packet_id(buffer);
        //     bool isDiscard = VoISP::is_discard_packet(packetId);
        //     if (isDiscard) {
        //         continue;
        //     }
        //     const uint16_t packetNo = VoISP::getPacketNumber(packetId);
        //
        //
        //     const auto crcA = VoISP::packet_crc(buffer);
        //     const auto crcC = VoISP::computeCRC(buffer, VOSPI_FRAME_SIZE);
        //
        //     bool isCRCValid = (crcA == crcC)  ;
        //     if (isCRCValid) {
        //         dataRecieved+= VOSPI_FRAME_SIZE;
        //
        //         std::array<uint8_t, VoISP::VoISPPacketSize> out{};
        //         std::copy(buffer, buffer+VoISP::VoISPPacketSize, out.begin());
        //         rollingBuffer.emplace_back(out);
        //         if (rollingBuffer.size() > 100) {
        //             rollingBuffer.pop_front();
        //         }
        //         if (packetNo == 20) {
        //             // get segment
        //             segment = VoISP::getSegmentNumber(packetId);
        //             std::cout << "Segment: " << (int)*segment << "\n";
        //         }
        //     }
        //
        //     // try to get segment
        //
        //     if (!isCRCValid) {
        //         std::cout << "CRC ERROR" << "\n";
        //         segment = std::nullopt;
        //         cs_deassert();
        //         std::this_thread::sleep_for(std::chrono::milliseconds(200));
        //         cs_assert();
        //     }
        // }
    }

    close(spi_fd);
    cs_release();
    return 0;
}

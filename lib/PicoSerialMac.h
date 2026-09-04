#pragma once
// Minimal POSIX/termios USB-CDC serial reader for macOS, standing in for
// LibSerial (which isn't packaged for macOS and pulls in Boost/Doxygen/Python
// when built from source). Only implements the small subset of
// LibSerial::SerialPort used by LeptonCDC::run(): Open, SetBaudRate, ReadByte,
// ReadLine, Read — each throwing PicoSerialMac::TimeoutException (a
// std::runtime_error) on timeout, matching LibSerial's exception-based
// timeout behavior so the call sites don't need to change.
#include <chrono>
#include <cerrno>
#include <cstring>
#include <ios>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

#include <IOKit/serial/ioss.h>

namespace picoserial {

    class TimeoutException : public std::runtime_error
    {
    public:
        TimeoutException() : std::runtime_error("Serial read timed out") {}
    };

    class SerialPort
    {
    public:
        ~SerialPort()
        {
            if(m_fd >= 0)
                ::close(m_fd);
        }

        void Open(const std::string& device, std::ios_base::openmode = std::ios_base::in | std::ios_base::out)
        {
            m_fd = ::open(device.c_str(), O_RDWR | O_NOCTTY);
            if(m_fd < 0)
                throw std::runtime_error("Failed to open " + device + ": " + std::strerror(errno));

            termios tty{};
            if(tcgetattr(m_fd, &tty) != 0)
                throw std::runtime_error("tcgetattr failed on " + device + ": " + std::strerror(errno));

            cfmakeraw(&tty);
            tty.c_cc[VMIN] = 0;
            tty.c_cc[VTIME] = 0; // non-blocking; timeouts handled via poll()

            if(tcsetattr(m_fd, TCSANOW, &tty) != 0)
                throw std::runtime_error("tcsetattr failed on " + device + ": " + std::strerror(errno));
        }

        // CDC-ACM is a virtual serial port over USB bulk endpoints; the Pico
        // firmware doesn't actually rate-limit on this value, but macOS still
        // requires a valid speed to be set. IOSSIOSPEED accepts arbitrary
        // integer baud rates (unlike the fixed Bxxxx termios constants).
        void SetBaudRate(int baud)
        {
            speed_t speed = static_cast<speed_t>(baud);
            if(ioctl(m_fd, IOSSIOSPEED, &speed) != 0)
                throw std::runtime_error(std::string("IOSSIOSPEED failed: ") + std::strerror(errno));
        }

        void ReadByte(char& c, unsigned timeout_ms)
        {
            pollfd pfd{m_fd, POLLIN, 0};
            const int rc = poll(&pfd, 1, static_cast<int>(timeout_ms));
            if(rc <= 0 || !(pfd.revents & POLLIN))
                throw TimeoutException();
            const ssize_t n = ::read(m_fd, &c, 1);
            if(n != 1)
                throw std::runtime_error("Serial read error");
        }

        void ReadLine(std::string& line, char terminator = '\n', unsigned timeout_ms = 1000)
        {
            line.clear();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            for(;;)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if(remaining.count() <= 0)
                    throw TimeoutException();
                char c;
                ReadByte(c, static_cast<unsigned>(remaining.count()));
                if(c == terminator)
                    return;
                line.push_back(c);
            }
        }

        void Read(std::vector<uint8_t>& data, size_t numBytes, unsigned timeout_ms)
        {
            data.clear();
            data.reserve(numBytes);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            while(data.size() < numBytes)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if(remaining.count() <= 0)
                    throw TimeoutException();
                char c;
                ReadByte(c, static_cast<unsigned>(remaining.count()));
                data.push_back(static_cast<uint8_t>(c));
            }
        }

    private:
        int m_fd = -1;
    };

} // namespace picoserial

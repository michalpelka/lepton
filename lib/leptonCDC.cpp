#include "leptonCDC.h"

#if defined(__linux__)
#include <SerialPort.h>
#else
#include "PicoSerialMac.h"
#endif

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <sstream>

#if defined(__APPLE__)
#include <glob.h>
#endif

static bool ends_with(const std::deque<uint8_t>& buffer, const std::vector<uint8_t>& pattern)
{
	if(pattern.size() > buffer.size())
		return false;
	return std::equal(pattern.rbegin(), pattern.rend(), buffer.rbegin());
}

std::string LeptonCDC::simplifyPicoName(const std::string& dev)
{
	// Input:  /dev/serial/by-id/usb-Raspberry_Pi_Pico_E6611C08CB754E22-if00
	// Output: E6611C08CB754E22
	const std::string filename = std::filesystem::path(dev).filename().string();
	const std::string prefix = "usb-Raspberry_Pi_Pico_";
	const auto start = filename.find(prefix);
	if(start == std::string::npos)
		return filename; // fallback: return as-is
	const auto nameStart = start + prefix.size();
	const auto ifPos = filename.rfind("-if");
	if(ifPos == std::string::npos || ifPos <= nameStart)
		return filename.substr(nameStart);
	return filename.substr(nameStart, ifPos - nameStart);
}

#if defined(__linux__)
std::vector<std::string> LeptonCDC::listPicos()
{
	std::vector<std::string> result;
	const std::filesystem::path byId("/dev/serial/by-id");
	if(!std::filesystem::exists(byId))
		return result;
	for(const auto& entry : std::filesystem::directory_iterator(byId))
	{
		const std::string name = entry.path().filename().string();
		if(name.find("usb-Raspberry_Pi_Pico") != std::string::npos)
			result.push_back(entry.path().string());
	}
	std::sort(result.begin(), result.end());
	return result;
}
#elif defined(__APPLE__)
std::vector<std::string> LeptonCDC::listPicos()
{
	// macOS has no by-id symlinks; a Pico's CDC-ACM interface shows up as
	// /dev/cu.usbmodemXXXX. There's no cheap way to further filter these to
	// "is actually a Pico" without linking IOKit, so treat every usbmodem
	// device as a candidate (use --device to be explicit if others are attached).
	std::vector<std::string> result;
	glob_t g{};
	if(glob("/dev/cu.usbmodem*", 0, nullptr, &g) == 0)
	{
		for(size_t i = 0; i < g.gl_pathc; ++i)
			result.emplace_back(g.gl_pathv[i]);
	}
	globfree(&g);
	std::sort(result.begin(), result.end());
	return result;
}
#else
std::vector<std::string> LeptonCDC::listPicos()
{
	std::cerr << "LeptonCDC::listPicos: auto-discovery not implemented on this platform; use --device" << std::endl;
	return {};
}
#endif

bool LeptonCDC::open(const std::string& device)
{
	m_device = device;
	return true;
}

void LeptonCDC::run()
{
#if defined(__linux__)
	LibSerial::SerialPort serial;
#else
	picoserial::SerialPort serial;
#endif
	try
	{
		serial.Open(m_device, std::ios_base::in | std::ios_base::out);
#if defined(__linux__)
		serial.SetBaudRate(LibSerial::BaudRate::BAUD_4000000);
#else
		serial.SetBaudRate(4000000);
#endif
	}
	catch(const std::exception& e)
	{
		std::cerr << "LeptonCDC: failed to open " << m_device << ": " << e.what() << std::endl;
		return;
	}
	std::cout << "LeptonCDC: opened " << m_device << std::endl;

	std::deque<uint8_t> buffer;
	const std::vector<uint8_t> headerMagic = {'L', 'E', 'P', 'T', 'O', 'N', ' '};
	int lastFrame = -1;

	for(;;)
	{
		try
		{
			char c;
			serial.ReadByte(c, 1000);
			buffer.push_back(static_cast<uint8_t>(c));

			if(ends_with(buffer, headerMagic))
			{
				std::string line;
				serial.ReadLine(line);

				int frameNo = 0;
				int dataSize = 0;
				std::stringstream ss(line);
				ss >> frameNo >> dataSize;

				std::vector<uint8_t> data;
				serial.Read(data, dataSize, 1000);

				if(frameNo > lastFrame && lastFrame != -1 && m_callback)
				{
					const int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
										   std::chrono::system_clock::now().time_since_epoch())
										   .count();
					m_callback(frameNo, ts, data);
				}

				lastFrame = frameNo;
				buffer.clear();
			}

			if(buffer.size() > 128)
				buffer.pop_front();
		}
		catch(const std::exception& e)
		{
			std::cerr << "LeptonCDC: read error: " << e.what() << std::endl;
		}
	}
}
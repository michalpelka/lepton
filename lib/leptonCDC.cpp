#include "leptonCDC.h"

#include <SerialPort.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iostream>
#include <sstream>

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

bool LeptonCDC::open(const std::string& device)
{
	m_device = device;
	return true;
}

void LeptonCDC::run()
{
	LibSerial::SerialPort serial;
	try
	{
		serial.Open(m_device, std::ios_base::in | std::ios_base::out);
		serial.SetBaudRate(LibSerial::BaudRate::BAUD_4000000);
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
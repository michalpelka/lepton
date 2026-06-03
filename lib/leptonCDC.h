#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Reads LEPTON frames from a Raspberry Pi Pico USB CDC serial port.
///
/// Frame protocol (sent by the Pico):
///   "LEPTON " <frameNo> <dataSize>\n
///   <dataSize raw bytes of VoSPI data>
///
/// Usage:
///   LeptonCDC cdc;
///   cdc.open(LeptonCDC::listPicos().front());
///   cdc.setFrameCallback([](int frameNo, int64_t tsNs, const std::vector<uint8_t>& blob) { ... });
///   cdc.run();  // blocks forever
class LeptonCDC
{
public:
	using FrameCallback =
		std::function<void(int frameNo, int64_t timestampNs, const std::vector<uint8_t>& blob)>;

	/// Returns paths of all Raspberry Pi Pico CDC serial devices found in /dev/serial/by-id/
	static std::vector<std::string> listPicos();

	/// Simplify nameof pico board from
	/// `dev/serial/by-id/usb-Raspberry_Pi_Pico_E6614810CB887334-if00`
	/// to E6614810CB887334
	static std::string simplifyPicoName(const std::string& dev);


	/// Open and configure the serial port. Returns false on failure.
	bool open(const std::string& device);

	void setFrameCallback(FrameCallback cb) { m_callback = std::move(cb); }

	/// Blocking read loop — calls the frame callback for each complete received frame.
	void run();

private:
	std::string m_device;
	FrameCallback m_callback;
};
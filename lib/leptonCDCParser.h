#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Push-based parser for the Pico CDC frame protocol (see leptonCDC.h):
///   "LEPTON " <frameNo> <dataSize>\n
///   <dataSize raw bytes of VoSPI data>
///
/// Unlike LeptonCDC::run(), which pulls bytes from a serial port with blocking
/// reads, this one is fed whatever chunks the transport delivers (e.g. Android
/// USB bulk transfers) and fires the callback once a whole frame is assembled.
/// Behaves like LeptonCDC: the first frame and non-increasing frame numbers
/// are dropped.
class LeptonCDCParser
{
public:
	using FrameCallback = std::function<void(int frameNo, const std::vector<uint8_t>& blob)>;

	void setFrameCallback(FrameCallback cb) { m_callback = std::move(cb); }

	void feed(const uint8_t* data, size_t len);

private:
	enum class State
	{
		SearchMagic, // scanning for "LEPTON "
		HeaderLine,  // collecting "<frameNo> <dataSize>" up to '\n'
		Payload,     // collecting dataSize bytes
	};

	void onHeaderLine();

	FrameCallback m_callback;
	State m_state = State::SearchMagic;
	size_t m_magicMatched = 0;
	std::string m_line;
	std::vector<uint8_t> m_payload;
	size_t m_payloadSize = 0;
	int m_frameNo = 0;
	int m_lastFrame = -1;
};

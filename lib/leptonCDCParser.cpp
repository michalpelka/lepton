#include "leptonCDCParser.h"

#include <algorithm>
#include <sstream>

namespace {
constexpr char kMagic[] = "LEPTON ";
constexpr size_t kMagicLen = sizeof(kMagic) - 1;
constexpr size_t kMaxLineLen = 64;
constexpr size_t kMaxPayload = 1 << 20; // a real frame is ~40 KB
} // namespace

void LeptonCDCParser::feed(const uint8_t* data, size_t len)
{
	size_t i = 0;
	while(i < len)
	{
		switch(m_state)
		{
		case State::SearchMagic:
		{
			const char c = static_cast<char>(data[i++]);
			if(c == kMagic[m_magicMatched])
				++m_magicMatched;
			else
				// "LEPTON " has no repeated prefix, so on a mismatch the only
				// possible restart is this byte being the first 'L'.
				m_magicMatched = (c == kMagic[0]) ? 1 : 0;

			if(m_magicMatched == kMagicLen)
			{
				m_magicMatched = 0;
				m_line.clear();
				m_state = State::HeaderLine;
			}
			break;
		}
		case State::HeaderLine:
		{
			const char c = static_cast<char>(data[i++]);
			if(c == '\n')
				onHeaderLine();
			else if(m_line.size() < kMaxLineLen)
				m_line.push_back(c);
			else
				m_state = State::SearchMagic; // garbage, resync
			break;
		}
		case State::Payload:
		{
			const size_t n = std::min(len - i, m_payloadSize - m_payload.size());
			m_payload.insert(m_payload.end(), data + i, data + i + n);
			i += n;
			if(m_payload.size() == m_payloadSize)
			{
				if(m_frameNo > m_lastFrame && m_lastFrame != -1 && m_callback)
					m_callback(m_frameNo, m_payload);
				m_lastFrame = m_frameNo;
				m_state = State::SearchMagic;
			}
			break;
		}
		}
	}
}

void LeptonCDCParser::onHeaderLine()
{
	int frameNo = 0;
	long dataSize = 0;
	std::istringstream ss(m_line);
	if(!(ss >> frameNo >> dataSize) || dataSize <= 0 || static_cast<size_t>(dataSize) > kMaxPayload)
	{
		m_state = State::SearchMagic;
		return;
	}
	m_frameNo = frameNo;
	m_payloadSize = static_cast<size_t>(dataSize);
	m_payload.clear();
	m_payload.reserve(m_payloadSize);
	m_state = State::Payload;
}

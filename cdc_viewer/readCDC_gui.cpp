#include "leptonCDC.h"
#include "leptonFrame.h"
#include "voisp.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>

// --------------------------------------------------------------------------
// Per-Pico display state (written by worker thread, read by main thread)
// --------------------------------------------------------------------------
struct PicoViewer
{
	std::string windowName;
	std::string device;

	std::mutex mutex;
	cv::Mat displayFrame; // 8-bit BGR colormap, ready to imshow
	cv::Mat rawFrame;     // 16-bit uint16 resized to 640x480, for mouse temp readout
	cv::Point mousePos{-1, -1};

	cv::VideoWriter writer;
	bool writerOpened = false;
};

// --------------------------------------------------------------------------
// Build a colored display frame from raw uint16 + telemetry + mouse position
// --------------------------------------------------------------------------
static cv::Mat buildDisplayFrame(const cv::Mat& raw16,
								 const std::optional<leptonframe::Telemetry>& telemetry,
								 cv::Point mousePos)
{
	cv::Mat scaled640, raw640;
	cv::resize(leptonframe::ScaleToU8(raw16), scaled640, cv::Size(640, 480));
	cv::resize(raw16, raw640, cv::Size(640, 480));

	cv::Mat color;
	cv::applyColorMap(scaled640, color, cv::COLORMAP_JET);

	// Mouse crosshair + temperature
	if(mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < color.cols && mousePos.y < color.rows)
	{
		uint16_t raw = raw640.at<uint16_t>(mousePos);
		double tempC = raw * 0.01 - 273.15;

		cv::line(color, {mousePos.x, 0}, {mousePos.x, color.rows - 1}, {0, 255, 0}, 1);
		cv::line(color, {0, mousePos.y}, {color.cols - 1, mousePos.y}, {0, 255, 0}, 1);
		cv::putText(color, std::to_string(tempC) + " C",
					{mousePos.x + 20, mousePos.y + 20},
					cv::FONT_HERSHEY_SIMPLEX, 0.5, {0, 255, 0});
	}

	// Telemetry overlay
	if(telemetry)
	{
		cv::putText(color, "Uptime : " + std::to_string(telemetry->A.timeCounterMs),
					{10, 20}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 255, 255});
		cv::putText(color, "Frame  : " + std::to_string(telemetry->A.frameCounter),
					{10, 40}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 255, 255});
		cv::putText(color, "Housing: " + std::to_string(float(telemetry->A.housingTempKelvinX100) / 100.0f - 273.0f) + " C",
					{10, 60}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 255, 255});
		cv::putText(color, "FPA    : " + std::to_string(float(telemetry->A.fpaTempKelvinX100) / 100.0f - 273.15f) + " C",
					{10, 80}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 255, 255});
		cv::putText(color, "Mean   : " + std::to_string(telemetry->A.frameMean),
					{10, 100}, cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 255, 255});
	}

	return color;
}

// --------------------------------------------------------------------------
// Worker thread for one Pico
// --------------------------------------------------------------------------
static void picoWorker(const std::string& device, PicoViewer* viewer)
{
	LeptonCDC cdc;
	cdc.open(device);

	cdc.setFrameCallback([viewer](int /*frameNo*/, int64_t /*ts*/, const std::vector<uint8_t>& blob) {
		const bool isTelemetry = (blob.size() / VoISP::VoISPPacketSize == 244);
		const int segSize = static_cast<int>(blob.size()) / 4;

		std::vector<std::span<const uint8_t>> spans;
		for(int i = 0; i < 4; ++i)
			spans.emplace_back(blob.data() + i * segSize, segSize);

		const auto [unscaled, telemetry] = leptonframe::ProcessDataSegmentsToMatU16(spans, isTelemetry);

		cv::Point mp;
		{
			std::lock_guard<std::mutex> lck(viewer->mutex);
			mp = viewer->mousePos;
		}

		cv::Mat display = buildDisplayFrame(unscaled, telemetry, mp);

		// Lazy video writer init
		if(!viewer->writerOpened)
		{
			std::ostringstream fname;
			fname << "lepton_" << viewer->device.back() << "_" << std::time(nullptr) << ".avi";
			viewer->writer.open(fname.str(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
								30.0, display.size(), true);
			viewer->writerOpened = viewer->writer.isOpened();
		}
		if(viewer->writerOpened)
			viewer->writer.write(display);

		cv::Mat raw640;
		cv::resize(unscaled, raw640, cv::Size(640, 480));

		std::lock_guard<std::mutex> lck(viewer->mutex);
		viewer->displayFrame = std::move(display);
		viewer->rawFrame = std::move(raw640);
	});

	cdc.run(); // blocks
}

// --------------------------------------------------------------------------
// main
// --------------------------------------------------------------------------
int main(int argc, char** argv)
{
	std::vector<std::string> devices;

	for(int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if(arg == "--device" && i + 1 < argc)
			devices.push_back(argv[++i]);
		else if(arg == "--help" || arg == "-h")
		{
			std::cout << "Usage: " << argv[0] << " [--device <path>]\n"
					  << "  Auto-discovers all connected Raspberry Pi Picos if no --device given.\n";
			return 0;
		}
	}

	if(devices.empty())
	{
		devices = LeptonCDC::listPicos();
		if(devices.empty())
		{
			std::cerr << "No Raspberry Pi Pico found." << std::endl;
			return 1;
		}
	}

	std::cout << "Found " << devices.size() << " Pico(s)" << std::endl;

	// Create per-pico viewers and windows
	std::vector<std::unique_ptr<PicoViewer>> viewers;
	for(size_t i = 0; i < devices.size(); ++i)
	{
		auto v = std::make_unique<PicoViewer>();
		v->device = devices[i];
		v->windowName = "Lepton [" + std::to_string(i) + "] " + devices[i];
		cv::namedWindow(v->windowName, cv::WINDOW_AUTOSIZE);
		cv::setMouseCallback(v->windowName, [](int event, int x, int y, int, void* ud) {
			if(event == cv::EVENT_MOUSEMOVE)
				static_cast<PicoViewer*>(ud)->mousePos = {x, y};
		}, v.get());
		viewers.push_back(std::move(v));
	}

	// Launch one worker thread per Pico
	std::vector<std::thread> threads;
	for(size_t i = 0; i < devices.size(); ++i)
		threads.emplace_back(picoWorker, devices[i], viewers[i].get());

	// Main thread owns all imshow/waitKey (OpenCV requirement)
	while(true)
	{
		for(auto& v : viewers)
		{
			cv::Mat frame;
			{
				std::lock_guard<std::mutex> lck(v->mutex);
				if(!v->displayFrame.empty())
					frame = v->displayFrame.clone();
			}
			if(!frame.empty())
				cv::imshow(v->windowName, frame);
		}
		if(cv::waitKey(1) == 27) // ESC to quit
			break;
	}

	return 0;
}
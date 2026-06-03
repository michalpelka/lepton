#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "ExtrasUtils.h"
#include "lepton.h"
#include "leptonCDC.h"
#include "voisp.h"
#include <nlohmann/json.hpp>
struct CameraConfig {
	double rateMs = 500;
};

namespace global
{
	CameraConfig config;
	std::mutex stateMutex;
	std::string state;
	std::string continousScanTarget;
	bool isContinousScanRunning()
	{
		std::lock_guard<std::mutex> lck(stateMutex);
		return state == mandeye::extras::keys::MODE_SCANNING;
	}

	std::string getContinousScanTarget()
	{
		std::lock_guard<std::mutex> lck(stateMutex);
		return continousScanTarget;
	}
} // namespace global

void zmqThread()
{
	mandeye::extras::startZeroMQListener([](const nlohmann::json& j) {
		std::lock_guard<std::mutex> lck(global::stateMutex);
		if(j.contains(mandeye::extras::keys::MODE))
			global::state = j[mandeye::extras::keys::MODE].get<std::string>();
		if(j.contains(mandeye::extras::keys::CONTINUOUS_SCAN_DIRECTORY))
			global::continousScanTarget = j[mandeye::extras::keys::CONTINUOUS_SCAN_DIRECTORY].get<std::string>();
	});
}

void picoThread(const std::string& device, int picoIdx)
{
	std::future<void> saveThread;
	const std::string tag = "LEPTON_" + LeptonCDC::simplifyPicoName(device);
	auto lastSaved = std::chrono::steady_clock::time_point{};

	LeptonCDC cdc;
	cdc.open(device);
	cdc.setFrameCallback([&](int frameNo, int64_t timestampNs, const std::vector<uint8_t>& blob) {
		if(!global::isContinousScanRunning())
			return;

		const auto now = std::chrono::steady_clock::now();
		const double minInterval = global::config.rateMs / 1000.0;
		if(std::chrono::duration<double>(now - lastSaved).count() < minInterval)
			return;

		bool prevSaved =
			!saveThread.valid() ||
			saveThread.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
		if(!prevSaved)
		{
			std::cerr << "[" << tag << "] Previous frame not saved yet - skipping frame " << frameNo
					  << std::endl;
			return;
		}

		const bool isTelemetry = (blob.size() / VoISP::VoISPPacketSize == 244);
		const int segSize = static_cast<int>(blob.size()) / 4;

		std::vector<std::span<const uint8_t>> spans;
		for(int i = 0; i < 4; ++i)
			spans.emplace_back(blob.data() + i * segSize, segSize);

		const auto [unscaled, telemetry] = lepton::Lepton::ProcessDataSegmentsToMatU16(spans, isTelemetry);

		const std::filesystem::path directory(global::getContinousScanTarget() + "/" + tag);
		std::filesystem::create_directories(directory);

		cv::Mat frameCopy = unscaled.clone();

		lastSaved = now;
		saveThread = std::async(std::launch::async, [=]() {
			try
			{
				const std::string base =
					(directory / std::to_string(timestampNs)).string();

				// 16-bit PNG — preserves full radiometric data (CV_16UC1)
				cv::imwrite(base + ".png", frameCopy);

				// 8-bit PNG — JET colormap for visual inspection
				cv::Mat jet;
				cv::applyColorMap(lepton::Lepton::ScaleToU8(frameCopy), jet, cv::COLORMAP_JET);
				cv::imwrite(base + "_visual.png", jet);

				// Metadata
				{
					nlohmann::json meta;
					meta["timestamp_ns"] = timestampNs;
					meta["frame"] = frameNo;
					meta["device"] = device;
					meta["width"] = frameCopy.cols;
					meta["height"] = frameCopy.rows;
					meta["dtype"] = "uint16";
					if(telemetry)
					{
						meta["fpa_temp_c"] = float(telemetry->A.fpaTempKelvinX100) / 100.0f - 273.15f;
						meta["housing_temp_c"] =
							float(telemetry->A.housingTempKelvinX100) / 100.0f - 273.15f;
						meta["frame_counter"] = telemetry->A.frameCounter;
						meta["uptime_ms"] = telemetry->A.timeCounterMs;
					}
					std::ofstream metaFile(base + ".meta.json");
					metaFile << meta.dump(4);
				}

				std::cout << "[" << tag << "] Saved frame " << frameNo << " -> " << base << std::endl;
			}
			catch(const std::exception& e)
			{
				std::cerr << "[" << tag << "] Failed to save frame: " << e.what() << std::endl;
			}
		});
	});

	cdc.run(); // blocks
}



std::optional<CameraConfig> getCameraConfig(const std::string &filename)
{
	std::ifstream file(filename);
	if(!file.is_open())
	{
		std::cerr << "Failed to open camera config file: " << filename << std::endl;
		return std::nullopt;
	}
	try {
		auto config = nlohmann::json::parse(file);
		CameraConfig cameraConfig;
		cameraConfig.rateMs = config["rateMs"].get<double>();
		return cameraConfig;
	}
	catch(const nlohmann::json::exception& e) {
		std::cerr << "Failed to parse camera config file: " << filename << " - " << e.what() << std::endl;
		return std::nullopt;
	}

	return std::nullopt;
}

void createCameraConfig(const std::string &filename, const CameraConfig &config) {
	nlohmann::json j;
	j["rateMs"] = config.rateMs;

	std::ofstream file(filename);
	if(!file.is_open())
	{
		std::cerr << "Failed to create camera config file: " << filename << std::endl;
		return;
	}
	file << j.dump(4);
	std::cout << "Created default camera config file: " << filename << std::endl;
}

int main(int argc, char** argv)
{
	std::vector<std::string> devices;
	const std::string LeptonConfig = "/media/usb/lepton_config.json";
	for(int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if(arg == "--device" && i + 1 < argc)
			devices.push_back(argv[++i]);
		else if(arg == "--list")
		{
			auto picos = LeptonCDC::listPicos();
			if(picos.empty())
				std::cout << "No Raspberry Pi Pico devices found." << std::endl;
			for(const auto& p : picos)
				std::cout << p << std::endl;
			return 0;
		}
		else if(arg == "--help" || arg == "-h")
		{
			std::cout << "Usage: " << argv[0] << " [--device <path>] [--fps <n>] [--list]\n"
					  << "  --device <path>  Add a specific device (can be repeated)\n"
					  << "  --list           List detected Raspberry Pi Pico devices and exit\n";
			return 0;
		}
	}
	const auto loadedConfig = getCameraConfig(LeptonConfig);
	if(!loadedConfig) {
		//default config path
		global::config = CameraConfig{500};
		createCameraConfig(LeptonConfig, global::config);
	}



	if(devices.empty())
	{
		devices = LeptonCDC::listPicos();
		if(devices.empty())
		{
			std::cerr << "No Raspberry Pi Pico found. Use --device to specify one." << std::endl;
			return 1;
		}
	}


	std::cout << "Starting " << devices.size() << " lepton thread(s)" << std::endl;

	std::thread zmq(zmqThread);

	std::vector<std::thread> threads;
	for(size_t i = 0; i < devices.size(); ++i)
	{
		std::cout << "  [" << i << "] " << devices[i] << std::endl;
		threads.emplace_back(picoThread, devices[i], static_cast<int>(i));
	}

	for(auto& t : threads)
		t.join();

	zmq.join();
	return 0;
}

// JNI bridge for the Android viewer: Kotlin reads USB bulk data and pushes it
// in here; we parse the Pico CDC stream, decode frames with leptonframe and
// draw them into the Activity's Surface (replaces cv::imshow on desktop).
//
// Threads: the USB reader thread only parses (cheap) and hands complete frames
// to a render thread, so USB reads are never held up by decoding. If decoding
// falls behind, the newest frame replaces the pending one (counted as dropped).

#include "leptonCDCParser.h"
#include "leptonFrame.h"
#include "voisp.h"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#define LOG_TAG "LeptonViewer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int kDisplayW = 640;
constexpr int kDisplayH = 480;

struct Viewer
{
	LeptonCDCParser parser;

	// Hand-off from the reader thread to the render thread.
	std::mutex frameMutex;
	std::condition_variable frameReady;
	std::vector<uint8_t> pendingFrame;
	bool hasPending = false;
	bool quit = false;
	std::thread renderThread;

	std::mutex mutex; // guards window and recording state
	ANativeWindow* window = nullptr;
	std::filesystem::path recordDir; // empty = not recording
	cv::VideoWriter writer;
	std::chrono::steady_clock::time_point lastSnapshot{};

	std::atomic<int> cursorX{-1};
	std::atomic<int> cursorY{-1};

	// Stats for the status bar.
	std::atomic<int64_t> bytesReceived{0};
	std::atomic<int64_t> framesReceived{0};
	std::atomic<int64_t> framesShown{0};
	std::atomic<int64_t> framesDropped{0};
	std::atomic<int64_t> lastProcessUs{0};

	Viewer()
	{
		parser.setFrameCallback([this](int, const std::vector<uint8_t>& blob) { onFrame(blob); });
		renderThread = std::thread([this] { renderLoop(); });
	}

	~Viewer()
	{
		{
			std::lock_guard<std::mutex> lck(frameMutex);
			quit = true;
		}
		frameReady.notify_one();
		renderThread.join();
		if(window)
			ANativeWindow_release(window);
	}

	// Reader thread.
	void onFrame(const std::vector<uint8_t>& blob)
	{
		++framesReceived;
		{
			std::lock_guard<std::mutex> lck(frameMutex);
			if(hasPending)
				++framesDropped;
			pendingFrame.assign(blob.begin(), blob.end());
			hasPending = true;
		}
		frameReady.notify_one();
	}

	void renderLoop()
	{
		std::vector<uint8_t> blob;
		for(;;)
		{
			{
				std::unique_lock<std::mutex> lck(frameMutex);
				frameReady.wait(lck, [this] { return hasPending || quit; });
				if(quit)
					return;
				std::swap(blob, pendingFrame);
				hasPending = false;
			}
			const auto t0 = std::chrono::steady_clock::now();
			process(blob);
			lastProcessUs = std::chrono::duration_cast<std::chrono::microseconds>(
								std::chrono::steady_clock::now() - t0)
								.count();
			++framesShown;
		}
	}

	void process(const std::vector<uint8_t>& blob)
	{
		// Same decoding as readCDC_gui: 4 equal VoSPI segments, telemetry
		// when each segment carries 61 instead of 60 packets.
		const bool isTelemetry = (blob.size() / VoISP::VoISPPacketSize == 244);
		const int segSize = static_cast<int>(blob.size()) / 4;
		std::vector<std::span<const uint8_t>> spans;
		for(int i = 0; i < 4; ++i)
			spans.emplace_back(blob.data() + i * segSize, segSize);

		const auto [raw16, telemetry] = leptonframe::ProcessDataSegmentsToMatU16(spans, isTelemetry);
		const cv::Mat display =
			leptonframe::RenderDisplayFrame(raw16, telemetry, {cursorX.load(), cursorY.load()});

		std::lock_guard<std::mutex> lck(mutex);
		if(!recordDir.empty())
			record(raw16, display);
		if(window)
			draw(display);
	}

	void record(const cv::Mat& raw16, const cv::Mat& display)
	{
		if(writer.isOpened())
			writer.write(display);

		// Same as desktop --record: every 1 s a raw 16-bit PNG + JET visual.
		const auto now = std::chrono::steady_clock::now();
		if(now - lastSnapshot >= std::chrono::seconds(1))
		{
			lastSnapshot = now;
			const auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count();
			const std::string base = (recordDir / std::to_string(ts)).string();
			cv::imwrite(base + ".png", raw16);
			cv::Mat jet;
			cv::applyColorMap(leptonframe::ScaleToU8(raw16), jet, cv::COLORMAP_JET);
			cv::imwrite(base + "_visual.png", jet);
		}
	}

	void draw(const cv::Mat& bgr)
	{
		ANativeWindow_Buffer buf;
		if(ANativeWindow_lock(window, &buf, nullptr) != 0)
			return;
		// Wrap the window's pixel buffer (stride is in pixels) and convert
		// OpenCV's BGR straight into it.
		cv::Mat dst(buf.height, buf.width, CV_8UC4, buf.bits, static_cast<size_t>(buf.stride) * 4);
		if(bgr.cols == buf.width && bgr.rows == buf.height)
			cv::cvtColor(bgr, dst, cv::COLOR_BGR2RGBA);
		ANativeWindow_unlockAndPost(window);
	}
};

Viewer* fromHandle(jlong handle)
{
	return reinterpret_cast<Viewer*>(handle);
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeCreate(JNIEnv*, jobject)
{
	return reinterpret_cast<jlong>(new Viewer());
}

JNIEXPORT void JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeDestroy(JNIEnv*, jobject, jlong handle)
{
	delete fromHandle(handle);
}

JNIEXPORT void JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeFeed(JNIEnv* env, jobject, jlong handle,
																		  jobject directBuffer, jint len)
{
	// Direct ByteBuffer: the USB request wrote straight into this memory, so
	// no copy between Java and C++.
	auto* bytes = static_cast<uint8_t*>(env->GetDirectBufferAddress(directBuffer));
	if(!bytes || len <= 0)
		return;
	Viewer* v = fromHandle(handle);
	v->bytesReceived += len;
	v->parser.feed(bytes, static_cast<size_t>(len));
}

JNIEXPORT void JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeSetSurface(JNIEnv* env, jobject, jlong handle,
																				jobject surface)
{
	Viewer* v = fromHandle(handle);
	std::lock_guard<std::mutex> lck(v->mutex);
	if(v->window)
	{
		ANativeWindow_release(v->window);
		v->window = nullptr;
	}
	if(surface)
	{
		v->window = ANativeWindow_fromSurface(env, surface);
		// The compositor scales this 640x480 buffer to the view's size.
		ANativeWindow_setBuffersGeometry(v->window, kDisplayW, kDisplayH, WINDOW_FORMAT_RGBA_8888);
	}
}

JNIEXPORT void JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeSetCursor(JNIEnv*, jobject, jlong handle,
																			   jint x, jint y)
{
	fromHandle(handle)->cursorX = x;
	fromHandle(handle)->cursorY = y;
}

JNIEXPORT jboolean JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeStartRecording(JNIEnv* env, jobject,
																						jlong handle, jstring dir)
{
	Viewer* v = fromHandle(handle);
	const char* cdir = env->GetStringUTFChars(dir, nullptr);
	const std::filesystem::path path(cdir);
	env->ReleaseStringUTFChars(dir, cdir);

	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	if(ec)
		return JNI_FALSE;

	std::lock_guard<std::mutex> lck(v->mutex);
	// ~9 fps: the Lepton's unique-frame rate (desktop uses 30, which plays fast).
	v->writer.open((path / "lepton.avi").string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 9.0,
				   cv::Size(kDisplayW, kDisplayH), true);
	v->recordDir = path;
	v->lastSnapshot = {};
	LOGI("Recording to %s (video %s)", path.c_str(), v->writer.isOpened() ? "ok" : "FAILED");
	return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeStopRecording(JNIEnv*, jobject, jlong handle)
{
	Viewer* v = fromHandle(handle);
	std::lock_guard<std::mutex> lck(v->mutex);
	v->writer.release();
	v->recordDir.clear();
}

// [bytesReceived, framesReceived, framesShown, framesDropped, lastProcessUs]
JNIEXPORT jlongArray JNICALL Java_com_lepton_cdcviewer_NativeViewer_nativeStats(JNIEnv* env, jobject, jlong handle)
{
	Viewer* v = fromHandle(handle);
	const jlong stats[] = {v->bytesReceived.load(), v->framesReceived.load(), v->framesShown.load(),
						   v->framesDropped.load(), v->lastProcessUs.load()};
	jlongArray out = env->NewLongArray(5);
	env->SetLongArrayRegion(out, 0, 5, stats);
	return out;
}

} // extern "C"

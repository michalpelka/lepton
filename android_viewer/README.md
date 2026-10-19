# Android CDC viewer

Android port of `cdc_viewer/readCDC_gui`: plug a Pico running `pico_lepton_cdc`
into the phone via USB OTG and see the live thermal image. Tap to read the
temperature under the crosshair; **Record** saves an MJPG video plus, every
second, a raw 16-bit PNG and a JET visual PNG (same as desktop `--record`).

## How it fits together

```
Kotlin                                  C++ (app/src/main/cpp + ../lib)
──────                                  ───────────────────────────────
PicoUsb: UsbManager, claim CDC          native_viewer.cpp (JNI)
interfaces, raise DTR, bulkTransfer ──► LeptonCDCParser   "LEPTON n size\n" + payload
                                        leptonframe::ProcessDataSegmentsToMatU16
                                        leptonframe::RenderDisplayFrame
ThermalView (SurfaceView)  ◄──────────  ANativeWindow draw (replaces cv::imshow)
Record button ───────────────────────►  cv::VideoWriter / cv::imwrite
```

Android apps can't open `/dev/ttyACM*`, so instead of termios the app talks
USB CDC-ACM itself via the USB host API. The firmware only sends once the host
raises DTR (`tud_cdc_connected()`), which `PicoUsb.open()` does explicitly.

## Build

Needs JDK 17 and the Android SDK (platform 34, NDK 26.3.11579264, CMake
3.22.1), e.g. via Android Studio, or:

```sh
brew install openjdk@17 && brew install --cask android-commandlinetools
sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" \
           "ndk;26.3.11579264" "cmake;3.22.1"
```

Then:

```sh
./fetch_opencv.sh                       # OpenCV Android SDK -> third_party/
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug                 # -> app/build/outputs/apk/debug/app-debug.apk
```

## Install and debug

The phone's single USB port is needed for the Pico, so use adb over Wi-Fi:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk   # once, over the cable
adb tcpip 5555                                             # Android 9/10
adb connect <phone-ip>:5555                                # unplug cable, plug Pico
adb logcat -s LeptonViewer                                 # app logs
adb pull /sdcard/Android/data/com.lepton.cdcviewer/files/  # recordings
```

On Android 11+ you can instead pair via *Developer options → Wireless
debugging*.

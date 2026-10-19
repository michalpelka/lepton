#!/bin/sh
# Downloads the prebuilt OpenCV Android SDK into third_party/ (gitignored).
set -e
cd "$(dirname "$0")"
VERSION=4.10.0
if [ -d third_party/OpenCV-android-sdk ]; then
    echo "OpenCV Android SDK already present"
    exit 0
fi
mkdir -p third_party
curl -L -o third_party/opencv.zip \
    "https://github.com/opencv/opencv/releases/download/${VERSION}/opencv-${VERSION}-android-sdk.zip"
unzip -q third_party/opencv.zip -d third_party
rm third_party/opencv.zip

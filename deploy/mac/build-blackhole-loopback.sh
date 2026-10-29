#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# BlackHole's device 2 reads and writes device 1's buffer, so each driver carries one direction and device 2 gets the opposite flags.

set -eu

src=$1
out=$2
identity=${3:--}

build() {
  name=$1 bundleId=$2 factory=$3 mirror=$4 input=$5 output=$6
  work="$out/work-$name"
  xcodebuild -quiet -project "$src/BlackHole.xcodeproj" -configuration Release -target BlackHole \
    SYMROOT="$work" OBJROOT="$work" CONFIGURATION_BUILD_DIR="$work" \
    ARCHS="arm64 x86_64" ONLY_ACTIVE_ARCH=NO \
    PRODUCT_BUNDLE_IDENTIFIER="$bundleId" CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM= \
    GCC_PREPROCESSOR_DEFINITIONS="\$GCC_PREPROCESSOR_DEFINITIONS kNumber_Of_Channels=2 kHas_Driver_Name_Format=false \
kPlugIn_BundleID=\\\"$bundleId\\\" kDriver_Name=\\\"$name\\\" kManufacturer_Name=\\\"Deskflow\\\" \
kDevice_Name=\\\"BlackHole\\ Loopback\\\" kDevice2_Name=\\\"$mirror\\\" kDevice2_IsHidden=true \
kDevice_HasInput=$input kDevice_HasOutput=$output kDevice2_HasInput=$output kDevice2_HasOutput=$input" \
    >"$out/$name.log" 2>&1 || { cat "$out/$name.log"; exit 1; }
  sed -i '' "s/e395c745-4eea-4d94-bb92-46224221047c/$factory/" "$work/BlackHole.driver/Contents/Info.plist"
  rm -rf "$out/$name.driver"
  mv "$work/BlackHole.driver" "$out/$name.driver"
  rm -rf "$work" "$out/$name.log"
  if [ "$identity" = "-" ]; then
    codesign --force --sign - "$out/$name.driver"
  else
    codesign --force --options runtime --timestamp --sign "$identity" "$out/$name.driver"
  fi
}

mkdir -p "$out"
build DeskflowLoopbackOut org.deskflow.loopback.out 6f1c2a3e-8d4b-4f5a-9c7e-1b2d3e4f5a60 "Deskflow\\ Sound\\ Capture" false true
build DeskflowLoopbackIn org.deskflow.loopback.in 0a9b8c7d-6e5f-4a3b-8c2d-1e0f9a8b7c61 "Deskflow\\ Mic\\ Feed" true false
cp "$src/LICENSE" "$out/LICENSE-BlackHole"

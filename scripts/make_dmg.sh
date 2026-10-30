#!/usr/bin/env bash
# Builds RubDuck for other Macs and packages it as a disk image
# holding an installer (.pkg) and a read-me:
#   dist/RubDuck-<version>.dmg
#
# The installer puts the AU and the VST3 in /Library/Audio/Plug-Ins and the
# standalone app in /Applications. Binaries are universal (Apple Silicon and
# Intel) and run on macOS 11 or later. They are only signed ad hoc (no Developer
# ID yet), so Gatekeeper blocks the installer once: packaging/LISEZ-MOI.txt,
# copied into the image, explains how to allow it.
#
# Builds in build-dist/ (the development build in build/ stays as it is and
# nothing is copied into ~/Library/Audio/Plug-Ins).
#
# Usage: scripts/make_dmg.sh

set -euo pipefail

cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(RubberDuckRevival VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="RubDuck"
ID="com.samkieffer.rubduck"
MIN_MACOS="11.0"
BUILD_DIR="build-dist"
DIST_DIR="dist"
WORK="${DIST_DIR}/work"
STAGE="${WORK}/stage"
ARTEFACTS="${BUILD_DIR}/plugin/RubberDuckRevival_artefacts/Release"
DMG="${DIST_DIR}/RubDuck-${VERSION}.dmg"

# The macOS 27 SDK of the Command Line Tools does not link with Xcode's ld
# ("unknown architecture arm64e.x1"): build against Xcode's SDK unless SDKROOT
# is already set.
if [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun --sdk macosx --show-sdk-path)
    export SDKROOT
fi

echo "==> Configuring ${BUILD_DIR} (arm64 + x86_64, macOS ${MIN_MACOS}+, version ${VERSION})"
# Reuse the JUCE sources the development build already fetched, if any.
juce_source=()
if [ -d build/_deps/juce-src ]; then
    juce_source=(-DFETCHCONTENT_SOURCE_DIR_JUCE="${PWD}/build/_deps/juce-src")
fi
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" \
    -DRD_INSTALL_PLUGINS=OFF \
    ${juce_source[@]+"${juce_source[@]}"} >/dev/null

echo "==> Building the AU, the VST3 and the standalone app"
cmake --build "$BUILD_DIR" --config Release -j "$(sysctl -n hw.ncpu)" \
    --target RubberDuckRevival_AU RubberDuckRevival_VST3 RubberDuckRevival_Standalone

AU="${ARTEFACTS}/AU/${NAME}.component"
VST3="${ARTEFACTS}/VST3/${NAME}.vst3"
APP="${ARTEFACTS}/Standalone/${NAME}.app"

echo "==> Signing the bundles ad hoc"
# JUCE seals the AU and the app only when it copies the plugins into ~/Library
# (off here); Apple Silicon needs every bundle signed, at least ad hoc.
for bundle in "$AU" "$VST3" "$APP"; do
    codesign --force --sign - "$bundle"
done

echo "==> Checking the bundles"
for bundle in "$AU" "$VST3" "$APP"; do
    binary="${bundle}/Contents/MacOS/${NAME}"
    archs=$(lipo -archs "$binary")
    if [[ "$archs" != *arm64* || "$archs" != *x86_64* ]]; then
        echo "${binary}: built for '${archs}', not universal" >&2
        exit 1
    fi
    for arch in arm64 x86_64; do
        minos=$(vtool -arch "$arch" -show-build "$binary" | awk '/minos/ { print $2 }')
        if [ "$minos" != "$MIN_MACOS" ]; then
            echo "${binary} (${arch}): needs macOS ${minos}, expected ${MIN_MACOS}" >&2
            exit 1
        fi
    done
    # Apple Silicon only loads signed code: the ad hoc signature must be intact.
    codesign --verify --strict "$bundle"
    echo "    $(basename "$bundle"): ${archs}, macOS ${MIN_MACOS}+, signature OK"
done

echo "==> Building the installer"
# Detach a volume left mounted by a previous run, or hdiutil fails later.
for volume in "/Volumes/${NAME} "*; do
    if [ -d "$volume" ]; then
        hdiutil detach "$volume" -force >/dev/null 2>&1 || true
    fi
done
rm -rf "$DIST_DIR"
mkdir -p "$WORK/packages" "$WORK/resources" "$STAGE"

# pkgbuild on macOS 27 prints "write: Permission denied" a few times although
# the package comes out whole (same payload, signature intact): drop that line.
pkgbuild_filtered() {
    local status=0
    # Only stderr goes through grep; stdout stays on stdout (fd 3).
    { pkgbuild "$@" 2>&1 1>&3 | { grep -v '^write: Permission denied$' >&2 || true; }; } 3>&1 || status=$?
    return "$status"
}

# One component package per format, so the installer can leave some out.
make_package() {
    local part="$1" bundle="$2" location="$3"
    local root="${WORK}/root-${part}"
    mkdir -p "$root"
    ditto "$bundle" "${root}/$(basename "$bundle")"
    # Not relocatable: otherwise Installer may "update" a copy of the bundle it
    # finds elsewhere on the disk (a build folder...) instead of installing it.
    pkgbuild_filtered --analyze --root "$root" "${WORK}/${part}.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "${WORK}/${part}.plist"
    pkgbuild_filtered --quiet --root "$root" --component-plist "${WORK}/${part}.plist" \
        --identifier "${ID}.${part}" --version "$VERSION" \
        --install-location "$location" "${WORK}/packages/${part}.pkg"
}
make_package au "$AU" "/Library/Audio/Plug-Ins/Components"
make_package vst3 "$VST3" "/Library/Audio/Plug-Ins/VST3"
make_package app "$APP" "/Applications"

fill() {
    sed -e "s/@VERSION@/${VERSION}/g" -e "s/@ID@/${ID}/g" "$1" > "$2"
}
fill packaging/distribution.xml "${WORK}/distribution.xml"
fill packaging/welcome.html "${WORK}/resources/welcome.html"
fill packaging/conclusion.html "${WORK}/resources/conclusion.html"
fill packaging/LISEZ-MOI.txt "${STAGE}/LISEZ-MOI.txt"

productbuild --quiet --distribution "${WORK}/distribution.xml" \
    --resources "${WORK}/resources" --package-path "${WORK}/packages" \
    "${STAGE}/Installer ${NAME}.pkg"

echo "==> Building the disk image"
hdiutil create -quiet -volname "${NAME} ${VERSION}" -srcfolder "$STAGE" \
    -ov -format UDZO "$DMG"
rm -rf "$WORK"

echo "==> Done: ${DMG} ($(du -h "$DMG" | cut -f1 | tr -d ' '))"

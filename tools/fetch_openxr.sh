#!/bin/bash
# Downloads the Khronos OpenXR loader for Android (Apache License 2.0) from
# Maven Central into third_party/openxr: its headers, which the game is
# compiled against, and libopenxr_loader.so, which the APK ships.
set -e
cd "$(dirname "$0")/.."
source tools/env.sh

VERSION=1.1.63
URL=https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/$VERSION/openxr_loader_for_android-$VERSION.aar
# The size and SHA-256 of the exact bytes Maven Central serves for this
# version (verified 2026-10-27).  The archive is compiled into the game and its
# library ships inside the APK, so an unverified download is a code path into
# every build.  Bump the version, size and hash together.
SIZE=4170279
SHA256=622419d2f6741c3443a3beb4779af0764318edd01830de967f24c741ebcded73
DEST=third_party/openxr

mkdir -p $DEST
echo "fetching the OpenXR loader $VERSION"
curl -fsSL --retry 3 -o $DEST/openxr_loader.aar "$URL"
# An .aar is a zip file; it is extracted member by member, never with
# extractall(), so an entry cannot write outside $DEST.
py3 -c "
import pathlib, sys
sys.path.insert(0, 'tools')
import build_installer
archive = pathlib.Path(sys.argv[1])
build_installer.verify_download(archive, int(sys.argv[2]), sys.argv[3])
build_installer.extract_zip(archive, sys.argv[4])
" "$(native_path $DEST/openxr_loader.aar)" "$SIZE" "$SHA256" "$(native_path $DEST)"
rm $DEST/openxr_loader.aar
test -f $DEST/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so
echo "OpenXR loader in $DEST"

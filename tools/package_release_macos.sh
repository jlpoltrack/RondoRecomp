#!/bin/bash
# Builds the macOS release (Apple silicon), dist/RondoRecomp-<version>-macos-arm64.dmg, on an
# Apple silicon Mac with Xcode's command line tools and CMake. Work files in build/macos.
# See docs/distribution.md.
set -euo pipefail
cd "$(dirname "$0")/.."
[ "$(uname -s)" = Darwin ] || { echo "run this on macOS"; exit 1; }
[ "$(uname -m)" = arm64 ] || { echo "builds the Apple silicon release: run it on an Apple silicon Mac"; exit 1; }
VER=$(sed -n 's/^#define RONDO_VERSION "\(.*\)"/\1/p' tools/frontend/programs.h)
ZIGV=$(sed -n 's/^#define RONDO_ZIG_VERSION "\(.*\)"/\1/p' tools/frontend/programs.h)
MINOS=15.0
export MACOSX_DEPLOYMENT_TARGET=$MINOS
M=$PWD/build/macos
S=$M/src
DEPS=$M/deps
JOBS=$(sysctl -n hw.ncpu)
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
mkdir -p "$S" "$DEPS"

fetch() {  # url sha256 file
    [ -f "$S/$3" ] || { curl -fsSL --proto '=https' -o "$S/$3.part" "$1" && mv "$S/$3.part" "$S/$3"; }
    echo "$2  $S/$3" | shasum -a 256 -c --quiet
}

# the same SDL3 and Zig versions as the Windows and Linux releases
SDL=SDL3-3.4.18
ZIG=zig-aarch64-macos-$ZIGV
fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/$SDL.tar.gz \
      9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3 $SDL.tar.gz
fetch https://ziglang.org/download/$ZIGV/$ZIG.tar.xz \
      b607e9b9234790a008116ae5bdb71c6243b84b9fb42a53a9e70fde41c06c536a $ZIG.tar.xz

# SDL3: shared, its install name @rpath/libSDL3.0.dylib, found next to each program
if [ "$(cat "$DEPS/version.txt" 2>/dev/null)" != "$SDL $MINOS" ]; then
    rm -rf "$DEPS" && mkdir -p "$DEPS"
    W=$(mktemp -d)
    tar -xf "$S/$SDL.tar.gz" -C "$W"
    cmake -S "$W/$SDL" -B "$W/build" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINOS -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF
    cmake --build "$W/build" -j"$JOBS"
    cmake --install "$W/build"
    cp "$W/$SDL/LICENSE.txt" "$DEPS/SDL3-LICENSE.txt"
    rm -rf "$W"
    echo "$SDL $MINOS" > "$DEPS/version.txt"
fi
otool -D "$DEPS/lib/libSDL3.0.dylib" | grep -q '@rpath/libSDL3.0.dylib' || { echo "SDL3's install name is not @rpath"; exit 1; }

# Setup, the SDK (and the developer tools, unused here)
cmake -S . -B "$M/cmake" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$DEPS" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINOS
cmake --build "$M/cmake" -j"$JOBS"
# each built, linking only the system's libraries and SDL3 (no Homebrew ones, which players do not have)
for t in rondo_setup "RondoRecomp Setup" sdk/lib/libSDL3.0.dylib; do
    [ -f "$M/cmake/$t" ] || { echo "macos: $t was not built"; exit 1; }
    LIBS=$(otool -L "$M/cmake/$t") || { echo "macos: otool cannot read $t"; exit 1; }
    if echo "$LIBS" | tail -n +2 | grep -v -e '^\s*/usr/lib/' -e '^\s*/System/' -e '@rpath/libSDL3.0.dylib'; then
        echo "macos: $t links a library players do not have (above)"; exit 1
    fi
done

# the app: programs in Contents/MacOS; the SDK, Zig and version.txt in Contents/Resources, sealed
# as resources, with links to them in Contents/MacOS, where Setup looks
P=$(mktemp -d)
trap 'rm -rf "$P"' EXIT
APP=$P/RondoRecomp.app
C=$APP/Contents
mkdir -p "$C/MacOS" "$C/Resources"
sed "s/@VERSION@/$VER/" tools/macos/Info.plist > "$C/Info.plist"
cc -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=$MINOS tools/macos/launcher.c -o "$C/MacOS/RondoRecomp"
install -m 755 "$M/cmake/rondo_setup" "$M/cmake/RondoRecomp Setup" "$C/MacOS/"
strip -x "$C/MacOS/RondoRecomp" "$C/MacOS/rondo_setup" "$C/MacOS/RondoRecomp Setup"
install -m 644 "$M/cmake/sdk/lib/libSDL3.0.dylib" "$C/MacOS/"
cp -R "$M/cmake/sdk" "$C/Resources/sdk"
tar -xf "$S/$ZIG.tar.xz" -C "$P" && mv "$P/$ZIG" "$C/Resources/zig"
[ "$("$C/Resources/zig/zig" version)" = "$ZIGV" ] || { echo "zig/ is not $ZIGV (RONDO_ZIG_VERSION in programs.h)"; exit 1; }
# only what building C for macOS uses (1,700 of 19,500 files): macOS checks every file of an app
# the first time it opens. Other systems' C libraries, C++, sanitizers and the unwinder go.
Z=$C/Resources/zig
rm -rf "$Z/doc" "$Z/lib/libcxx" "$Z/lib/libcxxabi" "$Z/lib/libtsan" "$Z/lib/libunwind"
find "$Z/lib/libc" -mindepth 1 -maxdepth 1 ! -name darwin ! -name include -exec rm -rf {} +
find "$Z/lib/libc/include" -mindepth 1 -maxdepth 1 ! -name any-darwin-any -exec rm -rf {} +
find "$C" -type d -exec chmod 755 {} + && find "$C" -type f ! -perm -u+x -exec chmod 644 {} +

# the icon, from the AppImage's (256 pixels)
IS=$P/RondoRecomp.iconset
mkdir "$IS"
for s in 16 32 128 256; do
    sips -z $s $s tools/linux/rondorecomp.png --out "$IS/icon_${s}x${s}.png" >/dev/null
    [ $s -le 128 ] && sips -z $((s * 2)) $((s * 2)) tools/linux/rondorecomp.png --out "$IS/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$IS" -o "$C/Resources/RondoRecomp.icns"

install -m 644 docs/PLAYER_README.txt "$C/Resources/README.txt"
{
    echo "Third-party software included with RondoRecomp"
    echo "==============================================="
    echo
    echo "SDL 3 (Contents/MacOS/libSDL3.0.dylib, Contents/Resources/sdk/include/SDL3 and sdk/lib): zlib license"
    echo "    https://libsdl.org"
    echo "Zig $ZIGV (Contents/Resources/zig): MIT; its LLVM, Clang and LLD: Apache 2.0 with LLVM exceptions"
    echo "    https://ziglang.org - see zig/LICENSE"
    echo "stb_truetype (sdk/src/frontend/stb_truetype.h, draws the menus' text): public domain"
    echo "    (or MIT), Sean Barrett - https://github.com/nothings/stb"
    echo "Cinzel, Cinzel Decorative and UnifrakturMaguntia fonts (sdk/src/frontend/ui_fonts.c, built"
    echo "into rondo for the menus): SIL Open Font License 1.1 - the Cinzel Project Authors,"
    echo "Natanael Gama; j. 'mach' wust, Peter Wiegel. https://github.com/google/fonts"
    echo "Shinonome Gothic 12 and 16 dot bitmap fonts (sdk/src/runtime/pce/kanji_font.c, the"
    echo "Japanese text of the original disc): public domain, The Electronic Font Open Laboratory"
    echo "    http://openlab.jp/efont/shinonome/"
    echo "rcheevos 12.5.0 (sdk/src/rcheevos, built into rondo for RetroAchievements): MIT,"
    echo "    RetroAchievements - https://github.com/RetroAchievements/rcheevos"
    echo "LZMA SDK 26.05 (7z and LZMA decoder: built into the Setup programs for .7z files, and its LZMA"
    echo "    decoder, sdk/src/lzma, into the game for .chd files): public domain, Igor Pavlov -"
    echo "    https://www.7-zip.org/sdk.html"
    echo "stb_vorbis 1.22 (built into the Setup programs for Virtual Console audio): public domain"
    echo "    (or MIT), Sean Barrett - https://github.com/nothings/stb"
    echo
    echo "The license texts follow."
    for f in "$DEPS/SDL3-LICENSE.txt" tools/fonts/OFL-*.txt third_party/rcheevos/LICENSE; do
        [ -f "$f" ] || continue
        echo
        echo "---------------------------------------------------------------------------"
        case $f in
            */SDL3-LICENSE.txt) echo "SDL3/LICENSE.txt" ;;
            */rcheevos/LICENSE) echo "rcheevos/LICENSE" ;;
            *) basename "$f" ;;
        esac
        echo "---------------------------------------------------------------------------"
        cat "$f"
    done
} > "$C/Resources/THIRD_PARTY.txt"

# version.txt: the version and a fingerprint of everything else in the app. Setup copies it into
# the game it builds (build/version.txt), and the launcher rebuilds the game when they differ.
ID=$(cd "$C" && find . -type f ! -name version.txt -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -c1-12)
echo "$VER $ID" > "$C/Resources/version.txt" && chmod 644 "$C/Resources/version.txt"
for f in sdk zig version.txt; do ln -s "../Resources/$f" "$C/MacOS/$f"; done

# signed ad hoc: Apple silicon runs only signed code, and the bundle's seal keeps it whole
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# the disk image: the app and a link to Applications, to drag it onto
DMG=dist/RondoRecomp-$VER-macos-arm64.dmg
mkdir -p dist
rm -f dist/RondoRecomp-*-macos-arm64.dmg
ln -s /Applications "$P/Applications"
rm -rf "$IS"
hdiutil create -quiet -volname "RondoRecomp $VER" -srcfolder "$P" -fs HFS+ -format ULFO -ov "$DMG"
echo "release: $DMG ($(du -sh "$DMG" | cut -f1 | tr -d ' '))"

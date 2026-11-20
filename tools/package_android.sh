#!/bin/bash
# Builds RondoRecomp for Android TV (the NVIDIA Shield TV, arm64-v8a) on a Mac or x86-64 Linux:
# build/android/RondoRecomp.apk. It compiles the code Setup generated from the player's disc, so
# the APK is for their own device only and must never be shared (docs/android.md).
#
# Needs: the Android SDK (platforms;android-36, build-tools;37.0.0, an NDK), a JDK, CMake, Ninja,
# make, zip and curl.
# usage: tools/package_android.sh [GEN_DIR]   (default: the macOS app's or the AppImage's code)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
case $ROOT in *' '*) echo "the repo's path has a space, which the generated Makefile cannot take"; exit 1 ;; esac
# this system: the NDK's compilers, the generated code Setup left (the app's or the AppImage's)
case $(uname -s) in
    Darwin)
        HOST=darwin-x86_64   # (universal)
        JOBS=$(sysctl -n hw.ncpu)
        GEN_HOME="$HOME/Library/Application Support/RondoRecomp/install"
        SDK_HOME=$HOME/Library/Android/sdk
        [ -n "${JAVA_HOME:-}" ] || JAVA_HOME=$(/usr/libexec/java_home 2>/dev/null || echo /opt/homebrew/opt/openjdk@21)
        ;;
    Linux)
        [ "$(uname -m)" = x86_64 ] || { echo "the Android NDK runs on x86-64 Linux only"; exit 1; }
        HOST=linux-x86_64
        JOBS=$(nproc)
        GEN_HOME=${RONDO_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/RondoRecomp/install}
        SDK_HOME=$HOME/Android/Sdk   # Android Studio's
        [ -n "${JAVA_HOME:-}" ] || JAVA_HOME=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
        ;;
    *) echo "builds on macOS or Linux"; exit 1 ;;
esac
VER=$(sed -n 's/^#define RONDO_VERSION "\(.*\)"/\1/p' tools/frontend/programs.h)
VCODE=$(echo "$VER" | awk -F. '{print $1 * 100 + $2}')
GEN=${1:-$GEN_HOME/build/gen}
[ -f "$GEN/table.c" ] || { echo "no generated code in $GEN (run Setup with your disc first)"; exit 1; }
SDK=${ANDROID_HOME:-$SDK_HOME}
NDK=${ANDROID_NDK_HOME:-}
if [ -z "$NDK" ]; then
    for d in "$SDK"/ndk/*; do [ -d "$d" ] && NDK=$d; done   # the newest (the glob sorts)
fi
BT=$SDK/build-tools/37.0.0
JAR=$SDK/platforms/android-36/android.jar
export JAVA_HOME
for f in "$NDK/build/cmake/android.toolchain.cmake" "$BT/aapt2" "$BT/d8" "$BT/apksigner" "$JAR" "$JAVA_HOME/bin/javac"; do
    [ -e "$f" ] || { echo "missing: $f (see docs/android.md, Build)"; exit 1; }
done
# the key that signs the app: outside the repo, since Android installs an update only with the same key
KS=${RONDO_KEYSTORE:-$HOME/.android/rondorecomp.keystore}
API=30   # Android 11, the Shield TV's
TC=$NDK/toolchains/llvm/prebuilt/$HOST/bin
CC=("$TC/clang" --target=aarch64-linux-android$API)
A=$ROOT/build/android
S=$A/src
mkdir -p "$S"

fetch() {  # url sha256 file
    [ -f "$S/$3" ] || { curl -fsSL --proto '=https' -o "$S/$3.part" "$1" && mv "$S/$3.part" "$S/$3"; }
    # (macOS's own sha256sum lacks --quiet; Linux may lack shasum)
    if command -v shasum >/dev/null; then echo "$2  $S/$3" | shasum -a 256 -c --quiet
    else echo "$2  $S/$3" | sha256sum -c --quiet; fi
}

# SDL3: the same version as the other releases, built for Android; its Java side goes in the APK
SDL=SDL3-3.4.18
fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/$SDL.tar.gz \
      9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3 $SDL.tar.gz
[ -d "$S/$SDL" ] || tar -xf "$S/$SDL.tar.gz" -C "$S"
if [ ! -f "$A/sdl/lib/libSDL3.so" ]; then
    cmake -S "$S/$SDL" -B "$S/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM=android-$API -DCMAKE_INSTALL_PREFIX="$A/sdl" -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build "$S/sdl-build" -j"$JOBS"
    cmake --install "$S/sdl-build" --strip
fi

# the game: the SDK's sources (as Setup compiles them) and the generated code -> libmain.so,
# compiled by make in parallel (paths without spaces, so the generated code is copied in)
rm -rf "$A/sdk" "$A/obj" "$A/gen" && mkdir -p "$A/obj" && cp -R "$GEN" "$A/gen"
cmake -DSRC="$ROOT" -DOUT="$A/sdk" -DSDL_INC="$A/sdl/include" -DSDL_LIB="$A/sdl/lib/libSDL3.so" -P tools/make_sdk.cmake
FLAGS="-O2 -fPIC -std=c11 -D_DEFAULT_SOURCE -I$A/sdk/src/runtime -I$A/sdk/include -I$A/gen"
SRCS=()
while IFS= read -r line; do
    case $line in
        ''|'#'*) ;;
        -I*) FLAGS="$FLAGS -I$A/sdk/${line#-I}" ;;
        -D*) FLAGS="$FLAGS $line" ;;
        *) SRCS+=("$A/sdk/$line") ;;
    esac
done < "$A/sdk/sources.txt"
for f in "$A"/gen/*.c; do SRCS+=("$f"); done
# warnings as in the desktop build for our code; none for third-party and generated code
{
    echo "CC = ${CC[*]}"
    echo "FLAGS = $FLAGS"
    objs=""
    i=0
    for src in "${SRCS[@]}"; do
        o=$A/obj/$(printf %03d $i).o
        case $src in */rcheevos/*|*/lzma/*|"$A"/gen/*) w="-w" ;; *) w="-Wall -Wextra -Wno-unused-parameter" ;; esac
        printf '%s: %s\n\t$(CC) $(FLAGS) %s -c $< -o $@\n' "$o" "$src" "$w"
        objs="$objs $o"
        i=$((i + 1))
    done
    echo "all:$objs"
} > "$A/obj/Makefile"
echo "compiling ${#SRCS[@]} sources"
make -s -f "$A/obj/Makefile" -j"$JOBS" all
LIB=$A/apk/lib/arm64-v8a
rm -rf "$A/apk" && mkdir -p "$LIB"
"${CC[@]}" -shared -o "$LIB/libmain.so" "$A"/obj/*.o "$A/sdl/lib/libSDL3.so" -lm -llog -landroid -Wl,--no-undefined -s
cp "$A/sdl/lib/libSDL3.so" "$LIB/"

# Java: SDL's activity (the manifest names it), compiled for Android and dexed
rm -rf "$A/classes" && mkdir -p "$A/classes"
find "$S/$SDL/android-project/app/src/main/java" -name '*.java' > "$A/java.txt"
"$JAVA_HOME/bin/javac" -nowarn -Xlint:none --release 11 -cp "$JAR" -d "$A/classes" @"$A/java.txt"
"$BT/d8" --release --min-api $API --lib "$JAR" --output "$A/apk" $(find "$A/classes" -name '*.class')

# resources: the launcher icon (the AppImage's) and the TV banner (tools/android/make_banner.py)
R=$A/res && rm -rf "$R" && mkdir -p "$R/mipmap-xxxhdpi" "$R/drawable-xhdpi"
cp tools/linux/rondorecomp.png "$R/mipmap-xxxhdpi/ic_launcher.png"
cp tools/android/banner.png "$R/drawable-xhdpi/banner.png"
sed -e "s/@VERSION@/$VER/" -e "s/@VERSION_CODE@/$VCODE/" tools/android/AndroidManifest.xml > "$A/AndroidManifest.xml"
"$BT/aapt2" compile --dir "$R" -o "$A/res.zip"
"$BT/aapt2" link -o "$A/base.apk" -I "$JAR" --manifest "$A/AndroidManifest.xml" \
    --min-sdk-version $API --target-sdk-version 36 "$A/res.zip"
(cd "$A/apk" && zip -q -r "$A/base.apk" classes.dex lib)

if [ ! -f "$KS" ]; then
    mkdir -p "$(dirname "$KS")"
    "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias rondo -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=RondoRecomp"
fi
OUT=$A/RondoRecomp.apk
"$BT/zipalign" -f 4 "$A/base.apk" "$A/aligned.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --ks-key-alias rondo --out "$OUT" "$A/aligned.apk"
"$BT/apksigner" verify "$OUT"
rm -f "$A/base.apk" "$A/aligned.apk" "$OUT.idsig"
echo "apk: $OUT ($(du -h "$OUT" | cut -f1)), for your own device only"

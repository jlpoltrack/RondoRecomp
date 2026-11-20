# Android: NVIDIA Shield TV only

The android branch is for one device, the NVIDIA Shield TV, and builds for `arm64-v8a` only,
for Android TV with a game controller or the Shield remote. Other Android devices, other ABIs
and touch controls are out of scope.

Checked on the target (2026-11-20, over the network): `SHIELD Android TV`, device `foster`
(the 2015/2017 model), Android 11 (API 30), ABIs `arm64-v8a,armeabi-v7a,armeabi`, a 3840x2160
display rendering at 1920x1080. The 2019 tube-shaped Shield is thought to run a 32-bit
userspace (`armeabi-v7a` only) and would not install this APK; check any other Shield with
`adb shell getprop ro.product.cpu.abilist`.

## How it differs from the other systems

Setup cannot run on Android: Android does not run programs from storage an app can write to,
and Zig has no Android build. So the game code is recompiled from the player's disc on a desktop
(Setup on the Mac does it), and `tools/package_android.sh` compiles that code with the NDK into
the app. **The APK therefore holds code generated from the player's own disc: it is for their
own device and must not be shared, published or built by CI.** The disc goes to the device
separately, with adb.

The app is SDL3's own activity (`org.libsdl.app.SDLActivity`) running the game as `libmain.so`.
Its manifest (`tools/android/AndroidManifest.xml`) puts it in the Android TV launcher with a
banner (`tools/android/make_banner.py`), needs no touchscreen, and traps the remote's Back so it
opens the game's menu (as Esc does) instead of closing the app. The game's folder is its
external files folder, `/sdcard/Android/data/io.github.gminustwo.rondorecomp/files`
(`rondo_base_path` in `tools/frontend/programs.h`; SDL's own base path there is `./`, which SDL
reads as the APK's assets). The game looks for its discs there (`game/en`, `game/ja`) and
writes its profile there (`profile/`: settings, in-game saves, states) along with `rondo.log` and
`misses.txt`. adb can reach all of it. The game saves when it goes to the background, since
Android may end it there.

Not on Android: Setup and the start screen's Setup item, updates and RetroAchievements (both
reach their servers through libcurl, which Android does not have: they say so and do nothing),
and the file picker. Without a disc the game names the folder to copy one into, then closes.
The game ends its process when it quits, since SDL would close the next launch at once if it
reused a process whose game had finished. When it goes to the background (Home), it writes
`misses.txt`; saves and settings are written as they change.

## Build (on a Mac or x86-64 Linux)

```sh
# the Android SDK, once: build tools, platform and NDK, and a JDK
#   Mac:   brew install --cask android-commandlinetools; brew install openjdk@21
#          (SDK in ~/Library/Android/sdk)
#   Linux: Google's command-line tools (developer.android.com/studio, "Command line tools only")
#          and e.g. sudo apt install openjdk-21-jdk-headless cmake ninja-build make zip curl
#          (SDK in ~/Android/Sdk, as Android Studio has it; ANDROID_HOME names another)
sdkmanager --sdk_root=<SDK> "ndk;29.0.14206865" "build-tools;37.0.0" "platforms;android-36"

# the game code from your disc: Setup has made it once it has built the game (the macOS app's in
# ~/Library/Application Support/RondoRecomp/install, the AppImage's in
# ~/.local/share/RondoRecomp/install); another folder's build/gen can be given as the argument
tools/package_android.sh        # -> build/android/RondoRecomp.apk
```

The NDK runs on x86-64 Linux only (not on Arm Linux). The Linux path is untested so far (built and
installed from a Mac only). Windows: use WSL, which is the Linux case,
with Setup's `build/gen` (next to `RondoRecomp Setup.exe`) as the argument.

With widescreen: build from a merge of the android and widescreen branches. The key that signs
the APK is made on the first build at `~/.android/rondorecomp.keystore` (`RONDO_KEYSTORE` names
another); keep it, since Android installs an update over the app only when the same key signs
it, and uninstalling deletes the discs and saves on the device. The banner is
`tools/android/banner.png`, drawn by `tools/android/make_banner.py` (Pillow).

Tested on the Shield (2026-11-20, with widescreen merged in): the app installs over the network,
appears in the launcher, finds both discs pushed by adb, and plays the intro.

## Install over the network

The Shield's network debugging is set up in the Hardware-Shield repo (`docs/shield-adb.md`):
Developer options, Network debugging, at `192.168.1.167:5555`. On the Mac, the app running adb
needs Local Network access (System Settings, Privacy & Security, Local Network), otherwise
`adb connect` fails with "No route to host".

```sh
adb connect 192.168.1.167:5555
D=/sdcard/Android/data/io.github.gminustwo.rondorecomp/files
G="$HOME/Library/Application Support/RondoRecomp/install/game"

# 1. the app (-r: over an earlier install, keeping its files)
adb install -r build/android/RondoRecomp.apk

# 2. the discs, as Setup extracted them: about 520 MB each
adb shell mkdir -p $D/game/en $D/game/ja
adb push "$G/en/." $D/game/en/
adb push "$G/ja/." $D/game/ja/

# 3. start it (or pick RondoRecomp in the launcher), and watch its log
adb shell am start -n io.github.gminustwo.rondorecomp/org.libsdl.app.SDLActivity
adb logcat -s SDL SDL/APP    # the game's SDL_Log lines (what it prints to stderr does not show)
```

Afterwards:
- `adb pull $D/misses.txt`: code the game met that was not recompiled. Put it next to the Mac's
  game and rebuild (Setup's Rebuild), then build the APK again.
- `adb pull $D/profile/`: back up settings and saves. Uninstalling the app deletes `$D`, discs
  and saves included.
- Do not copy a desktop `settings.ini` or `disc.txt` over: they hold the desktop's paths.

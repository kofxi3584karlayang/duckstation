# DuckStation - PlayStation 1, aka. PSX Emulator
<p align="center">
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/main-qt.png" alt="Main Window Screenshot" />
</p>

## Features
 - CPU Recompiler/JIT (x86-64 and AArch64)
 - Hardware (D3D11 and OpenGL) and software rendering
 - Upscaling and true colour (24-bit) in hardware renderers
 - "Fast boot" for skipping BIOS splash/intro
 - Save state support
 - Windows, Linux, **highly experimental** macOS support
 - Supports bin/cue images, raw bin/img files, and MAME CHD formats.
 - Direct booting of homebrew executables
 - Digital and analog controllers for input (rumble is forwarded to host)
 - Qt and SDL frontends for desktop
 - Qt frontend has graphical configuration, and controller binding
 - Automatic content scanning - game titles/regions are provided by redump.org

## System Requirements
 - A CPU faster than a potato.
 - For the hardware renderers, a GPU capable of OpenGL 3.0/OpenGL ES 3.0/Direct3D 11 Feature Level 10.0 and above. So, basically anything made in the last 10 years or so.
 - SDL-compatible game controller (e.g. XB360/XBOne)

## Downloading and running
Prebuilt binaries of DuckStation for 64-bit Windows are available via AppVeyor CI. To download:
 - Go to https://ci.appveyor.com/project/stenzek/duckstation
 - Select "Image: Visual Studio 2019" from the Job list.
 - Click on the "Artifacts" Tab.
 - Download the "duckstation-win64-release.7z" artifact. This is a 7-Zip archive containing the prebuilt binary.
 - Extract the archive **to a subdirectory**. The archive has no root subdirectory, so extracting to the current directory will drop a bunch of files in your download directory if you do not extract to a subdirectory.

Once downloaded and extracted, you can launch the Qt frontend from `duckstation-qt-x64-ReleaseLTCG.exe`, or the SDL frontend from `duckstation-sdl-x64-ReleaseLTCG.exe`.
To set up:
1. Either configure the path to a BIOS image in the settings, or copy one or more PlayStation BIOS images to the bios/ subdirectory.
2. If using the SDL frontend, add the directories containing your disc images by clicking `Settings->Add Game Directory`.
2. Select a game from the list, or open a disc image file and enjoy.

PlayStation game discs do not contain title information. For game titles, we use the redump.org database cross-referenced with the game's executable code.
This database can be manually downloaded and added as `cache/redump.dat`, or automatically downloaded by going into the `Game List Settings` in the Qt Frontend,
and clicking `Update Redump Database`.

## Building

### Windows
Requirements:
 - Visual Studio 2019
 
1. Clone the respository with submodules (`git clone --recursive` or `git clone` and `git submodule update --init`).
2. Open the Visual Studio solution `duckstation.sln` in the root, or "Open Folder" for cmake build.
3. Build solution.
4. Binaries are located in `bin/x64`.
5. Run `duckstation-sdl-x64-Release.exe`/`duckstation-qt-x64-Release.exe` or whichever config you used.

### Linux
Requirements (Debian/Ubuntu package names):
 - CMake (`cmake`)
 - SDL2 (`libsdl2-dev`)
 - GTK2.0 for file selector (`libgtk2.0-dev`)
 - Qt 5 (`qtbase5-dev`, `qtbase5-dev-tools`)
 - Optional for faster building: Ninja (`ninja-build`)

1. Clone the repository. Submodules aren't necessary, there is only one and it is only used for Windows.
2. Create a build directory, either in-tree or elsewhere.
3. Run cmake to configure the build system. Assuming a build subdirectory of `build-release`, `cd build-release && cmake -DCMAKE_BUILD_TYPE=Release -GNinja ..`.
4. Compile the source code. For the example above, run `ninja`.
5. Run the binary, located in the build directory under `src/duckstation-sdl/duckstation-sdl`, or `src/duckstation-qt/duckstation-qt`.

### macOS
**NOTE:** macOS is highly experimental and not tested by the developer. Use at your own risk, things may be horribly broken.

Requirements:
 - CMake (installed by default? otherwise, `brew install cmake`)
 - SDL2 (`brew install sdl2`)
 - Qt 5 (`brew install qt5`)

1. Clone the repository. Submodules aren't necessary, there is only one and it is only used for Windows.
2. Create a build directory, either in-tree or elsewhere, e.g. `mkdir build-release`, `cd build-release`.
3. Run cmake to configure the build system: `cmake -DCMAKE_BUILD_TYPE=Release -DQt5_DIR=/usr/local/opt/qt/lib/cmake/Qt5 ..`. You may need to tweak `Qt5_DIR` depending on your system.
4. Compile the source code: `make`. Use `make -jN` where `N` is the number of CPU cores in your system for a faster build.
5. Run the binary, located in the build directory under `src/duckstation-sdl/duckstation-sdl`, or `src/duckstation-qt/duckstation-qt`.

Application bundles/.apps are currently not created, so you can't launch it via Finder yet. This is planned for the future.

### Android
**NOTE:** The Android frontend is still incomplete, not all functionality works and some paths are hard-coded.
**The Android app is currently broken and fixing it is not a priority. However, this will change in the future.**

Requirements:
 - Android Studio with the NDK and CMake installed

1. Clone the repository. Submodules aren't necessary, there is only one and it is only used for Windows.
2. Open the project in the `android` directory.
3. Select Build -> Build Bundle(s) / APKs(s) -> Build APK(s).
4. Install APK on device, or use Run menu for attached device.

## User Directories
The "User Directory" is where you should place your BIOS images, where settings are saved to, and memory cards/save states are saved by default.

This is located in the following places depending on the platform you're using:

- Windows: Directory containing DuckStation executable.
- Linux: `$XDG_DATA_HOME/duckstation`, or `~/.local/share/duckstation`.
- macOS: `~/Library/Application Support/DuckStation`.

So, if you were using Linux, you would place your BIOS images in `~/.local/share/duckstation/bios`. This directory will be created upon running DuckStation
for the first time.

If you wish to use a "portable" build, where the user directory is the same as where the executable is located, create an empty file named `portable.txt`
in the same directory as the DuckStation executable.

## Bindings for Qt frontend
Your keyboard and any SDL-compatible game controller can be used to simulate the PS Controller. To bind keys/controllers to buttons, go to
Settings` -> `Port Settings`. Each of the buttons will be listed, along with the corresponding key it is bound to. To re-bind the button to a new key,
click the button next to button name, and press the key/button you want to use within 5 seconds.

**Currently, it is only possible to bind one input to each controller button/axis. Multiple bindings per button are planned for the future.**

## Default keyboard bindings for SDL frontend
Keyboard bindings in the SDL frontend are currently not customizable. For reference:
 - **D-Pad:** W/A/S/D or Up/Left/Down/Right
 - **Triangle/Square/Circle/Cross:** I/J/L/K or Numpad8/Numpad4/Numpad6/Numpad2
 - **L1/R1:** Q/E
 - **L2/L2:** 1/3
 - **Start:** Enter
 - **Select:** Backspace

Gamepads are automatically detected and supported. Tested with an Xbox One controller.
To access the menus with the controller, press the right stick down and use the D-Pad to navigate, A to select.

## Useful hotkeys for SDL frontend
 - **F1-F8:** Quick load/save (hold shift to save)
 - **F11:** Toggle fullscreen
 - **Tab:** Temporarily disable speed limiter
 - **Pause/Break:** Pause/resume emulation
 - **Space:** Frame step
 - **End:** Toggle software renderer
 - **Page Up/Down:** Increase/decrease resolution scale in hardware renderers

## Tests
 - Passes amidog's CPU and GTE tests in both interpreter and recompiler modes, partial passing of CPX tests

## Screenshots
<p align="center">
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff7.jpg" alt="Final Fantasy 7" width="400" />
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff8.jpg" alt="Final Fantasy 8" width="400" />
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/main.png" alt="SDL Frontend" width="400" />
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/spyro.jpg" alt="Spyro 2" width="400" />
</p>

## Disclaimers

Icon by icons8: https://icons8.com/icon/74847/platforms.undefined.short-title

"PlayStation" and "PSX" are registered trademarks of Sony Interactive Entertainment Europe Limited. This project is not affiliated in any way with Sony Interactive Entertainment.

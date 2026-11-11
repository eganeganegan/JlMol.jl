# Packaging JlMol

## macOS local package

Build JlMol with Chemfiles and embedded Julia enabled, then run the packager from the repository root. It requires Python 3.9+, the Xcode command-line tools, and `macdeployqt` from the Qt installation used for the build. [Qt macOS deployment guide](https://doc.qt.io/qt-6/macos-deployment.html)

```sh
cmake --preset dev
cmake --build --preset dev -j 8
python3 packaging/package_macos.py --archive
open build/package/JlMol.app
```

The outputs are `build/package/JlMol.app` and, with `--archive`, `build/package/JlMol-macOS.zip`. Use `--build-dir` for a different configured build, `--macdeployqt` to select the deployment tool explicitly, and `--output-dir` for another destination. Existing outputs are protected. The app bundle can be moved as a unit.

The package carries Qt, its Cocoa platform plugin, JlMol's native library and embedded shaders, the Julia distribution used at build time, the JlMol Julia project, crambin, a three-frame water trajectory, and the Structure Tools plugin example. The installed app finds these by location relative to its own executable. Julia uses JlMol's per-user application data for its default depot and keeps the bundled system depots available. The Julia project uses bundled standard libraries, so first launch needs no package download. [Julia environment and depot rules](https://docs.julialang.org/en/v1/manual/environment-variables/#JULIA_DEPOT_PATH)

The packager audits the Mach-O dependency paths and symlinks, signs the nested code and the outer app, and checks the signature. It runs the bundled scientific test after renaming the app and using a fresh home and a system-only executable search path. To run a separate native graphics check from the finished package:

```sh
app="$PWD/build/package/JlMol.app"
"$app/Contents/MacOS/jlmol" "$app/Contents/Resources/jlmol/resources/1CRN.pdb" --graphics-test
```

The verified build is Apple Silicon on macOS 27.0.1 with Qt 6.12 and Julia 1.13.1. An extracted copy passed signature verification, all 77 scientific checks, native graphics checks, and the bundled plugin test from a fresh home outside the checkout. The bundle uses an ad-hoc signature for local testing. Public distribution still needs a target-OS and architecture matrix, dependency redistribution notices, Developer ID signing, and notarization. [Apple notarization guide](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

## Windows MSI

Use a native Windows x64 build with Visual Studio 2022, its C++ tools and redistributable files, Qt 6.7 or newer for MSVC, the official Julia runtime on `PATH`, and WiX Toolset 3. The MSI stages the native libraries, Julia, and the Qt runtime and platform plugins alongside the application. The installer adds a JlMol Start menu entry. The viewport requires an OpenGL 4.1 driver on the host. [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html) · [Julia Windows embedding](https://docs.julialang.org/en/v1/manual/embedding/#High-Level-Embedding-on-Windows-with-Visual-Studio) · [CPack WiX](https://cmake.org/cmake/help/latest/cpack_gen/wix.html)

```powershell
# Set QT_ROOT_DIR to the root of the Qt kit.
cmake -S . -B build/portable -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$env:QT_ROOT_DIR" -DJLMOL_PACKAGE_DESKTOP=ON
cmake --build build/portable --config Release
ctest --test-dir build/portable -C Release --output-on-failure
cpack --config build/portable/CPackConfig.cmake -C Release -B build/packages
```

## Linux portable archive

Build on Ubuntu 24.04 x64 with CMake, Ninja, a desktop Qt build, the official Julia runtime on `PATH`, and `patchelf`. The tarball carries JlMol, Qt, and Julia. Keep the extracted directory together and launch `bin/jlmol` inside it. The host supplies the window system and OpenGL drivers. [Qt CMake deployment](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)

```sh
# Set QT_ROOT_DIR to the root of the Qt kit.
cmake -S . -B build/portable -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" -DCMAKE_INSTALL_LIBDIR=lib -DJLMOL_PACKAGE_DESKTOP=ON
cmake --build build/portable
ctest --test-dir build/portable --output-on-failure
cpack --config build/portable/CPackConfig.cmake -B build/packages
```

Extract and launch the x86-64 package on Ubuntu:

```sh
tar -xzf build/packages/JlMol-0.1.0-Linux-x86_64.tar.gz
./JlMol-0.1.0-Linux-x86_64/bin/jlmol
```

The archive built with Qt 6.8.3 and Julia 1.13.1 passed on Ubuntu 24.04 x86-64 in a separate offline runtime container. The test ran as an unprivileged user from a path with spaces and used Xvfb with Mesa software rendering. It verified that the executable loaded JlMol, Qt, and Julia from the bundle, passed all 77 scientific checks, and exercised rendering and the bundled three-frame trajectory.

## Windows and Linux verification

The [portable package workflow](../.github/workflows/portable-packages.yml) builds and runs CTest on Windows Server 2022 and Ubuntu 24.04 with Qt 6.8.3 and Julia 1.13.1. Both jobs run the bundled scientific reference, OpenGL rendering, and three-frame trajectory playback checks from a path with spaces and a system-only search path. The Windows job installs and removes the MSI in quiet mode. It supplies a checksum-verified Mesa llvmpipe driver to the verifier so the installed app can render on the hosted runner. The Linux job extracts the archive and checks where the dynamic loader finds JlMol, Qt, and Julia. Each job uploads the package after the checks pass. [Windows llvmpipe build](https://github.com/mmozeiko/build-mesa/releases/tag/26.1.6)

On Windows Server 2022 x64, the MSI installed and uninstalled successfully. The installed app passed all 77 scientific checks, rendered helix ribbons and beta sheets, and played the three-frame water trajectory with the runner's Mesa software driver. Linux verification covers Ubuntu 24.04 x86-64 with software rendering. [Verification run](https://github.com/eganeganegan/JlMol.jl/actions/runs/42890684775)

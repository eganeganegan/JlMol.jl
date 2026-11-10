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

The package carries Qt, its Cocoa platform plugin, JlMol's native library and embedded shaders, the Julia distribution used at build time, the JlMol Julia project, crambin, and the Structure Tools plugin example. The installed app finds these by location relative to its own executable. Julia uses JlMol's per-user application data for its default depot and keeps the bundled system depots available. The Julia project uses bundled standard libraries, so first launch needs no package download. [Julia environment and depot rules](https://docs.julialang.org/en/v1/manual/environment-variables/#JULIA_DEPOT_PATH)

The packager audits the Mach-O dependency paths and symlinks, signs the nested code and the outer app, and checks the signature. It runs the bundled scientific test after renaming the app and using a fresh home and a system-only executable search path. To run a separate native graphics check from the finished package:

```sh
app="$PWD/build/package/JlMol.app"
"$app/Contents/MacOS/jlmol" "$app/Contents/Resources/jlmol/resources/1CRN.pdb" --graphics-test
```

The verified build is Apple Silicon on macOS 27.0.1 with Qt 6.12 and Julia 1.13.1. An extracted copy passed signature verification, all 77 scientific checks, native graphics checks, and the bundled plugin test from a fresh home outside the checkout. The bundle uses an ad-hoc signature for local testing. Public distribution still needs a target-OS and architecture matrix, dependency redistribution notices, Developer ID signing, and notarization. [Apple notarization guide](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

## Windows and Linux verification

Packaging for these platforms is pending native build and launch verification.

| Platform | Work to verify |
| --- | --- |
| Windows | Add Julia import-library discovery for the selected Windows compiler. Stage `jlmol.exe`, the native DLL, the matching Julia runtime and project, and Qt DLLs and plugins with `windeployqt`. Verify the DLL search path on a clean system, then produce an MSI with WiX. [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html) · [Julia Windows embedding](https://docs.julialang.org/en/v1/manual/embedding/#High-Level-Embedding-on-Windows-with-Visual-Studio) · [CPack WiX](https://cmake.org/cmake/help/latest/cpack_gen/wix.html) |
| Linux | Stage the native executable, Qt libraries and platform plugins, and Julia in an AppDir with relative loader paths. Build against the oldest supported distribution and check the resulting AppImage on clean desktops. [Qt CMake deployment](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html) · [AppImage packaging guide](https://docs.appimage.org/packaging-guide/index.html) |

For each package, verify launch without a development environment, bundled scientific tests, native OpenGL 4.1 rendering, and trajectory playback before listing platform support.

"""Run the bundled JlMol checks from an installed or extracted package."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def launch_environment(home: Path, software_opengl_dll: Path | None) -> dict[str, str]:
    if os.name == "nt":
        system_root = Path(os.environ["SystemRoot"])
        environment = {
            "SystemRoot": str(system_root),
            "WINDIR": str(system_root),
            "PATH": os.pathsep.join((str(system_root / "System32"), str(system_root))),
            "USERPROFILE": str(home),
            "LOCALAPPDATA": str(home / "data"),
            "APPDATA": str(home / "config"),
            "TEMP": str(home),
            "TMP": str(home),
            "QT_FORCE_STDERR_LOGGING": "1",
            "QT_LOGGING_RULES": "qt.qpa.gl.debug=true",
        }
        if software_opengl_dll is not None:
            environment.update(QT_OPENGL="software", QT_OPENGL_DLL=str(software_opengl_dll))
        return environment

    if sys.platform == "darwin":
        return {"PATH": "/usr/bin:/bin", "HOME": str(home), "CFFIXED_USER_HOME": str(home), "TMPDIR": str(home)}

    return {
        **{key: os.environ[key] for key in ("DISPLAY", "XAUTHORITY") if key in os.environ},
        "PATH": os.defpath,
        "LANG": "C.UTF-8",
        "HOME": str(home),
        "XDG_DATA_HOME": str(home / "data"),
        "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_CACHE_HOME": str(home / "cache"),
        "QT_QPA_PLATFORM": "xcb",
        "LIBGL_ALWAYS_SOFTWARE": "1",
    }


def verify_linux_libraries(executable: Path, root: Path, environment: dict[str, str]) -> None:
    result = subprocess.run(["ldd", str(executable)], cwd=environment["HOME"], env=environment,
                            check=True, capture_output=True, text=True, timeout=30)
    for line in result.stdout.splitlines():
        library, separator, location = line.partition("=>")
        if not separator:
            continue
        library, location = library.strip(), location.strip()
        if location == "not found":
            raise RuntimeError(f"Unresolved library: {library}")
        if library.startswith("libjulia"):
            expected = root / "lib/jlmol/Julia/lib"
        elif library.startswith(("libjlmol_core", "libQt6")):
            expected = root / "lib"
        else:
            continue
        path = Path(location.rsplit(" (", 1)[0]).resolve()
        if not path.is_relative_to(expected):
            raise RuntimeError(f"{library} resolved to {path}, expected it under {expected}")


def verify(root: Path, software_opengl_dll: Path | None = None) -> None:
    if sys.platform == "darwin":
        contents = root / "Contents"
        project = contents / "Resources/jlmol"
        executable = contents / "MacOS/jlmol"
        runtime_files = ("Contents/Frameworks/libjlmol_core.dylib", "Contents/Resources/Julia/bin/julia")
    elif os.name == "nt":
        project = root / "share/jlmol"
        executable = root / "bin/jlmol.exe"
        runtime_files = ("bin/jlmol_core.dll", "bin/julia.exe", "bin/libjulia.dll")
    else:
        project = root / "share/jlmol"
        executable = root / "bin/jlmol"
        runtime_files = ("lib/libjlmol_core.so", "lib/jlmol/Julia/bin/julia")

    crambin = project / "resources/1CRN.pdb"
    water = project / "test/data/water.xyz"
    trajectory = project / "test/data/water_trajectory.xyz"
    for path in (executable, project / "Project.toml", crambin, water, trajectory,
                 *(root / name for name in runtime_files)):
        if not path.is_file():
            raise FileNotFoundError(f"Missing package file: {path}")

    with tempfile.TemporaryDirectory(prefix="JlMol clean home ") as temporary:
        home = Path(temporary)
        environment = launch_environment(home, software_opengl_dll)
        if sys.platform.startswith("linux"):
            verify_linux_libraries(executable, root, environment)
        elif sys.platform == "darwin":
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", root],
                           check=True, timeout=30)

        checks = (
            ("Scientific reference", "--scientific-test"),
            ("Native graphics", "--graphics-test", str(crambin)),
            ("Trajectory playback", "--trajectory-test", str(trajectory), str(water)),
        )
        for name, *arguments in checks:
            print(f"Checking {name.lower()} from {root}", flush=True)
            # The Windows GUI executable needs explicit standard handles for its logs.
            with tempfile.TemporaryFile() as output:
                try:
                    subprocess.run([str(executable), *arguments], cwd=home, env=environment,
                                   stdout=output, stderr=subprocess.STDOUT, check=True, timeout=120)
                finally:
                    output.seek(0)
                    print(output.read().decode("utf-8", errors="replace"), end="", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_root", type=Path, help="root of the installed or extracted package")
    parser.add_argument("--software-opengl-dll", type=Path, help="software OpenGL driver for Windows verification")
    args = parser.parse_args()
    if os.name != "nt" and sys.platform != "darwin" and not sys.platform.startswith("linux"):
        parser.error("run this verifier on Windows, macOS, or Linux")
    driver = args.software_opengl_dll
    if driver is not None:
        if os.name != "nt" or not driver.is_file():
            parser.error("--software-opengl-dll requires an existing DLL on Windows")
        driver = driver.resolve()
    verify(args.package_root.resolve(), driver)


if __name__ == "__main__":
    main()

"""Stage, sign, and verify a standalone JlMol application on macOS."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


MACH_O_MAGIC = {
    bytes.fromhex(magic)
    for magic in ("feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca", "cafebabf", "bfbafeca")
}
SYSTEM_PATHS = ("/System/Library", "/usr/lib")
LIBRARY_COMMANDS = {
    "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB", "LC_LAZY_LOAD_DYLIB",
}


def run(*command, **options):
    return subprocess.run(command, check=True, **options)


def macho_files(bundle):
    for path in bundle.rglob("*"):
        if path.is_symlink():
            if not path.resolve(strict=True).is_relative_to(bundle):
                raise ValueError(f"Link leaves the bundle: {path.relative_to(bundle)}")
        elif path.is_file():
            with path.open("rb") as file:
                if file.read(4) in MACH_O_MAGIC:
                    yield path


def load_paths(binary):
    output = run("otool", "-l", binary, capture_output=True, text=True).stdout
    command = ""
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("cmd "):
            command = line.removeprefix("cmd ")
        if command == "LC_RPATH" and line.startswith("path "):
            yield command, line.removeprefix("path ").rsplit(" (offset ", 1)[0]
        elif command in LIBRARY_COMMANDS and line.startswith("name "):
            yield command, line.removeprefix("name ").rsplit(" (offset ", 1)[0]


def expand_bundle_path(bundle, path, owner):
    contents = bundle / "Contents"
    executable_dirs = (contents / "MacOS", contents / "Resources/Julia/bin")
    prefix, _, suffix = path.partition("/")
    if prefix == "@loader_path":
        return ((owner.parent / suffix).resolve(),)
    if prefix == "@executable_path":
        return tuple((directory / suffix).resolve() for directory in executable_dirs)
    return ()


def is_system_path(path):
    return any(path == root or path.startswith(root + "/") for root in SYSTEM_PATHS)


def clean_runpaths(bundle, binaries):
    for binary in binaries:
        rpaths = {path for command, path in load_paths(binary) if command == "LC_RPATH"}
        for path in sorted(rpaths):
            if path == "$ORIGIN" or path.startswith("$ORIGIN/"):
                replacement = "@loader_path" + path.removeprefix("$ORIGIN")
                if replacement not in rpaths:
                    run("install_name_tool", "-rpath", path, replacement, binary, capture_output=True, text=True)
                    continue
            if not is_system_path(path) and not any(
                candidate.is_relative_to(bundle) for candidate in expand_bundle_path(bundle, path, binary)
            ):
                run("install_name_tool", "-delete_rpath", path, binary, capture_output=True, text=True)


def audit_bundle(bundle, binaries):
    commands = {binary: list(load_paths(binary)) for binary in binaries}
    local_rpaths = set()
    for binary, paths in commands.items():
        for kind, path in paths:
            if kind == "LC_RPATH":
                local_rpaths.update(candidate for candidate in expand_bundle_path(bundle, path, binary)
                                    if candidate.is_relative_to(bundle))

    errors = []
    for binary, paths in commands.items():
        for kind, path in paths:
            if is_system_path(path):
                continue
            candidates = expand_bundle_path(bundle, path, binary)
            if kind == "LC_RPATH":
                valid = any(candidate.is_relative_to(bundle) for candidate in candidates)
            elif path.startswith("@rpath/"):
                suffix = path.removeprefix("@rpath/")
                valid = any((root / suffix).is_file() and (root / suffix).resolve().is_relative_to(bundle) for root in local_rpaths)
            else:
                valid = any(candidate.is_file() and candidate.is_relative_to(bundle) for candidate in candidates)
            if not valid:
                errors.append(f"{binary.relative_to(bundle)}: {kind} {path}")
    if errors:
        raise ValueError("Unresolved or non-system bundle dependencies:\n" + "\n".join(errors))
    print(f"Verified dependency paths in {len(binaries)} Mach-O files.", flush=True)


def deploy_qt(bundle, macdeployqt):
    contents = bundle / "Contents"
    executable = contents / "MacOS/jlmol"
    frameworks = contents / "Frameworks"
    runtime = contents / "Resources/Julia"
    julia = [path for _, path in load_paths(executable) if path.startswith("@rpath/libjulia.")]
    if len(julia) != 1:
        raise ValueError("Could not identify the application's embedded libjulia dependency.")
    name = Path(julia[0]).name
    if not (runtime / "lib" / name).is_file():
        raise ValueError(f"The staged runtime lacks {name}.")
    # Julia's loader locates its own libraries relative to libjulia. Keep that layout intact.
    nested = f"@executable_path/../Resources/Julia/lib/{name}"
    run("install_name_tool", "-change", julia[0], nested, executable)
    # Let macdeployqt see only libjulia's loader, so it cannot flatten the Julia runtime's own libraries.
    held_runtime = bundle.parent / "julia-runtime"
    runtime.rename(held_runtime)
    try:
        loader_dir = runtime / "lib"
        loader_dir.mkdir(parents=True)
        shutil.copy2(held_runtime / "lib" / name, loader_dir / name)
        run(macdeployqt, bundle, "-no-codesign", "-no-strip")
    finally:
        if runtime.exists():
            shutil.rmtree(runtime)
        held_runtime.rename(runtime)
    deployed = [path for _, path in load_paths(executable) if Path(path).name == name]
    if len(deployed) != 1:
        raise ValueError("Qt deployment changed the application's libjulia dependency unexpectedly.")
    if deployed[0] != nested:
        run("install_name_tool", "-change", deployed[0], nested, executable)
    (frameworks / name).unlink(missing_ok=True)
    (contents / "Resources/qt.conf").write_text("[Paths]\nPrefix = .\nPlugins = PlugIns\n", encoding="utf-8")
    if not (contents / "PlugIns/platforms/libqcocoa.dylib").is_file():
        raise ValueError("Qt deployment did not produce the Cocoa platform plugin.")


def sign_bundle(bundle, binaries):
    print("Signing nested code and the app with an ad-hoc signature.", flush=True)
    for path in binaries:
        if path != bundle / "Contents/MacOS/jlmol":
            run("codesign", "--force", "--sign", "-", path, capture_output=True, text=True)
    for framework in sorted(bundle.rglob("*.framework"), key=lambda path: len(path.parts), reverse=True):
        if not framework.is_symlink():
            run("codesign", "--force", "--sign", "-", framework, capture_output=True, text=True)
    run("codesign", "--force", "--sign", "-", bundle)
    run("codesign", "--verify", "--deep", "--strict", bundle)


def smoke_test(bundle, staging):
    relocated = staging / "Relocated JlMol.app"
    bundle.rename(relocated)
    home = staging / "fresh home"
    home.mkdir()
    run(relocated / "Contents/MacOS/jlmol", "--scientific-test", cwd=home, env={
        "PATH": "/usr/bin:/bin", "HOME": str(home), "CFFIXED_USER_HOME": str(home), "TMPDIR": str(home),
    })
    relocated.rename(bundle)


def package(build, output, macdeployqt, archive):
    output.mkdir(parents=True, exist_ok=True)
    application = output / "JlMol.app"
    zip_path = output / "JlMol-macOS.zip"
    for path in (application, zip_path) if archive else (application,):
        if os.path.lexists(path):
            raise ValueError(f"Output already exists: {path}. Choose another output directory.")

    with tempfile.TemporaryDirectory(prefix=".jlmol-stage-", dir=output) as directory:
        staging = Path(directory)
        print(f"Staging the native build from {build}.", flush=True)
        run("cmake", "--install", build, "--prefix", staging, "--component", "JlMolDesktop", capture_output=True, text=True)
        bundle = staging / "jlmol.app"
        if not (bundle / "Contents/Resources/Julia/bin/julia").is_file():
            raise ValueError("Build JlMol with embedded Julia before packaging.")
        deploy_qt(bundle, macdeployqt)
        binaries = list(macho_files(bundle))
        clean_runpaths(bundle, binaries)
        audit_bundle(bundle, binaries)
        sign_bundle(bundle, binaries)
        smoke_test(bundle, staging)

        final_name = staging / application.name
        bundle.rename(final_name)
        if archive:
            run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", final_name, staging / zip_path.name)
        final_name.rename(application)
        if archive:
            (staging / zip_path.name).rename(zip_path)
            print(f"Archive: {zip_path}")
    print(f"Application: {application}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/package"))
    parser.add_argument("--macdeployqt", default=shutil.which("macdeployqt"))
    parser.add_argument("--archive", action="store_true", help="Also write a macOS ZIP archive.")
    options = parser.parse_args()
    if sys.platform != "darwin" or not options.macdeployqt:
        parser.error("This tool requires macOS and macdeployqt from the Qt installation used to build JlMol.")
    try:
        package(options.build_dir.resolve(), options.output_dir.resolve(), options.macdeployqt, options.archive)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.strip(), file=sys.stderr)
        sys.exit(1)

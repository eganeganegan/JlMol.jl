"""Collect matching Qt source and license materials for the desktop packages."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
import tempfile
from urllib.parse import urlsplit
from urllib.request import urlopen
import zipfile


CI_QT_VERSION = "6.8.3"
CI_SOURCE_HASHES = {
    "qtbase": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
    "qtsvg": "35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438",
}


def ci_sources():
    base = f"https://download.qt.io/archive/qt/6.8/{CI_QT_VERSION}/submodules"
    return [
        {"module": module, "url": f"{base}/{module}-everywhere-src-{CI_QT_VERSION}.tar.xz", "sha256": digest}
        for module, digest in CI_SOURCE_HASHES.items()
    ]


def homebrew_sources(prefix):
    sbom = prefix / "sbom.spdx.json"
    formula = prefix / ".brew/qtbase.rb"
    packages = json.loads(sbom.read_text(encoding="utf-8"))["packages"]
    base = next((package for package in packages if package["name"] == "qtbase"), None)
    if not base or not re.fullmatch(r"\d+\.\d+\.\d+", base.get("versionInfo", "")) or not formula.is_file():
        raise ValueError("Use the installed Homebrew qtbase prefix for the macOS build.")
    patches = [package for package in packages if package["name"].startswith("qtbase patch ")]
    sources = []
    for package in [base, *patches]:
        digest = next((entry["checksumValue"] for entry in package.get("checksums", ())
                       if entry["algorithm"] == "SHA256"), "")
        sources.append({
            "module": "qtbase",
            "url": package["downloadLocation"],
            "sha256": digest,
        })
    return base["versionInfo"], sources, {"homebrew/sbom.spdx.json": sbom, "homebrew/qtbase.rb": formula}


def verified_source(source, directory, cache):
    url = urlsplit(source["url"])
    digest = source["sha256"].lower()
    filename = PurePosixPath(url.path).name
    if url.scheme != "https" or not filename or not re.fullmatch(r"[a-f0-9]{64}", digest):
        raise ValueError(f"Missing HTTPS URL or SHA-256 for {source['module']}")
    path = directory / filename
    actual = hashlib.sha256()
    if cache is None:
        print(f"Downloading {filename}", flush=True)
        source_file = urlopen(source["url"], timeout=90)
    else:
        source_file = (cache / filename).open("rb")
    with source_file, path.open("xb") as output:
        for chunk in iter(lambda: source_file.read(1024 * 1024), b""):
            actual.update(chunk)
            output.write(chunk)
    if actual.hexdigest() != digest:
        raise ValueError(f"SHA-256 mismatch for {filename}")
    return path


def add_file(bundle, name, path):
    with path.open("rb") as source, bundle.open(zipfile.ZipInfo(name), "w") as output:
        shutil.copyfileobj(source, output)


def add_notices(bundle, module, path):
    found_lgpl = False
    with tarfile.open(path) as source:
        for entry in source:
            name = PurePosixPath(entry.name)
            parts = name.parts
            if not entry.isfile() or len(parts) < 2 or name.is_absolute() or ".." in parts:
                continue
            if parts[1] == "LICENSES" or name.name == "qt_attribution.json" or (
                len(parts) == 2 and name.name.startswith("LICENSE")
            ):
                destination = PurePosixPath("notices", module, *parts[1:])
                with source.extractfile(entry) as notice:
                    bundle.writestr(zipfile.ZipInfo(destination.as_posix()), notice.read())
                found_lgpl |= name.name in {"LGPL-3.0-only.txt", "LICENSE.LGPL3"}
    if not found_lgpl:
        raise ValueError(f"No LGPL 3.0 text found in {path.name}")


def collect(kit, version, sources, provider_files, output_dir, cache=None):
    destination = output_dir / f"Qt-{version}-{kit}-sources.zip"
    if destination.exists():
        raise FileExistsError(f"Output already exists: {destination}")
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".qt-sources-", dir=output_dir) as temporary:
        directory = Path(temporary)
        staged = directory / destination.name
        manifest = {"qt_version": version, "kit": kit, "files": []}
        with zipfile.ZipFile(staged, "w") as bundle:
            for source in sources:
                path = verified_source(source, directory, cache)
                name = f"sources/{path.name}"
                add_file(bundle, name, path)
                manifest["files"].append({**source, "path": name})
                if path.name.endswith(".tar.xz"):
                    add_notices(bundle, source["module"], path)
            for name, path in provider_files.items():
                add_file(bundle, name, path)
            bundle.writestr(zipfile.ZipInfo("manifest.json"), json.dumps(manifest, indent=2) + "\n")
            bundle.writestr(zipfile.ZipInfo("README.md"), f"""# Qt {version} source and license materials

JlMol uses the Qt shared libraries under the GNU Lesser General Public License
version 3. This companion archive applies to the {kit} build of JlMol.

`sources/` contains the Qt module archives and any provider patches.
`notices/` contains the license texts and third-party attributions from those
archives. `manifest.json` records the source URLs and SHA-256 checksums.
The Homebrew companion also carries the build formula and provider SBOM.

Qt source licensing and redistribution information is available from
https://www.qt.io/development/open-source-lgpl-obligations.
""")
        os.link(staged, destination)
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    kit = parser.add_mutually_exclusive_group(required=True)
    kit.add_argument("--ci-kit", action="store_true", help=f"collect the Windows and Linux Qt {CI_QT_VERSION} sources")
    kit.add_argument("--homebrew-qt", type=Path, help="installed Homebrew qtbase prefix used for the macOS build")
    parser.add_argument("--output-dir", type=Path, default=Path("build/release-materials"))
    parser.add_argument("--source-cache", type=Path, help="read source files from this directory and verify their checksums")
    args = parser.parse_args()
    try:
        if args.ci_kit:
            collect("windows-linux-ci", CI_QT_VERSION, ci_sources(), {}, args.output_dir, args.source_cache)
        else:
            version, sources, provider = homebrew_sources(args.homebrew_qt)
            collect("macos-homebrew", version, sources, provider, args.output_dir, args.source_cache)
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f"Qt source collection failed: {error}\n")


if __name__ == "__main__":
    main()

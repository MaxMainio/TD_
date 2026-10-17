#!/usr/bin/env python3
"""Build relocatable reader resources. All generated output stays in --build-root."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile

SOURCE = Path(__file__).resolve().parent.parent


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def unpack(dep, root):
    archive = root / "downloads" / dep["archive"]
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + ".partial")
        subprocess.run(["/usr/bin/curl", "-fL", "--retry", "2", "--connect-timeout", "20",
                        "--max-time", "300", dep["url"], "-o", str(temporary)], check=True)
        if digest(temporary) != dep["sha256"]:
            raise RuntimeError("Downloaded source checksum mismatch: " + dep["archive"])
        temporary.replace(archive)
    if digest(archive) != dep["sha256"]:
        raise RuntimeError("Cached source checksum mismatch: " + str(archive))
    sources = root / "sources"
    sources.mkdir(exist_ok=True)
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        for member in members:
            destination = (sources / member.name).resolve()
            if sources.resolve() not in destination.parents or member.issym() or member.islnk() or not (member.isfile() or member.isdir()):
                raise RuntimeError("Unsafe source archive member: " + member.name)
        folder = sources / members[0].name.split("/")[0]
        marker = folder / ".metadata-source-sha256"
        if not marker.exists():
            tar.extractall(sources)
            marker.write_text(dep["sha256"])
        elif marker.read_text() != dep["sha256"]:
            raise RuntimeError("Stale source folder: " + str(folder))
    return folder


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, default=SOURCE / "build")
    parser.add_argument("--architectures", nargs="+", choices=["arm64", "x86_64"], default=[platform.machine()])
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    args = parser.parse_args()
    root = args.build_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TMPDIR"] = str(root / "tmp")
    Path(env["TMPDIR"]).mkdir(exist_ok=True)
    env["MACOSX_DEPLOYMENT_TARGET"] = "13.0"
    lock = json.loads((SOURCE / "dependencies.json").read_text())
    if digest(SOURCE / "vendor/json.hpp") != lock["json"]["sha256"]:
        raise RuntimeError("Vendored JSON header checksum mismatch")
    subprocess.run(["/usr/bin/perl", "-MCompress::Zlib", "-MDigest::MD5", "-MDigest::SHA", "-e", "exit 0"], check=True)
    resources = root / "readers"
    resources.mkdir(exist_ok=True)
    licenses = resources / "licenses"
    licenses.mkdir(exist_ok=True)
    exif = unpack(lock["exiftool"], root)
    exifout = resources / "exiftool"
    exifout.mkdir(exist_ok=True)
    shutil.copy2(exif / "exiftool", exifout / "exiftool")
    shutil.copytree(exif / "lib", exifout / "lib", dirs_exist_ok=True)
    for name in ("README", "LICENSE", "Artistic", "Copying"):
        if (exif / name).is_file():
            shutil.copy2(exif / name, licenses / ("ExifTool-" + name + ".txt"))
    ffmpeg = unpack(lock["ffmpeg"], root)
    for path in ffmpeg.glob("COPYING*"):
        shutil.copy2(path, licenses / ("FFmpeg-" + path.name + ".txt"))
    shutil.copy2(ffmpeg / "LICENSE.md", licenses / "FFmpeg-LICENSE.txt")
    binaries = []
    for arch in args.architectures:
        build = root / ("ffmpeg-" + arch)
        build.mkdir(exist_ok=True)
        # FFmpeg rejects out-of-tree source paths containing spaces. An in-place
        # build with relative paths also supports the workspace's "TD Plugins" name.
        if not (build / "configure").exists():
            shutil.copytree(ffmpeg, build, dirs_exist_ok=True)
        source_marker = build / ".metadata-source-sha256"
        if not source_marker.exists() or source_marker.read_text() != lock["ffmpeg"]["sha256"]:
            raise RuntimeError("Stale FFmpeg build sources; select a fresh --build-root: " + str(build))
        (build / ".tmp").mkdir(exist_ok=True)
        build_env = dict(env, TMPDIR=".tmp")
        command = ["./configure", "--cc=/usr/bin/clang", "--cxx=/usr/bin/clang++",
                   "--arch=" + ("aarch64" if arch == "arm64" else "x86_64"), "--target-os=darwin",
                   "--extra-cflags=-arch " + arch + " -mmacosx-version-min=13.0",
                   "--extra-ldflags=-arch " + arch + " -mmacosx-version-min=13.0",
                   "--disable-autodetect", "--disable-programs", "--enable-ffprobe", "--disable-doc",
                   "--disable-network", "--disable-shared", "--enable-static", "--disable-gpl",
                   "--disable-nonfree", "--disable-encoders", "--disable-muxers", "--disable-devices",
                   "--disable-filters", "--disable-avdevice", "--disable-avfilter", "--disable-x86asm",
                   "--disable-protocols", "--enable-protocol=file", "--disable-demuxer=hls,dash,concat"]
        if arch != platform.machine():
            command.append("--enable-cross-compile")
        signature = json.dumps([lock["ffmpeg"]["sha256"], command])
        marker = build / "metadata-build.json"
        if not marker.exists() or marker.read_text() != signature or not (build / "ffprobe").exists():
            print("Building ffprobe for " + arch + "; log: " + str(build / "build.log"), flush=True)
            with (build / "configure.log").open("w") as log:
                subprocess.run(command, cwd=build, env=build_env, stdout=log, stderr=subprocess.STDOUT, check=True)
            with (build / "build.log").open("w") as log:
                subprocess.run(["/usr/bin/make", "-j", str(args.jobs), "ffprobe"], cwd=build, env=build_env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            marker.write_text(signature)
        binaries.append(build / "ffprobe")
    if len(binaries) == 1:
        shutil.copy2(binaries[0], resources / "ffprobe")
    else:
        subprocess.run(["/usr/bin/lipo", "-create", *map(str, binaries), "-output", str(resources / "ffprobe")], check=True)
    subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(resources / "ffprobe")], check=True)
    versions = {key: value["version"] for key, value in lock.items()}
    versions["architectures"] = args.architectures
    (resources / "versions.json").write_text(json.dumps(versions, indent=2) + "\n")
    shutil.copy2(SOURCE / "dependencies.json", resources / "dependencies.json")
    print("Reader resources ready: " + str(resources))


if __name__ == "__main__":
    main()

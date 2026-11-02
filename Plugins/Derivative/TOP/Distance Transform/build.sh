#!/bin/sh
# TD_PLUGIN_BUILD: {"archive":"distance-transform-snapshot-6f3219649c4b-source.zip","archive_sha256":"7a75f2417c6b725028812b788c3677fa9998a2ba06cf662d190ae5f9ce13896e","kind":"package","plugin":"derivative/top/distance-transform","source_root":"distance-transform-snapshot-6f3219649c4b","version":"snapshot-6f3219649c4b"}
set -eu
TD_PLUGIN_BUILD_DIRECTORY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export TD_PLUGIN_BUILD_DIRECTORY
TD_PLUGIN_BUILD_DESCRIPTOR='{"archive":"distance-transform-snapshot-6f3219649c4b-source.zip","archive_sha256":"7a75f2417c6b725028812b788c3677fa9998a2ba06cf662d190ae5f9ce13896e","kind":"package","plugin":"derivative/top/distance-transform","source_root":"distance-transform-snapshot-6f3219649c4b","version":"snapshot-6f3219649c4b"}'
export TD_PLUGIN_BUILD_DESCRIPTOR
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3.9 or newer is required to build this source package." >&2
    exit 1
fi
exec python3 -B - "$@" <<'TD_PLUGIN_BUILD_PYTHON'
#!/usr/bin/env python3
"""Common build engine. Also embedded in standalone source-package launchers."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

CATEGORIES = ("apple", "custom", "derivative")
FAMILIES = ("top", "chop", "dat", "mat", "pop", "sop")
DESCRIPTOR_PREFIX = "# TD_PLUGIN_BUILD: "
HEREDOC = "TD_PLUGIN_BUILD_PYTHON"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def launcher(descriptor, runtime=None):
    """Generate a relocatable shell launcher with no sibling Python dependency."""
    if runtime is None:
        runtime = Path(__file__).read_text()
    encoded = json.dumps(descriptor, sort_keys=True, separators=(",", ":"))
    # Descriptors are generated from validated IDs, versions and checksums.
    import shlex
    return ("#!/bin/sh\n" + DESCRIPTOR_PREFIX + encoded + "\nset -eu\n"
            'TD_PLUGIN_BUILD_DIRECTORY=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            "export TD_PLUGIN_BUILD_DIRECTORY\n"
            "TD_PLUGIN_BUILD_DESCRIPTOR=" + shlex.quote(encoded) + "\n"
            "export TD_PLUGIN_BUILD_DESCRIPTOR\n"
            "if ! command -v python3 >/dev/null 2>&1; then\n"
            '    echo "Python 3.9 or newer is required to build this source package." >&2\n'
            "    exit 1\nfi\n"
            'exec python3 -B - "$@" <<\'' + HEREDOC + "'\n" + runtime.rstrip() +
            "\n" + HEREDOC + "\n")


def descriptor_from_text(text):
    for line in text.splitlines()[:3]:
        if line.startswith(DESCRIPTOR_PREFIX):
            result = json.loads(line[len(DESCRIPTOR_PREFIX):])
            if result.get("kind") not in ("package", "source", "collection"):
                raise ValueError("Unknown build-launcher format")
            return result
    return None


def runtime_from_launcher(text):
    marker = "<<'" + HEREDOC + "'\n"
    if marker not in text or not text.endswith("\n" + HEREDOC + "\n"):
        raise ValueError("Invalid standalone build launcher")
    return text.split(marker, 1)[1].rsplit("\n" + HEREDOC + "\n", 1)[0] + "\n"


def check_collection_launcher(path, category=None):
    text = path.read_text()
    descriptor = descriptor_from_text(text)
    runtime = runtime_from_launcher(text)
    expected = {"kind": "collection", "runtime_sha256": sha256(runtime.encode())}
    if category:
        expected["category"] = category
    if descriptor != expected or text != launcher(descriptor, runtime):
        raise ValueError("Collection build launcher checksum/format mismatch: " + str(path))


def safe_relative(value):
    if (not isinstance(value, str) or not value or "\\" in value or "\x00" in value
            or ":" in value or any(x in ("", ".", "..") for x in value.split("/"))):
        raise ValueError("Unsafe package path: " + str(value))
    path = PurePosixPath(value)
    if path.is_absolute():
        raise ValueError("Absolute package path: " + value)
    return path


def validate_id(value):
    parts = safe_relative(value).parts
    if (len(parts) != 3 or parts[0] not in CATEGORIES or parts[1] not in FAMILIES
            or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", parts[2])):
        raise ValueError("Invalid plugin ID: " + str(value))
    return value


def package_contents(folder, descriptor):
    """Verify the delivery and every ZIP member before any extraction or code runs."""
    validate_id(descriptor["plugin"])
    archive_name = descriptor["archive"]
    if len(safe_relative(archive_name).parts) != 1 or not archive_name.endswith("-source.zip"):
        raise ValueError("Invalid source archive name")
    archive = folder / archive_name
    if archive.is_symlink() or not archive.is_file():
        raise ValueError("Missing source archive: " + str(archive))
    data = archive.read_bytes()
    if sha256(data) != descriptor["archive_sha256"]:
        raise ValueError("Source archive checksum mismatch: " + str(archive))
    root = descriptor["source_root"]
    if len(safe_relative(root).parts) != 1:
        raise ValueError("Invalid source archive root")
    members = {}
    case_names = set()
    with zipfile.ZipFile(archive) as source:
        if sum(x.file_size for x in source.infolist()) > 256 * 1024 * 1024:
            raise ValueError("Source archive exceeds the 256 MiB uncompressed limit")
        for entry in source.infolist():
            path = safe_relative(entry.filename)
            mode = entry.external_attr >> 16
            if (len(path.parts) < 2 or path.parts[0] != root or entry.is_dir()
                    or (stat.S_IFMT(mode) not in (0, stat.S_IFREG))
                    or entry.filename.casefold() in case_names or entry.flag_bits & 1):
                raise ValueError("Unsafe or duplicate source archive entry: " + entry.filename)
            case_names.add(entry.filename.casefold())
            members[PurePosixPath(*path.parts[1:]).as_posix()] = source.read(entry)
    info = json.loads(members["SOURCE-INFO.json"])
    if (info.get("format") != 1 or info["plugin"] != descriptor["plugin"]
            or info["version"] != descriptor["version"]):
        raise ValueError("Package identity does not match its source manifest")
    hashes = info["files"]
    if set(members) != set(hashes) | {"SOURCE-INFO.json"}:
        raise ValueError("Source archive file list does not match SOURCE-INFO.json")
    for name, expected in hashes.items():
        safe_relative(name)
        if sha256(members[name]) != expected:
            raise ValueError("Source file checksum mismatch: " + name)
    if not {"README.md", "CMakeLists.txt", ".gitignore", "build.sh"} <= members.keys():
        raise ValueError("Source package is missing required build/documentation files")
    inner_text = members["build.sh"].decode()
    inner = descriptor_from_text(inner_text)
    if inner != {"kind": "source", "plugin": info["plugin"], "version": info["version"]}:
        raise ValueError("Packaged source launcher has a different identity")
    runtime = runtime_from_launcher(inner_text)
    if sha256(runtime.encode()) != info["runtime_sha256"]:
        raise ValueError("Packaged build engine checksum mismatch")
    if (folder / "build.sh").read_text() != launcher(descriptor, runtime):
        raise ValueError("Delivery build launcher differs from the packaged build engine")
    if sha256((folder / "README.md").read_bytes()) != info["delivery_readme_sha256"]:
        raise ValueError("Delivery README checksum mismatch")
    return members, info


def public_id(parts):
    return "/".join((parts[0].lower(), parts[1].lower(), parts[2].lower().replace(" ", "-")))


def discover(root, descriptor, source_root=None):
    if descriptor and descriptor["kind"] in ("package", "source"):
        validate_id(descriptor["plugin"])
        return [{"id": descriptor["plugin"], "folder": root, "descriptor": descriptor}]
    source_root = source_root or (root / "plugins" if (root / "plugins").is_dir() else root)
    category = descriptor.get("category") if descriptor else None
    pattern = "*/*" if category else "*/*/*"
    results = {}
    for folder in sorted(source_root.glob(pattern)):
        if not folder.is_dir() or folder.is_symlink():
            continue
        relative = folder.relative_to(source_root).parts
        parts = (category,) + relative if category else relative
        if parts[0].lower() not in CATEGORIES or parts[1].lower() not in FAMILIES:
            continue
        item = None
        if (folder / "build.sh").is_file():
            item = descriptor_from_text((folder / "build.sh").read_text())
            if item and item["kind"] not in ("package", "source"):
                raise ValueError("Unexpected collection launcher inside plugin folder")
        if not item and not (folder / "CMakeLists.txt").is_file():
            continue
        name = item["plugin"] if item else public_id(parts)
        validate_id(name)
        if name in results:
            raise ValueError("Duplicate plugin ID: " + name)
        results[name] = {"id": name, "folder": folder, "descriptor": item}
    return list(results.values())


def select(projects, targets):
    selected = {}
    for target in targets:
        target = target.lower()
        matches = [p for p in projects if target == "all" or p["id"] == target
                   or (target in CATEGORIES and p["id"].startswith(target + "/"))]
        if not matches:
            raise ValueError("No plugins match '" + target + "'. Use --list to see available IDs.")
        selected.update((p["id"], p) for p in matches)
    return [selected[k] for k in sorted(selected)]


def logged(command, log, env=None):
    with log.open("w") as stream:
        stream.write("Command: " + repr([str(x) for x in command]) + "\n")
        stream.flush()
        result = subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT, env=env)
    if result.returncode:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-12:])
        raise ValueError(f"Command failed ({result.returncode}); see {log}\n{tail}")


def prepare_source(project, run):
    descriptor = project["descriptor"]
    if not descriptor or descriptor["kind"] == "source":
        return project["folder"]
    members, _ = package_contents(project["folder"], descriptor)
    source = run / "source" / descriptor["source_root"]
    source.mkdir(parents=True, exist_ok=False)
    for name, data in members.items():
        output = source.joinpath(*safe_relative(name).parts)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(data)
        if name == "build.sh":
            output.chmod(0o755)
    return source


def preflight(source, options):
    text = (source / "CMakeLists.txt").read_text()
    tools = ["cmake"] + (["ctest"] if options.tests else [])
    if platform.system() == "Darwin":
        tools += ["xcrun", "otool", "lipo", "codesign"]
    for tool in tools:
        if not shutil.which(tool):
            hint = "Install CMake (for example, brew install cmake)." if tool in ("cmake", "ctest") else "Install Apple's command-line developer tools."
            raise ValueError("Missing " + tool + ". " + hint)
    if platform.system() == "Darwin":
        probe = subprocess.run(["xcrun", "--find", "clang++"], capture_output=True, text=True)
        if probe.returncode:
            raise ValueError("No active C++ compiler. Install/select Apple's developer tools using xcode-select.")
    elif re.search(r"requires macOS|currently supports macOS|LANGUAGES[^\n]*OBJCXX", text, re.I):
        raise ValueError("This plugin requires macOS; see its README for platform requirements.")
    if "TD_APPLICATION" in text and platform.system() == "Darwin":
        application = options.td_application or Path("/Applications/TouchDesigner.app")
        headers = list((application / "Contents/Frameworks/Python.framework/Versions/Current/include").glob("python*/Python.h"))
        if len(headers) != 1:
            raise ValueError("TouchDesigner Python headers were not found. Set --td-application to the intended TouchDesigner.app.")
    return text


def build_project(project, run, dependencies, options):
    source = prepare_source(project, run)
    if options.extract_only:
        return {"status": "EXTRACTED", "source": str(source), "tests": "not run"}
    text = preflight(source, options)
    fixed_arch = re.search(r'set\s*\(\s*CMAKE_OSX_ARCHITECTURES\s+"?(arm64|x86_64)"?\s*\)', text, re.I)
    if options.architecture and fixed_arch and options.architecture != fixed_arch.group(1):
        raise ValueError(f"This project's CMake configuration requires {fixed_arch.group(1)}; requested {options.architecture}.")
    build = run / "build"
    output = run / "plugin"
    build.mkdir()
    output.mkdir()
    configure = ["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=" + options.config,
                 "-DOUTPUT_DIRECTORY=" + str(output)]
    # Both repository conventions are supported, including CTest's default-ON path.
    declared_tests = bool(re.search(r"option\s*\(\s*TD_BUILD_TESTS|include\s*\(\s*CTest", text, re.I))
    configure += ["-DTD_BUILD_TESTS=" + ("ON" if options.tests else "OFF"),
                  "-DBUILD_TESTING=" + ("ON" if options.tests else "OFF")]
    if options.td_application:
        configure += ["-DTD_APPLICATION=" + str(options.td_application.resolve())]
    if options.opencv_dir:
        configure += ["-DOpenCV_DIR=" + str(options.opencv_dir.resolve())]
    if options.architecture:
        configure += ["-DCMAKE_OSX_ARCHITECTURES=" + options.architecture]
    if project["id"] == "apple/top/depth-anything":
        models = Path(os.environ.get("DEPTH_MODEL_DIRECTORY", str(dependencies / "depth-anything/models"))).resolve()
        logged([sys.executable, "-B", source / "model_assets.py", "--directory", models], run / "prepare.log")
        configure += ["-DMODEL_DIRECTORY=" + str(models)]
    if project["id"] == "custom/dat/metadata-in":
        readers = os.environ.get("METADATA_READER_ROOT")
        if not readers:
            dependency_root = dependencies / "metadata-in"
            command = [sys.executable, "-B", source / "scripts/prepare-dependencies.py", "--build-root", dependency_root,
                       "--jobs", str(options.jobs)]
            if options.architecture:
                command += ["--architectures", options.architecture]
            logged(command, run / "prepare.log")
            readers = str(dependency_root / "readers")
        configure += ["-DMETADATA_READER_ROOT=" + str(Path(readers).resolve())]
    try:
        logged(configure, run / "configure.log")
    except ValueError as error:
        if "find_package(OpenCV" in text or "find_package (OpenCV" in text:
            raise ValueError(str(error) + "\nOpenCV projects need matching-architecture core/imgproc libraries. Install OpenCV (for example, brew install opencv) or pass --opencv-dir.") from error
        raise
    logged(["cmake", "--build", build, "--config", options.config, "--parallel", str(options.jobs)], run / "build.log")
    tests = "not requested"
    if options.tests and declared_tests:
        logged(["ctest", "--test-dir", build, "-C", options.config, "--output-on-failure", "--no-tests=error"], run / "test.log")
        tests = "passed"
    elif options.tests:
        tests = "none declared"
    bundles = sorted(output.glob("*.plugin"))
    if len(bundles) != 1:
        raise ValueError("Expected one .plugin bundle in " + str(output))
    binaries = [p for p in (bundles[0] / "Contents/MacOS").glob("*") if p.is_file()]
    if len(binaries) != 1:
        raise ValueError("Expected one executable in " + str(bundles[0]))
    if platform.system() == "Darwin":
        logged(["otool", "-L", binaries[0]], run / "dependencies.log")
        logged(["lipo", "-archs", binaries[0]], run / "architecture.log")
        architectures = (run / "architecture.log").read_text().splitlines()[-1].split()
        if options.architecture and options.architecture not in architectures:
            raise ValueError("Built executable does not contain the requested architecture " + options.architecture)
        if project["id"] == "custom/dat/metadata-in":
            logged(["otool", "-L", bundles[0] / "Contents/Resources/ffprobe"], run / "reader-dependencies.log")
            logged(["codesign", "--verify", "--deep", "--strict", bundles[0]], run / "signature.log")
    return {"status": "BUILT", "bundle": str(bundles[0]), "source": str(source), "tests": tests}


def main(argv=None):
    if sys.version_info < (3, 9):
        print("Python 3.9 or newer is required.", file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser(description="Build individual plugins, a category, or all plugins. Never installs or stages releases.")
    parser.add_argument("targets", nargs="*", metavar="all|apple|custom|derivative|category/family/plugin")
    parser.add_argument("--root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--source-root", type=Path, help="Override the collection source folder (for example, dist)")
    parser.add_argument("--list", action="store_true", help="List matching plugins without building")
    parser.add_argument("--dry-run", action="store_true", help="Show the selection and options without writes, extraction or downloads")
    parser.add_argument("--extract-only", action="store_true", help="Verify/extract a source package without building")
    parser.add_argument("--tests", action="store_true", help="Build and run declared native/simulated tests; does not launch TouchDesigner")
    parser.add_argument("--td-application", type=Path, help="TouchDesigner.app to build against")
    parser.add_argument("--opencv-dir", type=Path, help="Folder containing matching-architecture OpenCVConfig.cmake")
    parser.add_argument("--architecture", choices=("arm64", "x86_64"))
    parser.add_argument("--jobs", type=int, default=2, help="Compiler jobs per plugin (default: 2; plugins build sequentially)")
    parser.add_argument("--config", choices=("Release", "Debug", "RelWithDebInfo"), default="Release")
    parser.add_argument("--output-root", type=Path, help="Parent of fresh run directories (default: collection build/)")
    options = parser.parse_args(argv)
    if options.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        descriptor = json.loads(os.environ["TD_PLUGIN_BUILD_DESCRIPTOR"]) if "TD_PLUGIN_BUILD_DESCRIPTOR" in os.environ else None
        root = options.root or (Path(os.environ["TD_PLUGIN_BUILD_DIRECTORY"]) if "TD_PLUGIN_BUILD_DIRECTORY" in os.environ
                                else Path(__file__).resolve().parents[1])
        root = root.resolve()
        if options.source_root and descriptor:
            raise ValueError("Standalone launchers do not accept --source-root")
        source_root = options.source_root.resolve() if options.source_root else None
        projects = discover(root, descriptor, source_root)
        targets = options.targets or (["all"] if descriptor or options.list else [])
        if not targets:
            parser.print_help()
            return 0
        projects = select(projects, targets)
        if options.list or options.dry_run:
            for project in projects:
                delivery = project["descriptor"]
                kind = delivery["kind"] if delivery else "working source"
                print(f"{project['id']} [{kind}] {project['folder']}")
            print(f"{len(projects)} selected; tests {'enabled' if options.tests else 'disabled'}; no files changed.")
            return 0
        if options.extract_only and any(not p["descriptor"] or p["descriptor"]["kind"] != "package" for p in projects):
            raise ValueError("--extract-only requires source ZIP packages")
        output_root = (options.output_root or root / "build").resolve()
        output_root.mkdir(parents=True, exist_ok=True)
        batch = Path(tempfile.mkdtemp(prefix="td-build-", dir=output_root))
        print("Build run: " + str(batch), flush=True)
        results = []
        for project in projects:
            run = batch / project["id"]
            run.mkdir(parents=True)
            print("Building " + project["id"] + " ...", flush=True)
            try:
                result = build_project(project, run, output_root / "dependencies", options)
            except (OSError, ValueError, KeyError, zipfile.BadZipFile, RuntimeError) as error:
                result = {"status": "FAILED", "error": str(error)}
            result.update({"plugin": project["id"], "logs": str(run)})
            results.append(result)
            print(result["status"] + " " + project["id"], flush=True)
            if "error" in result:
                print(result["error"], flush=True)
            if "bundle" in result:
                print("Bundle: " + result["bundle"], flush=True)
            if "source" in result and options.extract_only:
                print("Source: " + result["source"], flush=True)
        (batch / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
        failed = sum(r["status"] == "FAILED" for r in results)
        print(f"{len(results) - failed} succeeded; {failed} failed. Summary: {batch / 'summary.json'}")
        print("Builds do not establish live TouchDesigner acceptance. No installation or release staging performed.")
        return 1 if failed else 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print("Build selection failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
TD_PLUGIN_BUILD_PYTHON

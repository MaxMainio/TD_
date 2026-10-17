#!/usr/bin/env python3
"""Optional fixture regeneration: requires an installed ffmpeg with encoders.
Creates only synthetic pixels/silence, then stores compressed base64 in media-fixtures.json.
Normal tests require neither ffmpeg encoders nor external image libraries.
"""
import base64
import json
from pathlib import Path
import shutil
import subprocess
import zlib

source = Path(__file__).resolve().parents[1]
output = source / "build/fixture-generation"
output.mkdir(parents=True, exist_ok=True)
ffmpeg = shutil.which("ffmpeg")
if not ffmpeg:
    raise SystemExit("Install ffmpeg with encoders to regenerate synthetic fixtures")
fixtures = {}
for suffix in ("jpg", "png", "tiff", "mp4", "mov", "mp3", "flac"):
    path = output / ("synthetic." + suffix)
    args = [ffmpeg, "-v", "error", "-y"]
    if suffix in ("mp3", "flac"):
        args += ["-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono", "-t", "0.05"]
    else:
        args += ["-f", "lavfi", "-i", "color=c=red:s=16x16:r=10"]
        if suffix in ("mp4", "mov"):
            args += ["-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono", "-t", "0.1", "-c:v", "mpeg4", "-c:a", "aac"]
        else:
            args += ["-frames:v", "1", "-update", "1"]
    args += ["-metadata", "title=MetaData_In fixture", str(path)]
    subprocess.run(args, check=True)
    fixtures[suffix] = base64.b64encode(zlib.compress(path.read_bytes(), 9)).decode("ascii")
(source / "tests/media-fixtures.json").write_text(json.dumps(fixtures, indent=2) + "\n")
print("Wrote synthetic image/audio/video fixtures")

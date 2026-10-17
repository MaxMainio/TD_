# MetaData_In

A native TouchDesigner DAT that reads metadata from **one local file** and
outputs a table. Select a file directly, or reference any operator with a File
parameter. No wired inputs are required.

Version 0.1.1; label `MetaData_In`, internal type `Metadatain`, bundle
`MetaData_In.plugin`. Targets macOS 13+ on arm64 and x86_64 with TouchDesigner
2025.33230 and its Python 3.11 runtime. The source includes that build's DAT SDK
headers; older TouchDesigner builds are not supported by this release.

## Parameters

| Page | Parameter | Identifier | Meaning | Default |
| --- | --- | --- | --- | --- |
| Source | File | `File` | One local file | Empty |
| Source | Operator | `Operator` | One operator of any family, including custom COMPs | Empty |
| Source | Auto Refresh | `Autorefresh` | Recheck the file on disk | On |
| Source | Refresh | `Refresh` | Force extraction, bypassing the cache | Pulse |
| Source | Deep Scan | `Deepscan` | Scan geometry data to calculate counts and bounds | Off |
| Technical | Check Interval (s) | `Checkinterval` | Seconds between background checks, 0.1-86400 | 1 |
| Technical | Read Timeout (s) | `Readtimeout` | Total extraction deadline, 0.1-86400 seconds | 30 |

File and Operator are both enabled while empty. Providing either automatically
selects it and disables the other. Clear the selected field to switch sources.
An empty node remains idle. If an older saved node or script supplies both, the
operator reports a conflict and leaves both fields editable so either can be
cleared. There is no Source menu or File Parameter override.

For an Operator reference, automatic detection looks for a unique parameter whose
name equals `file`, ignoring case. If absent, it looks for a unique parameter
labeled `File`, ignoring case. A missing or ambiguous File parameter produces a
clear error; use a source operator with an unambiguous File parameter.
There is no operator-family or operator-type allowlist. The selected parameter's
[`evalFile()`](https://derivative.ca/UserGuide/Par_Class) resolves expressions,
bindings, project paths, and the source operator's relative path rules.

Both inputs use the same reader pipeline. Directories, missing sequence patterns,
multiple operator references, URLs and virtual filesystem paths produce errors.
A filename expression that resolves to one concrete local file is supported.
Literal special characters in an existing filename are not interpreted as a
glob. The plugin does not expand sequences or discover sidecar files.

## Table and diagnostics

Every output has four columns: `key`, `value`, `group`, `source`. Rows sort by
qualified key, with unique names for repeated fields. Arrays use zero-based
indices; nested object names escape `.`, `[`, `]`, and `\` with a backslash.
ExifTool keys retain its metadata-family and duplicate-instance qualifiers.
Media stream indices identify independent streams.

Examples:

```python
op('metadatain1')['file.size_bytes', 'value']
op('metadatain1')['media.streams[0].codec_name', 'value']
op('metadatain1')['geometry.elements[0].count', 'value']
op('metadatain1')['geometry.calculated.bounds.min_x', 'value']
```

Values use unformatted numbers where readers provide them, retaining integer
precision, text, dates and rational rate strings. Filesystem sizes identify bytes
and timestamps identify Unix seconds in their keys. Coordinate bounds remain in
the file's coordinate system; `geometry.calculated.bounds.units` records this.
Calculated fields have `geometry.calculated.*` keys and the `Calculated` group.

Connect an Info DAT or Info CHOP to this operator through its Operator parameter.
The Info DAT and information popup report status (`idle`, `reading`, `ready`,
`partial`, or `error`), resolved file, row/read counts, read duration, reader
versions, warnings and errors. The Info CHOP exposes `reading`, `rows`, `reads`,
`read_ms`, and `limited`. Missing readers, malformed metadata and unsupported
formats retain valid information from other readers and report `partial`.

Source/settings changes clear old rows immediately; the header remains while
reading (TouchDesigner can clear the entire output on an operator error).
Completed results replace the table atomically. A file change detected
in the background retains the last completed table until the new result is
available. Changes to the selected path are observed whenever the DAT is used.
With Auto Refresh off, path/settings changes still read; on-disk changes require
Refresh. The per-node cache compares path, identity, size, modification/change
timestamps and extraction settings. Refresh always bypasses it.

## Coverage

| Reader | Metadata |
| --- | --- |
| Filesystem | Absolute resolved path, filename, extension, byte size, available creation/modification/change timestamps |
| ExifTool 13.59 | EXIF, IPTC, XMP, GPS, camera/image properties, audio/container tags, EXR multipart headers, FITS headers and other recognized formats |
| ffprobe / FFmpeg 8.0.3 | Media containers, streams, codecs, dimensions, rates, duration, sample rate, channels, chapters and technical tags |
| Native PLY | ASCII, binary little/big endian 1.0; elements, declared counts, properties, comments, including Gaussian-splat properties |
| Native OBJ | Comments and object/group/material declarations; Deep Scan adds vertex/face/normal/texture-coordinate counts and bounds |
| Native XYZ, PTS, CSV, TXT | Comments, declared point count where present, field names or positional `field0`, `field1`, etc.; comma, tab or whitespace delimiters |

Default geometry inspection reads a bounded header/prefix (at most 1 MiB, with
64 KiB lines). PLY declared counts do not require a scan. Deep Scan reads the
whole file, with cancellation and timeout checks. Text bounds require unique
`x`, `y`, `z` column names (case and surrounding spaces ignored), or the first
three columns of an unnamed XYZ/PTS file. Unnamed CSV/TXT data is not assumed to
be XYZ. CSV supports quoted single-line fields; multiline records are unsupported.
OBJ bounds describe stored vertex coordinates. Material references are recorded
without opening their files.

Extraction excludes binary previews and pixel decoding, per-frame analysis,
automatic sidecar discovery and metadata writing. EXR uses ExifTool's embedded
header extraction to retain multiple parts. Media probing may inspect a bounded
packet prefix to identify codecs, but does not enumerate frames or packets.
Unknown formats return filesystem metadata with an explicit limited-coverage
warning. Reader support is determined by the pinned versions, not the filename
extension alone (native geometry selection uses its extension).

Each child reader has a 16 MiB stdout / 64 KiB stderr capture limit. The table is
limited to 20,000 metadata rows, approximately 16 MiB of cell text, 64 KiB per
cell and 32 nested levels. Truncation and timeouts are reported. Superseded reads
are cancelled and their results discarded. Node destruction cancels workers and
kills/reaps active reader processes. TouchDesigner and Python access stays on
the host thread; file inspection and subprocesses run on a C++ worker.

## Build

Prerequisites: macOS 13+, Xcode Command Line Tools (Clang, SDK, make, lipo and
codesign), CMake 3.20+, Python 3.9+ for dependency preparation/tests, and a local
TouchDesigner 2025.33230 installation containing its Python development headers.
The runtime uses macOS `/usr/bin/perl`; configuration verifies `Compress::Zlib`,
`Digest::MD5` and `Digest::SHA`. Homebrew tools are not needed to run the bundle.

From this plugin folder:

```sh
python3 scripts/prepare-dependencies.py --architectures arm64 x86_64
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DTD_APPLICATION=/Applications/TouchDesigner.app -DTD_BUILD_TESTS=ON
cmake --build build/cmake --parallel 4
ctest --test-dir build/cmake --output-on-failure
```

The result is `plugin/MetaData_In.plugin`. For a single architecture, pass that
architecture to both the preparation script and CMake. `OUTPUT_DIRECTORY`
overrides the bundle destination. `METADATA_READER_ROOT` overrides the prepared
resource directory (default `build/readers`); it must contain every requested
architecture. `--build-root` moves all dependency downloads/builds/resources to
another chosen directory. Generated data is ignored and never needed as source.

The [dependency lock](dependencies.json) pins upstream versions, URLs and SHA256
checksums. The [preparation script](scripts/prepare-dependencies.py) verifies
archives, builds static FFmpeg libraries and ffprobe locally, bundles ExifTool's
Perl scripts/modules, and retains upstream licenses. Network, GPL/nonfree
FFmpeg additions, external library autodetection, encoders, muxers and playlist
demuxers are disabled. Only the file protocol is compiled into ffprobe. Runtime
launches use literal argument arrays, a minimal environment, a file-only protocol
whitelist, and disabled ExifTool user configuration.

The build uses relocatable bundle resources, links the host's Python framework
through `@rpath`, and ad-hoc signs ffprobe and the plugin. `TD_ADHOC_SIGN=OFF`
allows a separate signing workflow. Copy the complete bundle, including
`Contents/Resources`; copying only its executable is insufficient.

## Install and use

1. Copy `MetaData_In.plugin` into a `Plugins` folder next to your `.toe` project,
   or into `~/Library/Application Support/Derivative/TouchDesigner099/Plugins`.
2. Restart TouchDesigner, open that project, and create **MetaData_In** from the
   DAT Custom operator list. Review any plugin trust prompt.
3. Set File or Operator. The source is selected automatically. Attach an Info DAT for
   status and warnings. Allow a few frames for background extraction.

For development, a CPlusPlus DAT can load the same bundle with its Plugin
parameter. Use a fresh node/process after rebuilding a loaded binary.

## Validation

Standalone tests cover tagged JPEG/PNG/TIFF, EXR (including multipart), FITS,
MOV/MP4 (multiple streams), WAV/MP3/FLAC, ASCII/binary PLY, OBJ and text points;
Unicode, duplicate/nested tags, integer precision, limits, malformed files,
missing readers, caching, replacement/deletion, cancellation and shutdown.
Fixtures are generated from source-owned tiny images and silence, or constructed
with Python's standard library. Optional media fixture regeneration uses a local
encoding-capable ffmpeg; it is not required for the normal tests or runtime.

The universal build and arm64 test run pass. Version 0.1.1 passed 39 live checks
in TouchDesigner 2025.33230, including the automatic source controls, page layout,
actual DAT cooking and generic operator references. Saved-project reload and
registered `MetadatainDAT` creation were also validated for the initial 0.1.0
release. x86_64 compilation passes; Intel runtime acceptance still requires an
Intel host or working Rosetta.
Physical Intel validation and an isolated standalone source build remain release
gates.

To run the host checks, execute [tests/touchdesigner_validate.py](tests/touchdesigner_validate.py)
in the Textport, then call `run_validation(plugin_path, output_directory)` using
absolute paths. It creates a unique validation COMP, yields between frames and
writes `touchdesigner-results.json`. It exercises the example operator families,
custom COMPs, expressions, bindings, errors, downstream row lookup and slow reads.
After saving/reopening the validation project, execute the script again and call
`validate_reloaded(output_directory)`, which also checks registered Custom DAT
creation. Alternatively, explicitly call
`save_and_reload_validation(output_directory, script_path)` to save a fresh test
project beside that output directory and run those final checks automatically
after restarting. Pass `custom_type='MetadatainDAT'` to `run_validation` to run
the entire suite using registered Custom DAT instances.

## Source and licenses

The adapter, host resolver, extraction engine, reader adapters and table formatter
are separate source files. SDK headers are unchanged copies from TouchDesigner
2025.33230, with their Derivative Shared Use License intact. nlohmann/json 3.12.0
uses the [MIT license](vendor/LICENSE-json.txt). ExifTool is available under Perl's
terms (Artistic or GPL); this bundle retains its upstream GPL license and complete
Perl source. FFmpeg's applicable LGPL notices and pinned source/build information
are retained. See [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).

When distributing compiled binaries, provide corresponding FFmpeg source and
build material as required by its license. The preparation script's retained
archives and license files support that separate release step.

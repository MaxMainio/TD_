# Source and license notices

The convex hull engine, parameters, and its reference/benchmark/host tests were
developed for this workspace. No OpenCV code is included or linked.

The files below are local copies from the maintained Error Diffusion Dither
project at workspace revision `a715cd5`, copied on 2026-09-07. They are included
here so this project builds independently. Their original Derivative copyright
and license headers are preserved in each SDK header.

| File | SHA-256 of the unchanged copy |
| --- | --- |
| TOP_CPlusPlusBase.h | 8fe2764a6db0ecca9ee935f39c0c282452f240313db0c25dce82d8098c07fcf5 |
| CPlusPlus_Common.h | b9f3e2034776d5e03f579a0ae1041a43ac56f6a2733f5bf8cfdf9985072c3ff5 |
| TOPOutputHelper.h | fb110bc00dd67e8aefaa205508c9e7fbb2fbf2f79a5fd6f95e93cdff665b47b1 |

The SDK snapshot uses TOP family API 12 and Common API 1. It was copied from the
maintained plugin rather than replacing headers from the imported upstream
sample snapshot or the newest installed SDK.

TOPOutputHelper.h is the project-local copy of the workspace's maintained Common
output helper. OutputPacking.h, the TOP wrapper, CMake/bundle setup, and worker
lifecycle patterns were adapted from Error Diffusion Dither at that revision.
The output code preserves tested format resolution and packing fixes, while
adapting binary packing and resizing to convex hull output. Relevant future
shared-helper fixes must be reviewed and propagated deliberately to this copy.

All required source is contained in this folder. No symlinks or build dependencies
refer to parent/sibling projects. Compiled bundles, build caches, and local test
projects are not part of source distribution.

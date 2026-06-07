# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 12-rules for Action

These rules apply to every task in this project unless explicitly overridden.
Bias: caution over speed on non-trivial work. Use judgment on trivial tasks.

### Rule 1 — Think Before Coding
State assumptions explicitly. If uncertain, ask rather than guess.
Present multiple interpretations when ambiguity exists.
Push back when a simpler approach exists.
Stop when confused. Name what's unclear.

### Rule 2 — Simplicity First
Minimum code that solves the problem. Nothing speculative.
No features beyond what was asked. No abstractions for single-use code.
Test: would a senior engineer say this is overcomplicated? If yes, simplify.

### Rule 3 — Surgical Changes
Touch only what you must. Clean up only your own mess.
Don't "improve" adjacent code, comments, or formatting.
Don't refactor what isn't broken. Match existing style.

### Rule 4 — Goal-Driven Execution
Define success criteria. Loop until verified.
Don't follow steps. Define success and iterate.
Strong success criteria let you loop independently.

### Rule 5 — Use the model only for judgment calls
Use me for: classification, drafting, summarization, extraction.
Do NOT use me for: routing, retries, deterministic transforms.
If code can answer, code answers.

### Rule 6 — Token budgets are not advisory
Per-task: 4,000 tokens. Per-session: 30,000 tokens.
If approaching budget, summarize and start fresh.
Surface the breach. Do not silently overrun.

### Rule 7 — Surface conflicts, don't average them
If two patterns contradict, pick one (more recent / more tested).
Explain why. Flag the other for cleanup.
Don't blend conflicting patterns.

### Rule 8 — Read before you write
Before adding code, read exports, immediate callers, shared utilities.
"Looks orthogonal" is dangerous. If unsure why code is structured a way, ask.

### Rule 9 — Tests verify intent, not just behavior
Tests must encode WHY behavior matters, not just WHAT it does.
A test that can't fail when business logic changes is wrong.

### Rule 10 — Checkpoint after every significant step
Summarize what was done, what's verified, what's left.
Don't continue from a state you can't describe back.
If you lose track, stop and restate.

### Rule 11 — Match the codebase's conventions, even if you disagree
Conformance > taste inside the codebase.
If you genuinely think a convention is harmful, surface it. Don't fork silently.

### Rule 12 — Fail loud
"Completed" is wrong if anything was skipped silently.
"Tests pass" is wrong if any were skipped.
Default to surfacing uncertainty, not hiding it.

## Project overview

SUSTech EEE5437 Digital Image Compression course repository. C11/C++20 codebase with a custom progressive bitplane codec, a from-scratch JPEG 2000 implementation, and a TCP network transport layer. Built with CMake + Ninja + `clang-cl` on Windows.

## Build commands

```bash
python build.py                  # configure + build + test (full pipeline)
python build.py configure        # CMake configure only
python build.py build            # build all targets
python build.py build hw2        # build a specific scope
python build.py test             # build all + run all tests
python build.py test lib_j2k     # build + run tests matching scope
python build.py run finalproj    # build + run a target (-- to pass args)
python build.py list             # list discovered targets

# Direct Ninja/CMake (after configure):
cd build && ninja finalproj      # build one target
cd build && ninja project_tests  # build all tests
ctest --test-dir build -R hw2    # run matching tests
ctest --test-dir build --output-on-failure
```

Source is in `code/`; build output goes to `build/bin/` (exes) and `build/lib/` (static libs).

## Test conventions

Tests live in `code/tests/`. No external test framework — a single `DIC_EXPECT(expr)` macro from `test_helpers.h` is the entire framework (prints to stderr + `exit(1)` on failure).

**Naming is enforced at CMake time** — wrong names cause a fatal error:

| Pattern | Auto-links to | Example |
|---|---|---|
| `hw<N>_test_*.c` | `hw<N>_lib` | `hw2_test_codec_roundtrip.c` |
| `lib_<name>_test_*.c` | `lib_<name>` | `lib_j2k_test_ebcot.c` |

Tests are auto-discovered via glob — just create the file with the right name, reconfigure, and it's registered. No manual CMakeLists edits needed.

Run a single test binary directly (CWD = repo root):
```bash
build/bin/lib_codec_test_basic.exe
```

## High-level architecture

### Two compression pipelines

**Basic codec** (`code/lib/codec/`): The project's own quality-progressive codec. Pipeline: deinterleave → mandatory RGB RCT → 5/3 DWT → scalar quantize → LL DPCM → full-plane cross-resolution zerotree scan. The dominant pass uses fixed Huffman coding plus Exp-Golomb IZ runs; refinement uses a three-context adaptive binary arithmetic coder with raw fallback. Serialized to incompatible **DICW v7** full-plane streams.

**JPEG 2000** (`code/lib/j2k/`): From-scratch T.800 implementation. Reversible path: RCT → 5/3 DWT (int). Irreversible path: ICT → 9/7 DWT (float) → scalar quant. EBCOT with MQ arithmetic coder, tag-tree packet headers, LRCP packet construction. Supports tiles, ROI Maxshift, multi-layer.

### Library modules (`code/lib/`)

Each subdirectory becomes a static lib (`lib_<name>`) auto-linked via the `project_lib` INTERFACE target:

| Module | Purpose |
|---|---|
| `codec` | Basic codec: encode/decode, full-plane zerotree scan, DICW v7 file I/O, quantization, metrics, subband layout |
| `j2k` | JPEG 2000: codestream, EBCOT, MQ coder, packets, tag-trees, ICT/RCT, ROI, JP2 files, parsing |
| `wavelet` | 5/3 (integer, reversible) and 9/7 (float, irreversible) DWT |
| `image_u8` | `dic_image_u8` struct (width, height, channels, uint8 data) — the universal pixel container |
| `ppm` | PPM/PGM read (P5/P6) and write |
| `net` | TCP/UDP/FILE transport abstraction with platform socket shim (winsock2 on Windows) |
| `errors` | X-macro `dic_status` enum with ~30 error codes |
| `list` | Intrusive doubly-linked list (Linux kernel style) |
| `pqueue` | Priority queue on top of `list_head` |
| `fs` | Cross-platform `fopen` wrapper |

### Application layer (`code/finalproj/`)

CLI built with `cargs` (third-party). Subcommands: `encode`, `decode`, `codec` (roundtrip), `j2k-write`, `j2k-write-tiled`, `j2k-read`, `j2k-info`, `send`, `receive`.

Network flow: sender reads PPM → encodes → serializes quality layers as DICQ v3 → sends a 4-byte LE size plus payload over TCP. Receiver accepts → receives payload → publishes interleaved channel bitplane layers → writes intermediate PPMs during quality progression → writes the final PPM.

### Data flow (basic codec)

```
PPM file → dic_ppm_read() → dic_image_u8
  → codec_basic_encode_image() → codec_basic_encoded_image
  → codec_basic_serialize() / codec_basic_write_file() → DICW v7 buffer/file
  → codec_basic_deserialize() / codec_basic_read_file()
  → codec_basic_decode_image(bitplane_count) → full-resolution dic_image_u8
  → dic_ppm_write() → PPM file
```

### Auto-discovery rules

- `code/hw*/`: all source files → `hwX_impl` library + `hwX` executable (from `main.*`)
- `code/lib/<dir>/`: all source → `lib_<dir>` static library
- `code/tests/`: globbed; naming pattern determines auto-linking
- `code/finalproj/`: manually defined in CMake (not convention-based)
- CMake logic lives in `code/cmake/ProjectConventions.cmake`; overrides in `TargetOverrides.cmake`

## Key conventions

- Third-party deps via `FetchContent` in `code/lib/third_party.cmake`: OpenCV 4.13.0 (minimal: core, imgcodecs, imgproc) and cargs (CLI parser)
- `finalproj` links `hw2_impl` (for Huffman library used by the basic codec scan)
- `hw4` links OpenCV modules
- `lib_net` links `ws2_32` on Windows
- Generated PPM/PGM files, `.bit` files, `.jp2` files are git-ignored
- Viewer tool: `python bin/viwer.py <file.ppm> [--interval MS]` — polling PPM viewer for use with `finalproj receive`
- **After every task that touches C/C++ code**, run `clang-format --dry-run -Werror` on changed files to verify formatting against the project `.clang-format`. Apply fixes with `clang-format -i <file>` if needed.

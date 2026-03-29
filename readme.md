# EEE5437 Digital image compression

This repo holds the code, recommended reading materials, and written assignments for SUSTech EEE5437 Image and Video Compression and Network Communication.

Code is built with a simple CMake-based build system.

All homework code can be built using `build.py`. For details, run `build.py --help`.

## CMake auto-tracking rules

The project entry is [code/CMakeLists.txt](/C:/Users/edsu/workdir/Digital_Image_Compression/code/CMakeLists.txt). It does not maintain a manual source list. Instead, it uses `file(GLOB ...)` and `file(GLOB_RECURSE ... CONFIGURE_DEPENDS ...)` in [code/cmake/ProjectConventions.cmake](/C:/Users/edsu/workdir/Digital_Image_Compression/code/cmake/ProjectConventions.cmake) to discover source files automatically.

Supported source file suffixes:

- `*.c`
- `*.cc`
- `*.cpp`
- `*.cxx`

### Homework files under `code/hw*`

Any directory directly under `code/` whose name matches `hw*` will be treated as one homework, for example `code/hw1`.

Inside each `code/hw*` directory:

- All files with supported suffixes in that directory and its subdirectories are automatically tracked.
- A file named `main.c`, `main.cc`, `main.cpp`, or `main.cxx` is treated as the executable entry.
- All other source files are built into the homework implementation library.
- If more than one `main.*` exists in the same homework directory, CMake fails.

So, to make a homework source file be tracked automatically, put it under `code/hw*/` and use one of the supported suffixes. If you want it to be the executable entry, name it `main.*`.

### Library files under `code/lib`

The `code/lib` directory is also tracked automatically.

- A source file placed directly under `code/lib/`, such as `code/lib/foo.cpp`, becomes a library target named `lib_foo`.
- Each non-hidden subdirectory under `code/lib/` is scanned recursively.
- If a subdirectory contains supported source files, it becomes one library target named after the directory, for example `code/lib/some_lib/...` becomes `lib_some_lib`.

In practice:

- `code/lib/<name>.cpp` -> `lib_<name>`
- `code/lib/<dir>/**/*.cpp` -> `lib_<dir>`

### Test files under `code/tests`

All supported source files under `code/tests/` and its subdirectories are discovered automatically as test executables.

But the test file name must follow one of these patterns:

- `hwX_test`
- `hwX_test_*`
- `lib_XXX_test`
- `lib_XXX_test_*`

Examples:

- `code/tests/hw1_test_summary.cpp`
- `code/tests/lib_some_lib_test_runs.cpp`

Meaning:

- `hw1_test_xxx.cpp` links automatically against homework target `hw1`
- `lib_some_lib_test_xxx.cpp` links automatically against library target `lib_some_lib`

If a test file name does not match these rules, CMake fails.

## Quick reference

To have a file automatically tracked by CMake:

- Homework source: place it under `code/hw*/` and name it `*.c`, `*.cc`, `*.cpp`, or `*.cxx`
- Homework entry: place it under `code/hw*/` and name it `main.*`
- Library source: place it under `code/lib/` or `code/lib/<module>/` and use a supported suffix
- Test source: place it under `code/tests/` and name it `hwX_test.cpp`, `hwX_test_*.cpp`, `lib_XXX_test.cpp`, or `lib_XXX_test_*.cpp`

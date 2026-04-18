include_guard(GLOBAL)

# Centralized entry point for third-party dependencies.
# Any library that is not maintained in this repository should be declared
# here through FetchContent and exposed as a normal CMake target.
#
# Example:
# FetchContent_Declare(
#     fmt
#     GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#     GIT_TAG 11.0.2
# )
# FetchContent_MakeAvailable(fmt)
#
# After that, link the dependency like any other target:
# target_link_libraries(your_target PRIVATE fmt::fmt)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_js OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_ts OFF CACHE BOOL "" FORCE)
set(BUILD_LIST "core,imgcodecs,imgproc" CACHE STRING "" FORCE)
set(WITH_AVIF OFF CACHE BOOL "" FORCE)
set(WITH_JASPER OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_MSMF OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
set(WITH_IPP OFF CACHE BOOL "" FORCE)
set(WITH_ITT OFF CACHE BOOL "" FORCE)
set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
set(WITH_OPENCL_SVM OFF CACHE BOOL "" FORCE)
set(WITH_OPENCLAMDFFT OFF CACHE BOOL "" FORCE)
set(WITH_OPENCLAMDBLAS OFF CACHE BOOL "" FORCE)
set(WITH_IMGCODEC_HDR OFF CACHE BOOL "" FORCE)
set(WITH_IMGCODEC_PFM OFF CACHE BOOL "" FORCE)
set(WITH_IMGCODEC_PXM OFF CACHE BOOL "" FORCE)
set(WITH_IMGCODEC_SUNRASTER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    opencv
    GIT_REPOSITORY https://github.com/opencv/opencv.git
    GIT_TAG 4.13.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(opencv)

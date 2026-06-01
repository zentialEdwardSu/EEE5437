include_guard(GLOBAL)

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

FetchContent_Declare(
    cargs
    GIT_REPOSITORY https://github.com/likle/cargs.git
    GIT_TAG stable
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(opencv)

FetchContent_GetProperties(cargs)
if(NOT cargs_POPULATED)
    FetchContent_Populate(cargs)
endif()

add_library(
    cargs
    STATIC
    "${cargs_SOURCE_DIR}/src/cargs.c"
)
target_include_directories(cargs PUBLIC "${cargs_SOURCE_DIR}/include")

if(NOT TARGET cargs::cargs)
    add_library(cargs::cargs ALIAS cargs)
endif()

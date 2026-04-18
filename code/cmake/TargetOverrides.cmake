function(dic_apply_target_overrides)
    if(TARGET hw4_impl)
        target_link_libraries(hw4_impl PUBLIC hw1_impl hw2_impl opencv_core opencv_imgcodecs opencv_imgproc)
        target_include_directories(
            hw4_impl
            PUBLIC
            "${OpenCV_SOURCE_DIR}/include"
            "${OpenCV_SOURCE_DIR}/modules/core/include"
            "${OpenCV_SOURCE_DIR}/modules/imgcodecs/include"
            "${OpenCV_SOURCE_DIR}/modules/imgproc/include"
            "${OpenCV_BINARY_DIR}"
            "${CMAKE_BINARY_DIR}"
        )
    endif()
endfunction()

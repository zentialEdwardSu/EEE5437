include_guard(GLOBAL)

include(FetchContent)

function(dic_collect_sources base_dir out_var)
    if(NOT EXISTS "${base_dir}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(
        GLOB_RECURSE dic_sources
        CONFIGURE_DEPENDS
        "${base_dir}/*.c"
        "${base_dir}/*.cc"
        "${base_dir}/*.cpp"
        "${base_dir}/*.cxx"
    )

    list(SORT dic_sources)
    set(${out_var} "${dic_sources}" PARENT_SCOPE)
endfunction()

function(dic_apply_interface_layout target include_dir)
    target_include_directories(
        "${target}"
        INTERFACE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/lib"
        "${include_dir}"
    )
    target_link_libraries("${target}" INTERFACE project_lib)
endfunction()

function(dic_apply_target_layout target include_dir)
    target_include_directories(
        "${target}"
        PUBLIC
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/lib"
        "${include_dir}"
    )

    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options("${target}" PRIVATE /W4 /permissive-)
    else()
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(dic_split_main_sources out_main_var out_other_var)
    set(dic_main_sources "")
    set(dic_other_sources "")

    foreach(source IN LISTS ARGN)
        get_filename_component(source_stem "${source}" NAME_WE)
        if(source_stem STREQUAL "main")
            list(APPEND dic_main_sources "${source}")
        else()
            list(APPEND dic_other_sources "${source}")
        endif()
    endforeach()

    set(${out_main_var} "${dic_main_sources}" PARENT_SCOPE)
    set(${out_other_var} "${dic_other_sources}" PARENT_SCOPE)
endfunction()

function(dic_register_library_tree base_dir out_targets_var)
    set(dic_module_targets "")

    file(
        GLOB dic_root_sources
        CONFIGURE_DEPENDS
        "${base_dir}/*.c"
        "${base_dir}/*.cc"
        "${base_dir}/*.cpp"
        "${base_dir}/*.cxx"
    )
    list(SORT dic_root_sources)

    foreach(source_file IN LISTS dic_root_sources)
        get_filename_component(module_name "${source_file}" NAME_WE)
        set(target_name "lib_${module_name}")

        if(TARGET "${target_name}")
            message(FATAL_ERROR "Duplicate library module target detected: ${target_name}")
        endif()

        add_library("${target_name}" STATIC "${source_file}")
        dic_apply_target_layout("${target_name}" "${base_dir}")
        list(APPEND dic_module_targets "${target_name}")
    endforeach()

    file(GLOB dic_lib_children RELATIVE "${base_dir}" "${base_dir}/*")
    list(SORT dic_lib_children)

    foreach(child IN LISTS dic_lib_children)
        if(NOT IS_DIRECTORY "${base_dir}/${child}")
            continue()
        endif()

        if(child MATCHES "^\\.")
            continue()
        endif()

        dic_collect_sources("${base_dir}/${child}" module_sources)
        if(NOT module_sources)
            continue()
        endif()

        set(target_name "lib_${child}")
        if(TARGET "${target_name}")
            message(FATAL_ERROR "Duplicate library module target detected: ${target_name}")
        endif()

        add_library("${target_name}" STATIC ${module_sources})
        dic_apply_target_layout("${target_name}" "${base_dir}/${child}")
        list(APPEND dic_module_targets "${target_name}")
    endforeach()

    set(${out_targets_var} "${dic_module_targets}" PARENT_SCOPE)
endfunction()

function(dic_register_third_party_dependencies lib_root)
    set(third_party_file "${lib_root}/third_party.cmake")
    if(EXISTS "${third_party_file}")
        include("${third_party_file}")
    endif()
endfunction()

function(dic_register_libraries lib_root)
    set(dic_module_targets "")

    dic_register_library_tree("${lib_root}" root_module_targets)
    if(root_module_targets)
        list(APPEND dic_module_targets ${root_module_targets})
    endif()

    add_library(project_lib INTERFACE)
    target_include_directories(
        project_lib
        INTERFACE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/lib"
    )

    if(dic_module_targets)
        target_link_libraries(project_lib INTERFACE ${dic_module_targets})
    endif()

    dic_register_third_party_dependencies("${lib_root}")

    set_property(GLOBAL PROPERTY DIC_LIBRARY_TARGETS "${dic_module_targets}")
endfunction()

function(dic_add_homework homework_name homework_dir out_target_var)
    dic_collect_sources("${homework_dir}" homework_sources)

    if(NOT homework_sources)
        message(STATUS "Skipping ${homework_name}: no source files found.")
        set(${out_target_var} "" PARENT_SCOPE)
        return()
    endif()

    dic_split_main_sources(main_sources other_sources ${homework_sources})

    list(LENGTH main_sources main_count)
    if(main_count GREATER 1)
        message(
            FATAL_ERROR
            "${homework_name} contains multiple main translation units. "
            "Keep only one main.* file in each homework directory."
        )
    endif()

    add_library("${homework_name}_lib" INTERFACE)
    dic_apply_interface_layout("${homework_name}_lib" "${homework_dir}")

    if(other_sources)
        add_library("${homework_name}_impl" STATIC ${other_sources})
        dic_apply_target_layout("${homework_name}_impl" "${homework_dir}")
        target_link_libraries("${homework_name}_impl" PUBLIC project_lib)
        target_link_libraries("${homework_name}_lib" INTERFACE "${homework_name}_impl")
    endif()

    if(main_sources)
        add_executable("${homework_name}" ${main_sources})
        dic_apply_target_layout("${homework_name}" "${homework_dir}")
        target_link_libraries("${homework_name}" PRIVATE "${homework_name}_lib")
        set(build_target "${homework_name}")
    else()
        add_custom_target("${homework_name}")
        if(TARGET "${homework_name}_impl")
            add_dependencies("${homework_name}" "${homework_name}_impl")
        endif()
        set(build_target "${homework_name}")
    endif()

    set(${out_target_var} "${build_target}" PARENT_SCOPE)
endfunction()

function(dic_register_homeworks project_root)
    set(dic_homework_targets "")
    set(dic_homework_impl_targets "")
    set(dic_homework_executable_targets "")

    file(GLOB dic_hw_dirs RELATIVE "${project_root}" "${project_root}/hw*")
    list(SORT dic_hw_dirs)

    foreach(entry IN LISTS dic_hw_dirs)
        if(NOT IS_DIRECTORY "${project_root}/${entry}")
            continue()
        endif()

        dic_add_homework("${entry}" "${project_root}/${entry}" build_target)
        if(build_target)
            list(APPEND dic_homework_targets "${build_target}")
        endif()
        if(TARGET "${entry}_impl")
            list(APPEND dic_homework_impl_targets "${entry}_impl")
        endif()
        if(TARGET "${entry}")
            get_target_property(entry_type "${entry}" TYPE)
            if(entry_type STREQUAL "EXECUTABLE")
                list(APPEND dic_homework_executable_targets "${entry}")
            endif()
        endif()
    endforeach()

    add_library(project_homeworks INTERFACE)
    target_include_directories(
        project_homeworks
        INTERFACE
        "${PROJECT_SOURCE_DIR}"
    )
    if(dic_homework_impl_targets)
        target_link_libraries(project_homeworks INTERFACE ${dic_homework_impl_targets})
    endif()
    foreach(executable_target IN LISTS dic_homework_executable_targets)
        target_link_libraries("${executable_target}" PRIVATE project_homeworks)
    endforeach()

    set_property(GLOBAL PROPERTY DIC_HOMEWORK_BUILD_TARGETS "${dic_homework_targets}")
    set_property(GLOBAL PROPERTY DIC_HOMEWORK_LIBRARY_TARGETS "${dic_homework_impl_targets}")
endfunction()

function(dic_add_test test_source out_target_var)
    get_filename_component(test_name "${test_source}" NAME_WE)
    get_filename_component(test_dir "${test_source}" DIRECTORY)

    add_executable("${test_name}" "${test_source}")
    dic_apply_target_layout("${test_name}" "${test_dir}")
    target_link_libraries("${test_name}" PRIVATE project_lib)
    if(TARGET project_homeworks)
        target_link_libraries("${test_name}" PRIVATE project_homeworks)
    endif()

    if(test_name MATCHES "^(hw[0-9A-Za-z_]+)_test_.+")
        set(homework_name "${CMAKE_MATCH_1}")
        if(NOT TARGET "${homework_name}_lib")
            message(
                FATAL_ERROR
                "Test ${test_name} references ${homework_name}, but that homework target does not exist."
            )
        endif()
        target_link_libraries("${test_name}" PRIVATE "${homework_name}_lib")
    elseif(test_name MATCHES "^lib_([0-9A-Za-z_]+)_test_.+")
        set(module_name "${CMAKE_MATCH_1}")
        set(module_target "lib_${module_name}")
        if(NOT TARGET "${module_target}")
            message(
                FATAL_ERROR
                "Test ${test_name} references ${module_target}, but that library target does not exist."
            )
        endif()
        target_link_libraries("${test_name}" PRIVATE "${module_target}")
    else()
        message(
            FATAL_ERROR
            "Test ${test_name} does not match the required naming rules: "
            "hwX_test_* or lib_XXX_test_*."
        )
    endif()

    add_test(NAME "${test_name}" COMMAND "${test_name}")
    set_tests_properties(
        "${test_name}"
        PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/.."
    )
    set(${out_target_var} "${test_name}" PARENT_SCOPE)
endfunction()

function(dic_register_tests test_root)
    set(dic_test_targets "")

    dic_collect_sources("${test_root}" test_sources)
    foreach(test_source IN LISTS test_sources)
        dic_add_test("${test_source}" test_target)
        if(test_target)
            list(APPEND dic_test_targets "${test_target}")
        endif()
    endforeach()

    set_property(GLOBAL PROPERTY DIC_TEST_BUILD_TARGETS "${dic_test_targets}")
endfunction()

function(dic_create_aggregate_target target_name property_name)
    get_property(aggregate_targets GLOBAL PROPERTY "${property_name}")
    if(aggregate_targets)
        add_custom_target("${target_name}" DEPENDS ${aggregate_targets})
    else()
        add_custom_target("${target_name}")
    endif()
endfunction()

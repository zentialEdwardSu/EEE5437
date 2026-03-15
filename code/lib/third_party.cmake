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

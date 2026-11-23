cmake_minimum_required(VERSION 3.20)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" C_DOCC_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(c-docc VERSION ${C_DOCC_VERSION} LANGUAGES C CXX)

find_path(CMARK_INCLUDE_DIR NAMES cmark.h REQUIRED)
find_library(CMARK_LIBRARY NAMES cmark REQUIRED)
find_path(UTF8PROC_INCLUDE_DIR NAMES utf8proc.h REQUIRED)
find_library(UTF8PROC_LIBRARY NAMES utf8proc REQUIRED)
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION MATCHES "^${LLVM_VERSION_MAJOR}[.]")
    set(C_DOCC_CLANG "${CMAKE_C_COMPILER}")
else()
    find_program(C_DOCC_CLANG NAMES clang-${LLVM_VERSION_MAJOR} clang REQUIRED)
endif()
execute_process(
    COMMAND "${C_DOCC_CLANG}" -print-resource-dir
    OUTPUT_VARIABLE C_DOCC_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
if(WIN32)
    file(TO_CMAKE_PATH "${C_DOCC_RESOURCE_DIR}" C_DOCC_RESOURCE_DIR)
endif()

add_executable(c-docc src/main.cpp src/Analyzer.cpp src/Graph.cpp src/Docs.cpp)
target_compile_features(c-docc PRIVATE cxx_std_20)
if(NOT WIN32)
    # Keep these paths in compile_commands.json when the Nix wrapper adds them implicitly.
    list(REMOVE_ITEM CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES
        ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} "${CMARK_INCLUDE_DIR}" "${UTF8PROC_INCLUDE_DIR}")
endif()
target_include_directories(c-docc SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_include_directories(c-docc SYSTEM PRIVATE "${CMARK_INCLUDE_DIR}" "${UTF8PROC_INCLUDE_DIR}")
target_link_libraries(c-docc PRIVATE "${CMARK_LIBRARY}" "${UTF8PROC_LIBRARY}")
target_compile_definitions(c-docc PRIVATE
    C_DOCC_RESOURCE_DIR="${C_DOCC_RESOURCE_DIR}"
    C_DOCC_VERSION="${PROJECT_VERSION}"
)
if(TARGET clang-cpp AND TARGET LLVM)
    target_link_libraries(c-docc PRIVATE clang-cpp LLVM)
else()
    target_link_libraries(c-docc PRIVATE clangTooling clangToolingSyntax LLVMSupport)
endif()
if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    get_filename_component(C_DOCC_TOOLCHAIN_BIN "${C_DOCC_CLANG}" DIRECTORY)
    install(TARGETS c-docc
        RUNTIME_DEPENDENCIES
            DIRECTORIES "${C_DOCC_TOOLCHAIN_BIN}"
            PRE_EXCLUDE_REGEXES "^(api|ext)-ms-"
            POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]"
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY "${C_DOCC_RESOURCE_DIR}/include" DESTINATION bin/clang-resource)
    install(FILES LICENSE DESTINATION .)
else()
    install(TARGETS c-docc RUNTIME DESTINATION bin)
endif()

# Include tutorial declarations in the compilation database for documentation checks.
add_library(c-docc-tutorial-examples OBJECT EXCLUDE_FROM_ALL
    examples/photo-backup/retries.c
    examples/parcel-tracking/delivery_estimate.c
    examples/parcel-tracking/depot_sorting.c
)
set_target_properties(c-docc-tutorial-examples PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME snapshot-diff
        COMMAND "${CMAKE_COMMAND}"
            -DPROGRAM=$<TARGET_FILE:c-docc>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-snapshots
            -DCLANG=${C_DOCC_CLANG}
            -DVERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshot-diff.cmake
    )
    add_test(NAME docs-links
        COMMAND "${CMAKE_COMMAND}"
            -DPROGRAM=$<TARGET_FILE:c-docc>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-doc-links
            -DCLANG=${C_DOCC_CLANG}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/docs-links.cmake
    )
endif()

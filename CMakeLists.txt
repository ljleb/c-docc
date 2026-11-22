cmake_minimum_required(VERSION 3.20)
project(dagdocdiff LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION MATCHES "^${LLVM_VERSION_MAJOR}[.]")
    set(DAGDOCDIFF_CLANG "${CMAKE_C_COMPILER}")
else()
    find_program(DAGDOCDIFF_CLANG NAMES clang-${LLVM_VERSION_MAJOR} clang REQUIRED)
endif()
execute_process(
    COMMAND "${DAGDOCDIFF_CLANG}" -print-resource-dir
    OUTPUT_VARIABLE DAGDOCDIFF_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

add_executable(dagdocdiff src/main.cpp src/Analyzer.cpp src/Graph.cpp)
target_compile_features(dagdocdiff PRIVATE cxx_std_20)
target_include_directories(dagdocdiff SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(dagdocdiff PRIVATE DAGDOCDIFF_RESOURCE_DIR="${DAGDOCDIFF_RESOURCE_DIR}")
if(TARGET clang-cpp AND TARGET LLVM)
    target_link_libraries(dagdocdiff PRIVATE clang-cpp LLVM)
else()
    target_link_libraries(dagdocdiff PRIVATE clangTooling clangToolingSyntax LLVMSupport)
endif()
install(TARGETS dagdocdiff RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME snapshot-diff
        COMMAND "${CMAKE_COMMAND}"
            -DPROGRAM=$<TARGET_FILE:dagdocdiff>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-snapshots
            -DCLANG=${DAGDOCDIFF_CLANG}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/snapshot-diff.cmake
    )
endif()

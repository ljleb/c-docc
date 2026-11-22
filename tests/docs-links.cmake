function(run_cli expected text)
    execute_process(COMMAND "${PROGRAM}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE diagnostics)
    if(NOT status EQUAL expected OR NOT "${out}${diagnostics}" MATCHES "${text}")
        message(FATAL_ERROR "Expected status ${expected} and message ${text}, got ${status}:\n${out}${diagnostics}")
    endif()
endfunction()

function(run_interactive answers expected)
    file(WRITE "${WORK_DIR}/answers.txt" "${answers}")
    execute_process(COMMAND "${PROGRAM}" approve ${path_options}
        WORKING_DIRECTORY "${WORK_DIR}"
        INPUT_FILE "${WORK_DIR}/answers.txt"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics)
    if(NOT status EQUAL 0 OR NOT output MATCHES "${expected}")
        message(FATAL_ERROR "Expected interactive approval to report ${expected}, got ${status}:\n${output}${diagnostics}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${WORK_DIR}/docs" "${WORK_DIR}/build")
file(REMOVE "${WORK_DIR}/docs/.dagdocdiff-approvals.json"
    "${WORK_DIR}/.dagdocdiff-approvals.json" "${WORK_DIR}/compile_commands.json")
file(WRITE "${WORK_DIR}/api.hpp" [=[
namespace demo {
int parse(int);
int parse(double);
static int helper() { return 1; }
struct Parser {
    int run();
};
}
]=])
file(WRITE "${WORK_DIR}/a.cpp" [=[
#include "api.hpp"
int demo::parse(int value) { return value + helper(); }
int demo::Parser::run() { return parse(1); }
int read_value() {
    return demo::parse(2);
}
]=])
file(WRITE "${WORK_DIR}/b.cpp" [=[
#include "api.hpp"
int demo::parse(double value) { return int(value); }
]=])
file(WRITE "${WORK_DIR}/build/compile_commands.json"
    "[{\"directory\":\"${WORK_DIR}\",\"file\":\"${WORK_DIR}/a.cpp\",\"arguments\":[\"${CLANG}\",\"-std=c++20\",\"-c\",\"${WORK_DIR}/a.cpp\"]},
      {\"directory\":\"${WORK_DIR}\",\"file\":\"${WORK_DIR}/b.cpp\",\"arguments\":[\"${CLANG}\",\"-std=c++20\",\"-c\",\"${WORK_DIR}/b.cpp\"]}]")

set(page "${WORK_DIR}/docs/guide.md")
set(path_options "--source-dir=${WORK_DIR}" --build-dir build --docs-dir docs)
set(markdown [=[
# Guide

## Header

[Parse integer input](../api.hpp#L2). The [header helper](../api.hpp#L4) is local to each translation unit.

## Result

[Read the value](../a.cpp#L4).

~~~markdown
[Example text](../api.hpp#L404)
~~~
]=])
file(WRITE "${page}" "${markdown}")

run_cli(2 "missing value for --build-dir" check --build-dir)
run_cli(1 "approval required" check ${path_options})
run_cli(0 "Recorded review for guide.md" approve ./guide.md ${path_options})
run_cli(0 "Approved documentation sections: 2\n  guide.md:3: Guide / Header\n  guide.md:7: Guide / Result\n"
    check ${path_options})
run_cli(0 "Recorded review" approve ${path_options} --reason "Reviewed the caller" guide.md "Guide / Result")
run_cli(0 "Approved documentation sections: 2" check ${path_options})
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" approvals)
string(JSON header_reason GET "${approvals}" sections 0 reason)
string(JSON result_reason GET "${approvals}" sections 1 reason)
if(NOT header_reason STREQUAL "" OR NOT result_reason STREQUAL "Reviewed the caller")
    message(FATAL_ERROR "Expected the optional reason to be empty or recorded as supplied")
endif()
if(NOT approvals MATCHES "a.cpp::demo::helper" OR NOT approvals MATCHES "b.cpp::demo::helper")
    message(FATAL_ERROR "Header link did not track both translation units")
endif()

file(READ "${WORK_DIR}/api.hpp" header)
string(REPLACE "return 1" "return 2" changed "${header}")
file(WRITE "${WORK_DIR}/api.hpp" "${changed}")
run_cli(1 "read_value.* -> demo::parse.* -> a.cpp::demo::helper" check ${path_options})
run_interactive("?\nr\ny\n" "Enter y, r, or q.*Left for revision.*Recorded review for guide.md / Guide / Result")
run_cli(1 "Guide / Header: code changed" check ${path_options})
run_interactive("q\n" "Guide / Header")
run_cli(1 "Guide / Header: code changed" check ${path_options})
run_interactive("y\n" "Recorded review for guide.md / Guide / Header")
run_cli(0 "Approved documentation sections: 2" check ${path_options})
run_interactive("" "No sections need approval")

string(REPLACE "int parse(int);\nint parse(double);" "int parse(double);\nint parse(int);" swapped "${changed}")
file(WRITE "${WORK_DIR}/api.hpp" "${swapped}")
run_cli(1 "previously demo::parse" check ${path_options})
file(WRITE "${WORK_DIR}/api.hpp" "${changed}")

file(APPEND "${WORK_DIR}/a.cpp" "int x() { return 0; } int y() { return 0; }\n")
file(WRITE "${page}" "${markdown}\n[Ambiguous](../a.cpp#L7)\n")
run_cli(1 "multiple declarations" check ${path_options})
run_interactive("y\n" "Fix the source links before approving this section")
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" before)
run_cli(1 "resolve source links before approving this page" approve guide.md ${path_options} --reason "Blocked")
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "Approving a page with a broken link changed its approvals")
endif()
file(WRITE "${page}" "${markdown}\n[Body](../a.cpp#L5)\n")
run_cli(1 "no indexed declaration" check ${path_options})
file(WRITE "${page}" "${markdown}\n[Member](../api.hpp#L6)\n")
run_cli(1 "no indexed declaration" check ${path_options})

file(WRITE "${page}" "# Guide\n\nThe linked sections were removed.\n")
run_cli(1 "approved section or its source links were removed" check ${path_options})
run_cli(0 "Recorded review" approve ${path_options} guide.md "Guide / Header")
run_interactive("y\n" "approve the removal.*Recorded review for guide.md / Guide / Result")
run_cli(0 "Approved documentation sections: 0" check ${path_options})

file(WRITE "${page}" "${markdown}")
run_cli(0 "Recorded review for guide.md" approve guide.md ${path_options})
file(WRITE "${page}" "# Guide\n\nThe linked sections were removed.\n")
run_cli(0 "Recorded review for guide.md" approve guide.md ${path_options})
run_cli(0 "Approved documentation sections: 0" check ${path_options})

file(COPY "${WORK_DIR}/build/compile_commands.json" DESTINATION "${WORK_DIR}")
file(WRITE "${WORK_DIR}/local.md" "# Local\n\n[Parse integer input](api.hpp#L2).\n")
run_cli(1 "approval required" check)
run_cli(0 "Recorded review for local.md / Local" approve local.md Local)
run_cli(0 "Approved documentation sections: 1" check)

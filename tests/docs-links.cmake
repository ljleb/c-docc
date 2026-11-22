function(run_docs expected text)
    execute_process(COMMAND "${PROGRAM}" docs ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE diagnostics)
    if(NOT status EQUAL expected OR NOT "${out}${diagnostics}" MATCHES "${text}")
        message(FATAL_ERROR "Expected status ${expected} and message ${text}, got ${status}:\n${out}${diagnostics}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${WORK_DIR}/docs")
file(REMOVE "${WORK_DIR}/docs/.dagdocdiff-approvals.json")
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
file(WRITE "${WORK_DIR}/compile_commands.json"
    "[{\"directory\":\"${WORK_DIR}\",\"file\":\"${WORK_DIR}/a.cpp\",\"arguments\":[\"${CLANG}\",\"-std=c++20\",\"-c\",\"${WORK_DIR}/a.cpp\"]},
      {\"directory\":\"${WORK_DIR}\",\"file\":\"${WORK_DIR}/b.cpp\",\"arguments\":[\"${CLANG}\",\"-std=c++20\",\"-c\",\"${WORK_DIR}/b.cpp\"]}]")

set(page "${WORK_DIR}/docs/guide.md")
set(roots "${WORK_DIR}" "${WORK_DIR}" "${WORK_DIR}/docs")
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

run_docs(1 "approval required" check ${roots})
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Header" "Reviewed the declarations")
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Result" "Reviewed the caller")
run_docs(0 "Approved documentation sections: 2" check ${roots})
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" approvals)
if(NOT approvals MATCHES "a.cpp::demo::helper" OR NOT approvals MATCHES "b.cpp::demo::helper")
    message(FATAL_ERROR "Header link did not track both translation units")
endif()

file(READ "${WORK_DIR}/api.hpp" header)
string(REPLACE "return 1" "return 2" changed "${header}")
file(WRITE "${WORK_DIR}/api.hpp" "${changed}")
run_docs(1 "read_value.* -> demo::parse.* -> a.cpp::demo::helper" check ${roots})
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Header" "Reviewed the changed helper")
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Result" "Reviewed the changed result")
run_docs(0 "Approved documentation sections: 2" check ${roots})

string(REPLACE "int parse(int);\nint parse(double);" "int parse(double);\nint parse(int);" swapped "${changed}")
file(WRITE "${WORK_DIR}/api.hpp" "${swapped}")
run_docs(1 "previously demo::parse" check ${roots})
file(WRITE "${WORK_DIR}/api.hpp" "${changed}")

file(APPEND "${WORK_DIR}/a.cpp" "int x() { return 0; } int y() { return 0; }\n")
file(WRITE "${page}" "${markdown}\n[Ambiguous](../a.cpp#L7)\n")
run_docs(1 "multiple declarations" check ${roots})
file(WRITE "${page}" "${markdown}\n[Body](../a.cpp#L5)\n")
run_docs(1 "no indexed declaration" check ${roots})
file(WRITE "${page}" "${markdown}\n[Member](../api.hpp#L6)\n")
run_docs(1 "no indexed declaration" check ${roots})

file(WRITE "${page}" "# Guide\n\nThe linked sections were removed.\n")
run_docs(1 "approved section or its source links were removed" check ${roots})
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Header" "Reviewed the removal")
run_docs(0 "Recorded review" approve ${roots} guide.md "Guide / Result" "Reviewed the removal")
run_docs(0 "Approved documentation sections: 0" check ${roots})

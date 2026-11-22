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

file(REMOVE_RECURSE "${WORK_DIR}/navigation" "${WORK_DIR}/ranges")
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

[Read **again**
please](../a.cpp#L4).

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
run_cli(1 [=[guide[.]md:9: Guide / Result: "Read the value": code changed: read_value.* -> demo::parse.* -> a[.]cpp::demo::helper[(][)] [(]api[.]hpp:4[)].*guide[.]md:11: Guide / Result: "Read again please": code changed:]=]
    check ${path_options})
run_interactive("?\nr\ny\n" "Enter y, r, or q.*Left for revision.*Recorded review for guide.md / Guide / Result")
run_cli(1 [=[Guide / Header: "header helper": code changed:]=] check ${path_options})
run_interactive("q\n" "Guide / Header")
run_cli(1 [=[Guide / Header: "header helper": code changed:]=] check ${path_options})
run_interactive("y\n" "Recorded review for guide.md / Guide / Header")
run_cli(0 "Approved documentation sections: 2" check ${path_options})
run_interactive("" "No sections need approval")

string(REPLACE "int parse(int);\nint parse(double);" "int parse(double);\nint parse(int);" swapped "${changed}")
file(WRITE "${WORK_DIR}/api.hpp" "${swapped}")
run_cli(1 [=["Parse integer input": declaration changed: ../api[.]hpp#L2]=] check ${path_options})
file(WRITE "${WORK_DIR}/api.hpp" "${changed}")

file(APPEND "${WORK_DIR}/a.cpp" "int x() { return 0; } int y() { return 0; }\n")
file(WRITE "${page}" "${markdown}\n[Ambiguous](../a.cpp#L7)\n")
run_cli(1 [=["Ambiguous": multiple declarations]=] check ${path_options})
run_interactive("y\n" "Fix the broken links before approving this section")
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" before)
run_cli(1 "resolve links before approving this page" approve guide.md ${path_options} --reason "Blocked")
file(READ "${WORK_DIR}/docs/.dagdocdiff-approvals.json" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "Approving a page with a broken link changed its approvals")
endif()
file(WRITE "${page}" "${markdown}\n[Both functions](../a.cpp#L7-L7)\n")
run_cli(0 "Recorded review for guide.md" approve guide.md ${path_options})
run_cli(0 "Approved documentation sections: 2" check ${path_options})
file(WRITE "${page}" "${markdown}\n[Body](../a.cpp#L5)\n[Member](../api.hpp#L6)\n")
run_cli(0 "Recorded review for guide.md" approve guide.md ${path_options})
run_cli(0 "Approved documentation sections: 2" check ${path_options})

file(WRITE "${page}" "# Guide\n\nThe linked sections were removed.\n")
run_cli(1 "approved section or its tracked links were removed" check ${path_options})
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

set(navigation "${WORK_DIR}/navigation")
file(MAKE_DIRECTORY "${navigation}/topics")
file(WRITE "${navigation}/topics/README.md" "# More details\n")
set(topic "${navigation}/topics/Build notes.md")
set(topic_contents [=[
# Notes

## Café *Build*!
First section.

### Subsection
Nested details.

## Café Build!
Second section.

## Other topic
Unrelated details.
]=])
file(WRITE "${topic}" "${topic_contents}")
set(navigation_page [=[
# Start here

[Same page](#start-here).
[Directory index](topics/#more-details).
[Other page](topics/Build%20notes.md#caf%C3%A9-build).
[Duplicate heading](topics/Build%20notes.md#caf%C3%A9-build-1).
[Project-root link](/local.md#local).
[External link](https://example.invalid/not.md#missing).

~~~markdown
[Sample](missing.md)
~~~

## Whole files

[Build notes](topics/Build%20notes.md).
[Full header](../api.hpp).
[Topics directory](topics/).
]=])
file(WRITE "${navigation}/README.md" "${navigation_page}")
run_cli(1 "approval required" check --docs-dir navigation)
run_cli(0 "Recorded review for README.md" approve README.md --docs-dir navigation)
run_cli(0 "Approved documentation sections: 2" check --docs-dir navigation)

string(REPLACE "Unrelated details." "Additional unrelated details." outside "${topic_contents}")
file(WRITE "${topic}" "${outside}")
run_cli(1 [=["Build notes": linked file changed: topics/Build%20notes[.]md]=] check --docs-dir navigation)
run_cli(0 "Recorded review" approve README.md "Start here / Whole files" --docs-dir navigation)
run_cli(0 "Approved documentation sections: 2" check --docs-dir navigation)

string(REPLACE "Nested details." "More nested details." nested "${outside}")
file(WRITE "${topic}" "${nested}")
run_cli(1 [=["Other page": linked section changed: topics/Build%20notes[.]md#caf%C3%A9-build]=]
    check --docs-dir navigation)
file(APPEND "${WORK_DIR}/api.hpp" "\n// A comment in the whole-file target.\n")
run_cli(1 [=["Full header": linked file changed: ../api[.]hpp]=] check --docs-dir navigation)
run_cli(0 "Recorded review for README.md" approve README.md --docs-dir navigation)
run_cli(0 "Approved documentation sections: 2" check --docs-dir navigation)

file(RENAME "${topic}" "${navigation}/topics/Getting started.md")
run_cli(1 [=[README[.]md:5: Start here: "Other page": local link target does not exist: topics/Build%20notes[.]md#caf%C3%A9-build]=]
    check --docs-dir navigation)
string(REPLACE "Build%20notes.md" "Getting%20started.md" updated_navigation "${navigation_page}")
file(WRITE "${navigation}/README.md" "${updated_navigation}")
run_cli(1 "approval lacks a fingerprint" check --docs-dir navigation)
run_cli(0 "Recorded review for README.md" approve README.md --docs-dir navigation)
run_cli(0 "Approved documentation sections: 2" check --docs-dir navigation)

file(WRITE "${navigation}/topics/Getting started.md" "# Setup\n")
run_cli(1 [=[README[.]md:5: Start here: "Other page": Markdown heading not found: topics/Getting%20started[.]md#caf%C3%A9-build]=]
    check --docs-dir navigation)

set(ranges "${WORK_DIR}/ranges")
file(MAKE_DIRECTORY "${ranges}/docs")
set(span_source [=[
struct Value;
int helper(int value);
int first();

int helper(int value)
{
    return value + 1;
}

int first()
{
    return helper(2);
}
// Range context.
int second()
{
    return 4;
}
]=])
set(span_page [=[
# Source links

## Declarations
[Prototype](../spans.cpp#L2).
[Forward declaration with no definition](../spans.cpp#L1).

## Definition
[Body](../spans.cpp#L12).
[Closing brace](../spans.cpp#L13).

## Range
[Across functions](../spans.cpp#L12-L16).
]=])
file(WRITE "${ranges}/spans.cpp" "${span_source}")
file(WRITE "${ranges}/docs/guide.md" "${span_page}")
file(WRITE "${ranges}/compile_commands.json"
    "[{\"directory\":\"${ranges}\",\"file\":\"${ranges}/spans.cpp\",\"arguments\":[\"${CLANG}\",\"-std=c++20\",\"-c\",\"${ranges}/spans.cpp\"]}]")
set(span_options --source-dir "${ranges}" --build-dir "${ranges}" --docs-dir "${ranges}/docs")
run_cli(0 "Recorded review for guide.md" approve guide.md ${span_options})
run_cli(0 "Approved documentation sections: 3" check ${span_options})

string(REPLACE "return value + 1;" "return value + 2;" implementation "${span_source}")
file(WRITE "${ranges}/spans.cpp" "${implementation}")
run_cli(1 [=["Body": code changed: first[(][)] -> helper[(]int[)].*"Closing brace": code changed: first[(][)] -> helper[(]int[)]]=]
    check ${span_options})
run_cli(0 "Recorded review" approve guide.md "Source links / Definition" ${span_options})
run_cli(0 "Recorded review" approve guide.md "Source links / Range" ${span_options})
run_cli(0 "Approved documentation sections: 3" check ${span_options})

string(REPLACE "helper(int value);" "helper(int number);" declaration "${implementation}")
file(WRITE "${ranges}/spans.cpp" "${declaration}")
run_cli(1 [=["Prototype": declaration changed: ../spans[.]cpp#L2]=] check ${span_options})
run_cli(0 "Recorded review" approve guide.md "Source links / Declarations" ${span_options})
run_cli(0 "Approved documentation sections: 3" check ${span_options})

string(REPLACE "return 4;" "return 5;" second "${declaration}")
file(WRITE "${ranges}/spans.cpp" "${second}")
run_cli(1 [=["Across functions": code changed: second[(][)]]=] check ${span_options})
run_cli(0 "Recorded review" approve guide.md "Source links / Range" ${span_options})
string(REPLACE "// Range context." "// Updated range context." context "${second}")
file(WRITE "${ranges}/spans.cpp" "${context}")
run_cli(1 [=["Across functions": source text changed: ../spans[.]cpp#L12-L16]=] check ${span_options})
run_cli(0 "Recorded review" approve guide.md "Source links / Range" ${span_options})
run_cli(0 "Approved documentation sections: 3" check ${span_options})

file(APPEND "${ranges}/docs/guide.md" "\n[Reversed](../spans.cpp#L12-L10).\n[Past EOF](../spans.cpp#L12-L100).\n")
run_cli(1 [=["Reversed": expected a source link.*"Past EOF": source link goes beyond the end of the file]=]
    check ${span_options})

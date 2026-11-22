function(run_tool output)
    execute_process(
        COMMAND "${PROGRAM}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${ARGN} failed with ${status}\n${stderr}\n${stdout}")
    endif()
    set(${output} "${stdout}" PARENT_SCOPE)
endfunction()

function(make_snapshot name macro comment)
    set(root "${WORK_DIR}/${name}")
    file(MAKE_DIRECTORY "${root}")
    file(WRITE "${root}/shared.hpp"
        "#define INITIAL ${macro}\n${comment}\n"
        "extern \"C\" int c_value(void);\n"
        "namespace app {\n"
        "int a(int n);\nint b(int n);\n"
        "static int helper() { return 4; }\n"
        "struct Base { int n; };\nstruct Derived : Base {};\n"
        "struct Reader { int read() const & { return 7; } };\n}\n"
    )
    file(WRITE "${root}/a.cpp"
        "#include \"shared.hpp\"\n"
        "int app::a(int n) { return n ? b(n - 1) : INITIAL; }\n"
    )
    file(WRITE "${root}/b.cpp"
        "#include \"shared.hpp\"\n"
        "int app::b(int n) { return n ? a(n - 1) : helper(); }\n"
    )
    file(WRITE "${root}/caller.cpp"
        "#include \"shared.hpp\"\n#include <vector>\n"
        "using Count = int;\n"
        "int inspect(Count n) { return app::a(n) + c_value(); }\n"
        "int inspect(double) { return sizeof(std::vector<int>); }\n"
        "int use(const app::Derived &value) { return value.n; }\n"
    )
    file(WRITE "${root}/legacy.c"
        "#include <stdint.h>\nstruct State { int32_t count; };\n"
        "static int helper(void) { return 3; }\n"
        "int c_value(void) { struct State s = {helper()}; return s.count; }\n"
    )
    set(database "[]")
    set(index 0)
    foreach(file IN ITEMS a.cpp b.cpp caller.cpp legacy.c)
        if(file STREQUAL "legacy.c")
            set(standard "-std=c17")
        else()
            set(standard "-std=c++20")
        endif()
        string(JSON database SET "${database}" ${index}
            "{\"directory\":\"${root}\",\"file\":\"${root}/${file}\",\"arguments\":[\"${CLANG}\",\"${standard}\",\"-c\",\"${root}/${file}\"]}")
        math(EXPR index "${index} + 1")
    endforeach()
    file(WRITE "${root}/compile_commands.json" "${database}\n")
endfunction()

function(find_entry out json list id)
    string(JSON count LENGTH "${json}" "${list}")
    if(count EQUAL 0)
        message(FATAL_ERROR "No entries for ${id} in ${list}")
    endif()
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON item GET "${json}" "${list}" ${i})
        string(JSON found GET "${item}" id)
        if(found STREQUAL id)
            set(${out} "${item}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Missing ${id} in ${list}\n${json}")
endfunction()

make_snapshot(base 1 "")
make_snapshot(head 2 "// A comment near the header definitions.")
make_snapshot(comment-only 1 "// This change only shifts source locations.")
set(base "${WORK_DIR}/base")
set(head "${WORK_DIR}/head")
set(comment "${WORK_DIR}/comment-only")

run_tool(scan scan "${base}" "${base}")
foreach(id IN ITEMS "a.cpp::app::helper()" "b.cpp::app::helper()" "legacy.c::helper"
                    "legacy.c::struct State" "app::Reader::read() const &"
                    "inspect(int)" "inspect(double)" "c_value")
    find_entry(symbol "${scan}" symbols "${id}")
endforeach()
find_entry(symbol "${scan}" symbols "b.cpp::app::helper()")
string(JSON file GET "${symbol}" definition file)
if(NOT file STREQUAL "shared.hpp")
    message(FATAL_ERROR "Header helper has wrong definition file: ${file}")
endif()
find_entry(derived "${scan}" symbols "app::Derived")
string(JSON base_type GET "${derived}" dependencies 0 target)
if(NOT base_type STREQUAL "app::Base")
    message(FATAL_ERROR "Missing inheritance edge: ${derived}")
endif()
string(JSON findings LENGTH "${scan}" findings)
if(NOT findings EQUAL 0)
    message(FATAL_ERROR "Unexpected findings: ${scan}")
endif()

run_tool(diff diff "${base}" "${base}" "${head}" "${head}")
string(JSON count LENGTH "${diff}" changes)
if(NOT count EQUAL 3)
    message(FATAL_ERROR "Unexpected changes: ${diff}")
endif()
find_entry(a "${diff}" changes "app::a(int)")
find_entry(b "${diff}" changes "app::b(int)")
find_entry(caller "${diff}" changes "inspect(int)")
string(JSON a_kind GET "${a}" kind)
string(JSON b_kind GET "${b}" kind)
string(JSON caller_kind GET "${caller}" kind)
string(JSON cause GET "${caller}" dependency_path 1)
if(NOT a_kind STREQUAL "direct" OR NOT b_kind STREQUAL "cycle" OR
   NOT caller_kind STREQUAL "dependency" OR NOT cause STREQUAL "app::a(int)")
    message(FATAL_ERROR "Bad change classification or path: ${diff}")
endif()

run_tool(diff diff "${base}" "${base}" "${comment}" "${comment}")
string(JSON count LENGTH "${diff}" changes)
if(NOT count EQUAL 0)
    message(FATAL_ERROR "Comment-only edit changed hashes: ${diff}")
endif()

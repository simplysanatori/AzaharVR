# AzaharVR: MSVC takes no string literal over about 16 KB (C2026), nor adjacent ones over 64 KB
# together. The built-in catalogs (mods/catalog_default.cpp, custom_tex/download_default.cpp) are
# one `return R"DELIM(...)DELIM";` each, far bigger. On MSVC this writes a copy of such a file
# where that literal is cut, at line ends, into pieces joined once at run time; the file itself
# stays as it is (it is kept in step with the catalog repository) and other compilers use it.
#
# cmake -DIN=<file.cpp> -DOUT=<copy.cpp> -P MsvcSplitRawString.cmake

cmake_minimum_required(VERSION 3.20)

file(READ "${IN}" text)

string(REGEX MATCH "return R\"([A-Za-z_]*)\\(" opening "${text}")
if (NOT opening)
    message(FATAL_ERROR "${IN}: no `return R\"DELIM(` found")
endif()
set(delim "${CMAKE_MATCH_1}")
string(FIND "${text}" "${opening}" open_at)
string(LENGTH "${opening}" open_len)
math(EXPR body_at "${open_at} + ${open_len}")
set(closing ")${delim}\"")
string(FIND "${text}" "${closing}" close_at REVERSE)
if (close_at LESS body_at)
    message(FATAL_ERROR "${IN}: no closing `)${delim}\"` found")
endif()
string(LENGTH "${closing}" close_len)
math(EXPR body_len "${close_at} - ${body_at}")
math(EXPR after_at "${close_at} + ${close_len}")

string(SUBSTRING "${text}" 0 ${open_at} before)
string(SUBSTRING "${text}" ${body_at} ${body_len} body)
string(SUBSTRING "${text}" ${after_at} -1 after)

set(piece_max 15000)
set(pieces "")
set(count 0)
while (NOT body STREQUAL "")
    string(LENGTH "${body}" left)
    if (left LESS_EQUAL piece_max)
        set(take ${left})
    else()
        string(SUBSTRING "${body}" 0 ${piece_max} window)
        string(FIND "${window}" "\n" nl REVERSE)
        if (nl LESS 1)
            message(FATAL_ERROR "${IN}: a line longer than ${piece_max} bytes")
        endif()
        math(EXPR take "${nl} + 1")
    endif()
    string(SUBSTRING "${body}" 0 ${take} piece)
    string(SUBSTRING "${body}" ${take} -1 body)
    if (count EQUAL 0)
        string(APPEND pieces "std::string(R\"${delim}(${piece})${delim}\")")
    else()
        string(APPEND pieces "\n        + R\"${delim}(${piece})${delim}\"")
    endif()
    math(EXPR count "${count} + 1")
endwhile()

set(out "// Made by CMakeModules/MsvcSplitRawString.cmake from ${IN}; do not edit.\n")
string(APPEND out "#include <string>\n")
string(APPEND out "${before}")
string(APPEND out "return [] {\n        static const std::string whole =\n        ${pieces};\n        return whole.c_str();\n    }()")
string(APPEND out "${after}")

file(WRITE "${OUT}.tmp" "${out}")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")

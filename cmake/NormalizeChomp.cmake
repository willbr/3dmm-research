# Chomp's original lexer consumes CRLF text and MSVC-style #line directives.
# Preserve source locations while accepting GCC/Clang's numeric line markers
# (including their trailing enter/leave/system-header flags). No text encoding
# conversion is performed: authored byte strings must survive unchanged.
file(READ "${INPUT}" text)
string(REPLACE "\r\n" "\n" text "${text}")
string(REGEX REPLACE "(^|\n)#[ \t]+([0-9]+)[ \t]+(\"[^\"\n]*\")[^\n]*" "\\1#line \\2 \\3" text "${text}")
# GCC uses zero for synthetic command-line/builtin locations. The legacy
# lexer starts at one and decrements a #line value before consuming its EOL.
string(REGEX REPLACE "(^|\n)#line 0 " "\\1#line 1 " text "${text}")
if(DEPFILE)
  # Both compiler families emit every included file as a line marker. Use
  # those to track nested includes with Ninja and Make, including MSVC.
  string(REGEX MATCHALL "(^|\n)#line [0-9]+ \"[^\"]*\"" markers "${text}")
  set(dependencies)
  foreach(marker IN LISTS markers)
    string(REGEX REPLACE "^[^\"]*\"([^\"]*)\"$" "\\1" path "${marker}")
    string(REPLACE "\\" "/" path "${path}")
    cmake_path(NORMAL_PATH path)
    if(EXISTS "${path}")
      list(APPEND dependencies "${path}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES dependencies)
  set(rule)
  foreach(path IN ITEMS "${OUTPUT}" ${dependencies})
    string(REPLACE "$" "$$" path "${path}")
    string(REPLACE "#" "\\#" path "${path}")
    string(REPLACE " " "\\ " path "${path}")
    string(REPLACE ":" "\\:" path "${path}")
    if(NOT rule)
      set(rule "${path}:")
    else()
      string(APPEND rule " ${path}")
    endif()
  endforeach()
  file(WRITE "${DEPFILE}" "${rule}\n")
endif()
string(REPLACE "\n" "\r\n" text "${text}")
file(WRITE "${OUTPUT}" "${text}")

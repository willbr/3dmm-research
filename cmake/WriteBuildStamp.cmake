string(TIMESTAMP NOW "%Y-%m-%d %H:%M:%S")
set(COMMIT "unavailable (source archive)")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE GIT_RESULT OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(GIT_RESULT EQUAL 0 AND GIT_COMMIT MATCHES "^[0-9a-f]+$")
        set(COMMIT "${GIT_COMMIT}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
            WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE GIT_DIRTY ERROR_QUIET)
        if(GIT_DIRTY EQUAL 1)
            string(APPEND COMMIT " (modified)")
        endif()
    endif()
endif()
file(WRITE "${OUT}" "extern const char kszBuildStamp[] = \"${NOW}\";\nextern const char kszGitCommit[] = \"${COMMIT}\";\n")

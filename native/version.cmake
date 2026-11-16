# Writes OUTPUT (a header defining OYR_VERSION) from the git tags, in the same form setuptools_scm gives the Python
# package: "0.0.3" on the tag v0.0.3, "0.0.4.dev23+ge819dca" 23 commits after it, with ".dirty" for uncommitted changes.
# Runs on every build and leaves the header untouched when the version did not change.
set(version "0.0.0+unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --long --dirty --match "v[0-9]*"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE result
        ERROR_QUIET)
    if(result EQUAL 0 AND describe MATCHES "^v([0-9.]*[0-9])-([0-9]+)-(g[0-9a-f]+)(-dirty)?$")
        set(tag "${CMAKE_MATCH_1}")
        set(distance "${CMAKE_MATCH_2}")
        set(commit "${CMAKE_MATCH_3}")
        set(dirty "${CMAKE_MATCH_4}")
        if(distance EQUAL 0)
            set(version "${tag}")
        else()
            # Like setuptools_scm's guess-next-dev: bump the last number of the tag.
            string(REGEX MATCH "[0-9]+$" last "${tag}")
            string(REGEX REPLACE "[0-9]+$" "" head "${tag}")
            math(EXPR last "${last} + 1")
            set(version "${head}${last}.dev${distance}+${commit}")
        endif()
        if(dirty)
            if(distance EQUAL 0)
                set(version "${version}+${commit}.dirty")
            else()
                set(version "${version}.dirty")
            endif()
        endif()
    endif()
endif()

set(content "#pragma once\n#define OYR_VERSION \"${version}\"\n")
set(old "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()

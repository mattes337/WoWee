# Writes core/version.hpp from `git describe`, so the version shown in the client
# is always the last tag reachable from HEAD.
#
# Run as a script (cmake -P) from a build-time custom target, not just at configure
# time - otherwise tagging a release would not change the binary until someone
# happened to re-run cmake.
#
# Expects: SRC_DIR, IN_FILE, OUT_FILE

find_package(Git QUIET)

set(WOWEE_GIT_VERSION "unknown")
set(WOWEE_SOURCE_REVISION "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --verify HEAD
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE _source_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _source_result
    )
    if(_source_result EQUAL 0 AND _source_sha)
        set(WOWEE_SOURCE_REVISION "${_source_sha}")
        # Ignore extracted assets and other untracked local runtime files.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${SRC_DIR}
            OUTPUT_VARIABLE _source_changes
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _status_result
        )
        if(NOT _status_result EQUAL 0)
            string(APPEND WOWEE_SOURCE_REVISION "-state-unknown")
        elseif(_source_changes)
            string(APPEND WOWEE_SOURCE_REVISION "-dirty")
        endif()
    endif()
    # The last tag reachable from HEAD - the released version this build descends from.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE _git_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0 AND _git_tag)
        set(WOWEE_GIT_VERSION "${_git_tag}")
    else()
        # No tags (shallow clone, fresh fork): fall back to the short commit.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${SRC_DIR}
            OUTPUT_VARIABLE _git_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _sha_result
        )
        if(_sha_result EQUAL 0 AND _git_sha)
            set(WOWEE_GIT_VERSION "${_git_sha}")
        endif()
    endif()
endif()

# Date only, no clock time: a timestamp would differ on every build and force a
# recompile of everything including this header.
string(TIMESTAMP WOWEE_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(${IN_FILE} ${OUT_FILE}.tmp @ONLY)

# Only touch the real header when the version actually changed; rewriting it every
# build would rebuild every translation unit that includes it.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT_FILE}.tmp ${OUT_FILE}
)
file(REMOVE ${OUT_FILE}.tmp)

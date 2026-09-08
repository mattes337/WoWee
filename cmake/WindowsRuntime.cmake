# Install only DLLs beside the selected executable. Resolve at install time:
# the bundler can populate this directory after CMake configuration.
function(wowee_install_runtime_dlls target)
    set(_code [[
        file(GLOB _wowee_runtime_dlls LIST_DIRECTORIES FALSE
            "$<TARGET_FILE_DIR:@target@>/*.dll")
        if(_wowee_runtime_dlls)
            file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin"
                TYPE FILE FILES ${_wowee_runtime_dlls})
        endif()
    ]])
    string(CONFIGURE "${_code}" _code @ONLY)
    install(CODE "${_code}")
endfunction()

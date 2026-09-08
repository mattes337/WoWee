# ---- Lua 5.1.5 (vendored, static library) ----
set(LUA_DIR ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
set(LUA_SOURCES
    ${LUA_DIR}/lapi.c    ${LUA_DIR}/lcode.c   ${LUA_DIR}/ldebug.c
    ${LUA_DIR}/ldo.c     ${LUA_DIR}/ldump.c   ${LUA_DIR}/lfunc.c
    ${LUA_DIR}/lgc.c     ${LUA_DIR}/llex.c    ${LUA_DIR}/lmem.c
    ${LUA_DIR}/lobject.c ${LUA_DIR}/lopcodes.c ${LUA_DIR}/lparser.c
    ${LUA_DIR}/lstate.c  ${LUA_DIR}/lstring.c ${LUA_DIR}/ltable.c
    ${LUA_DIR}/ltm.c     ${LUA_DIR}/lundump.c ${LUA_DIR}/lvm.c
    ${LUA_DIR}/lzio.c    ${LUA_DIR}/lauxlib.c ${LUA_DIR}/lbaselib.c
    ${LUA_DIR}/ldblib.c  ${LUA_DIR}/liolib.c  ${LUA_DIR}/lmathlib.c
    ${LUA_DIR}/loslib.c  ${LUA_DIR}/ltablib.c ${LUA_DIR}/lstrlib.c
    ${LUA_DIR}/linit.c
)
add_library(lua51 STATIC ${LUA_SOURCES})
set_target_properties(lua51 PROPERTIES LINKER_LANGUAGE C C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_include_directories(lua51 SYSTEM PUBLIC ${LUA_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lua51 PRIVATE -w)
endif()
if(ANDROID)
    # Lua stores a string's bytes immediately after the TString union that
    # describes them, and reaches them with (ts + 1). That pointer is one past
    # the declared object, so __builtin_object_size answers 0, and bionic's
    # fortified strchr aborts on a read that is in bounds of the allocation and
    # NUL terminated. lgc.c does exactly that on every garbage collection that
    # walks a table with a __mode field, so the client died during startup as
    # soon as one existed. glibc does not check, which is why only Android saw
    # it. The bound is wrong, not the read.
    target_compile_options(lua51 PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
endif()

# Instrument the VM itself; executable-only flags miss faults inside Lua.
# The link requirement propagates to every consumer of the static library.
if(WOWEE_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(lua51 PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lua51 INTERFACE -fsanitize=address,undefined)
endif()

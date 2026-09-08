# Pure tests: no graphics SDK, window, game assets, or running server.
# Included by tests/CMakeLists.txt in both client and headless configurations.

wowee_add_test(test_input_trace SOURCES test_input_trace.cpp
    ${CMAKE_SOURCE_DIR}/src/core/test_input_trace.cpp)
target_include_directories(test_input_trace SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/extern)
set_tests_properties(input_trace PROPERTIES LABELS "headless")

if(TARGET SDL2::SDL2)
    wowee_add_test(test_input_trace_sdl SOURCES test_input_trace_sdl.cpp
        ${CMAKE_SOURCE_DIR}/src/core/test_input_trace.cpp
        ${CMAKE_SOURCE_DIR}/src/core/test_input_trace_sdl.cpp
        ${CMAKE_SOURCE_DIR}/src/core/input.cpp)
    target_include_directories(test_input_trace_sdl SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/extern)
    target_link_libraries(test_input_trace_sdl PRIVATE SDL2::SDL2)
    wowee_test_link_glm(test_input_trace_sdl)
    set_tests_properties(input_trace_sdl PROPERTIES LABELS "headless;sdl")
    if(WIN32)
        add_custom_command(TARGET test_input_trace_sdl POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:test_input_trace_sdl>
            VERBATIM)
    endif()
endif()

# ── test_framexml ───────────────────────────────────────────
add_executable(test_framexml
    test_framexml.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_emitter.cpp
)
target_include_directories(test_framexml PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_framexml SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_compile_definitions(test_framexml PRIVATE
    WOWEE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_framexml PRIVATE catch2_main)
add_test(NAME framexml COMMAND test_framexml)
register_test_target(test_framexml)

# ── test_framexml_takeover ──────────────────────────────────
# Which interface draws what in the configuration a run actually gets. The
# transition's state is "FrameXML draws all of it", and that rested on a list
# of forty-nine names and a grouping rule agreeing about fifty-two elements,
# in a file where either can be edited alone. An element falling out is not an
# error - this client just draws its own version again.
add_executable(test_framexml_takeover
    test_framexml_takeover.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/framexml_takeover.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_framexml_takeover PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_framexml_takeover SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_framexml_takeover PRIVATE catch2_main)
add_test(NAME framexml_takeover COMMAND test_framexml_takeover)
register_test_target(test_framexml_takeover)


# ── test_widget_tree ────────────────────────────────────────
add_executable(test_widget_tree
    test_widget_tree.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widget_tree.cpp
)
target_include_directories(test_widget_tree PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_widget_tree SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_widget_tree PRIVATE catch2_main)
add_test(NAME widget_tree COMMAND test_widget_tree)
register_test_target(test_widget_tree)

# ── test_text_edit ──────────────────────────────────────────
wowee_add_test(test_text_edit SOURCES
    test_text_edit.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/text_edit.cpp
)

# ── test_escape_action ──────────────────────────────────────
add_executable(test_escape_action
    test_escape_action.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/escape_action.cpp
)
target_include_directories(test_escape_action PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_escape_action SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_escape_action PRIVATE catch2_main)
add_test(NAME escape_action COMMAND test_escape_action)
register_test_target(test_escape_action)

# ── test_monster_move_facing ─────────────────────────────────
# SMSG_MONSTER_MOVE's move-type byte and the facing after it, which three
# expansion parsers used to read for themselves. A wrong length here misreads
# every field that follows.
wowee_add_test(test_monster_move_facing
    SOURCES test_monster_move_facing.cpp
            ${TEST_COMMON_SOURCES}
            ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
            ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp)

# ── test_packet ──────────────────────────────────────────────
add_executable(test_packet
    test_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_packet PRIVATE catch2_main)
add_test(NAME packet COMMAND test_packet)
register_test_target(test_packet)

# ── test_bit_packet ──────────────────────────────────────────
add_executable(test_bit_packet
    test_bit_packet.cpp
    ${TEST_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp
)
target_include_directories(test_bit_packet PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_bit_packet SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_bit_packet PRIVATE catch2_main)
add_test(NAME bit_packet COMMAND test_bit_packet)
register_test_target(test_bit_packet)

# ── test_spline_body ─────────────────────────────────────────
# The body of SMSG_MONSTER_MOVE, whose head two parsers shared and neither
# covered. Checked against AzerothCore's own WriteCommonMonsterMovePart.
wowee_add_test(test_spline_body
    SOURCES test_spline_body.cpp
            ${TEST_COMMON_SOURCES}
            ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
            ${CMAKE_SOURCE_DIR}/src/network/packet.cpp)

# ── test_spline ──────────────────────────────────────────────
wowee_add_test(test_spline
    SOURCES test_spline.cpp
            ${TEST_COMMON_SOURCES}
            ${CMAKE_SOURCE_DIR}/src/math/spline.cpp
            ${CMAKE_SOURCE_DIR}/src/game/spline_packet.cpp
            ${CMAKE_SOURCE_DIR}/src/network/packet.cpp)

set_tests_properties(
    framexml framexml_takeover widget_tree text_edit
    escape_action monster_move_facing packet bit_packet spline_body spline
    PROPERTIES LABELS "headless")

# Ready-check state used by the live social handler and Lua API.
wowee_add_test(test_ready_check_state SOURCES test_ready_check_state.cpp
    ${CMAKE_SOURCE_DIR}/src/network/packet.cpp)
set_tests_properties(ready_check_state PROPERTIES LABELS "headless")

# ── test_settings_panel_layout ───────────────────────────────
# The options panels are generated from the schema by Lua, and nothing else
# checks the arithmetic between them. A control laid out past the bottom of a
# panel registers, refreshes and answers correctly - it is just not on screen.
add_executable(test_settings_panel_layout
    test_settings_panel_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/settings_schema.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_settings_panel_layout PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_settings_panel_layout SYSTEM PRIVATE ${TEST_SYSTEM_INCLUDE_DIRS})
target_link_libraries(test_settings_panel_layout PRIVATE catch2_main)
add_test(NAME settings_panel_layout COMMAND test_settings_panel_layout)
register_test_target(test_settings_panel_layout)

set_tests_properties(settings_panel_layout PROPERTIES LABELS "headless")

# Wire-format response helper updates ready state and member payload together.
wowee_add_test(test_ready_check_member SOURCES test_ready_check_member.cpp)
set_tests_properties(ready_check_member PROPERTIES LABELS "headless")

# Real Lua VM protected calls and error-handler routing.
wowee_add_test(test_lua_error_api SOURCES test_lua_error_api.cpp)
target_link_libraries(test_lua_error_api PRIVATE lua51)
set_tests_properties(lua_error_api PROPERTIES LABELS "headless")

# ── test_lua_handler_globals ────────────────────────────────
# The pre-3.0 handler globals, which are a pure function of the Lua stack and
# so need no game, window or interface - while the fault they prevent is the
# largest one this client can have: `this` nil on the first line of every
# OnLoad in a 1.12 interface, so every frame it declares reaches the screen at
# once and none of them where it belongs.
add_executable(test_lua_handler_globals
    test_lua_handler_globals.cpp
    ${CMAKE_SOURCE_DIR}/src/addons/lua_handler_globals.cpp
)
target_include_directories(test_lua_handler_globals PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_lua_handler_globals SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_lua_handler_globals PRIVATE catch2_main lua51)
wowee_test_link_glm(test_lua_handler_globals)
add_test(NAME lua_handler_globals COMMAND test_lua_handler_globals)
register_test_target(test_lua_handler_globals)

# ── test_lua_generic_for ────────────────────────────────────
# `for k, v in t do` over a plain table - the Lua 5.0 form 1.12's FrameXML is
# written in. It raised on the vendored 5.1, and raising while a file is read
# takes the whole file with it: six of FrameXML's died on this one construct,
# the player, target and party frames among them.
add_executable(test_lua_generic_for
    test_lua_generic_for.cpp
)
target_include_directories(test_lua_generic_for PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_lua_generic_for SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_lua_generic_for PRIVATE catch2_main lua51)
wowee_test_link_glm(test_lua_generic_for)
add_test(NAME lua_generic_for COMMAND test_lua_generic_for)
register_test_target(test_lua_generic_for)

# ── test_lua_arg_coercion ───────────────────────────────────
# The lenient numeric argument, which is a pure function of the Lua stack and
# so needs no game, window or interface - while the fault it prevents is
# invisible at every other level: a raise inside a click handler is swallowed,
# so an auction search that never sent looked exactly like one that found
# nothing.
add_executable(test_lua_arg_coercion
    test_lua_arg_coercion.cpp
)
target_include_directories(test_lua_arg_coercion PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_lua_arg_coercion SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_lua_arg_coercion PRIVATE catch2_main lua51)
wowee_test_link_glm(test_lua_arg_coercion)
add_test(NAME lua_arg_coercion COMMAND test_lua_arg_coercion)
register_test_target(test_lua_arg_coercion)

# ── test_addon_lua_snippets ──────────────────────────────────
# The Lua this client injects into the interface lives as C++ string literals,
# which nothing compiles until the client runs - and a syntax error there is a
# false from executeString and a line in a warning-only log. This asks Lua
# whether they parse.
add_executable(test_addon_lua_snippets
    test_addon_lua_snippets.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_addon_lua_snippets PRIVATE ${TEST_INCLUDE_DIRS})
target_include_directories(test_addon_lua_snippets SYSTEM PRIVATE
    ${TEST_SYSTEM_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/extern/lua-5.1.5/src)
target_link_libraries(test_addon_lua_snippets PRIVATE catch2_main lua51)
add_test(NAME addon_lua_snippets COMMAND test_addon_lua_snippets)
register_test_target(test_addon_lua_snippets)

set_tests_properties(lua_handler_globals lua_generic_for lua_arg_coercion addon_lua_snippets PROPERTIES LABELS "headless")

# FrameXML runner exit/failure contract shared with the runtime entrypoint.
wowee_add_test(test_framexml_run_contract SOURCES test_framexml_run_contract.cpp)
set_tests_properties(framexml_run_contract PROPERTIES LABELS "headless")

# Opt-in real-client smoke loop limit and successful SDL quit contract.
wowee_add_test(test_update_limit SOURCES test_update_limit.cpp)
set_tests_properties(update_limit PROPERTIES LABELS "headless")

# Deferred screenshot acceptance/completion contract; no graphics dependency.
wowee_add_test(test_screenshot_request SOURCES test_screenshot_request.cpp)
set_tests_properties(screenshot_request PROPERTIES LABELS "headless")

wowee_add_test(test_screenshot_schedule SOURCES test_screenshot_schedule.cpp)
set_tests_properties(screenshot_schedule PROPERTIES LABELS "headless")

# Execute the same animation clock/bootstrap used by LuaEngine.
wowee_add_test(test_animation_group_lua SOURCES test_animation_group_lua.cpp)
target_link_libraries(test_animation_group_lua PRIVATE lua51)
set_tests_properties(animation_group_lua PROPERTIES LABELS "headless")

wowee_add_test(test_model_replacement SOURCES test_model_replacement.cpp)
set_tests_properties(model_replacement PROPERTIES LABELS "headless")

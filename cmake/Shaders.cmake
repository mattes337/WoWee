# Function to compile GLSL shaders to SPIR-V
function(compile_shaders TARGET_NAME)
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
    set(SPV_DIR ${CMAKE_CURRENT_BINARY_DIR}/compiled-shaders)
    file(MAKE_DIRECTORY ${SPV_DIR})

    file(GLOB GLSL_SOURCES "${SHADER_DIR}/*.glsl")
    set(SPV_OUTPUTS)
    if(WOWEE_SHADER_DEBUG_INFO)
        set(SHADER_OPTIONS -g -O0)
    else()
        set(SHADER_OPTIONS -O)
    endif()
    # Makefile generators do not rebuild an OUTPUT solely because flags changed.
    set(SHADER_OPTIONS_FILE "${SPV_DIR}/../shader-options.txt")
    file(GENERATE OUTPUT "${SHADER_OPTIONS_FILE}" CONTENT "${GLSLC};${SHADER_OPTIONS}\n")

    foreach(GLSL_FILE ${GLSL_SOURCES})
        get_filename_component(FILE_NAME ${GLSL_FILE} NAME)
        # e.g. skybox.vert.glsl -> skybox.vert.spv
        string(REGEX REPLACE "\\.glsl$" ".spv" SPV_NAME ${FILE_NAME})
        set(SPV_FILE ${SPV_DIR}/${SPV_NAME})

        # Determine shader stage from filename
        if(FILE_NAME MATCHES "\\.vert\\.glsl$")
            set(SHADER_STAGE vertex)
        elseif(FILE_NAME MATCHES "\\.frag\\.glsl$")
            set(SHADER_STAGE fragment)
        elseif(FILE_NAME MATCHES "\\.comp\\.glsl$")
            set(SHADER_STAGE compute)
        elseif(FILE_NAME MATCHES "\\.geom\\.glsl$")
            set(SHADER_STAGE geometry)
        else()
            message(WARNING "Cannot determine shader stage for: ${FILE_NAME}")
            continue()
        endif()

        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLC} -fshader-stage=${SHADER_STAGE} ${SHADER_OPTIONS} ${GLSL_FILE} -o ${SPV_FILE}
            DEPENDS ${GLSL_FILE} "${SHADER_OPTIONS_FILE}"
            COMMENT "Compiling SPIR-V: ${FILE_NAME} -> ${SPV_NAME}"
            VERBATIM
        )
        list(APPEND SPV_OUTPUTS ${SPV_FILE})
    endforeach()

    add_custom_target(${TARGET_NAME}_shaders ALL DEPENDS ${SPV_OUTPUTS})
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_shaders)
    set_property(TARGET ${TARGET_NAME} PROPERTY WOWEE_COMPILED_SHADER_DIR ${SPV_DIR})
endfunction()

# Run on every client build, including builds where only GLSL/assets changed.
function(sync_runtime_assets TARGET_NAME)
    # TARGET_FILE_DIR would add a dependency cycle on older supported CMake.
    set(RUNTIME_ASSET_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    if(CMAKE_CONFIGURATION_TYPES)
        string(APPEND RUNTIME_ASSET_DIR "/$<CONFIG>")
    endif()
    string(APPEND RUNTIME_ASSET_DIR "/assets")
    add_custom_target(${TARGET_NAME}_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${RUNTIME_ASSET_DIR}"
        COMMENT "Syncing runtime assets"
        VERBATIM
    )
    if(TARGET ${TARGET_NAME}_shaders)
        get_target_property(SPV_DIR ${TARGET_NAME} WOWEE_COMPILED_SHADER_DIR)
        add_custom_command(TARGET ${TARGET_NAME}_assets POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SPV_DIR}" "${RUNTIME_ASSET_DIR}/shaders"
            COMMENT "Syncing compiled shaders to runtime assets"
            VERBATIM
        )
        add_dependencies(${TARGET_NAME}_assets ${TARGET_NAME}_shaders)
    endif()
    add_dependencies(${TARGET_NAME} ${TARGET_NAME}_assets)
endfunction()


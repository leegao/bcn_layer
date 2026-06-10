cmake_minimum_required(VERSION 3.16)
project(bcn_layer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "/usr") # or ~/.local/
endif()

set(JSON_INSTALL_DIR "share/vulkan/implicit_layer.d")
if(NOT DEFINED LIB_INSTALL_DIR)
    set(LIB_INSTALL_DIR "lib/x86_64-linux-gnu")
endif()

if(NOT "${LIB_INSTALL_DIR}" STREQUAL "lib/x86_64-linux-gnu")
    set(JSON_LIBRARY_PATH "./libbcn_layer.so")
else()
    set(JSON_LIBRARY_PATH "libbcn_layer.so")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libbcn_layer.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer.json"
    @ONLY
)

# BCn decode shaders
set(SHADER_NAMES s3tc rgtc bc6 bc7 s3tc_iv rgtc_iv bc6_iv bc7_iv)
set(SPIRV_HEADERS "")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src")
find_program(GLSL_COMPILER glslc REQUIRED)
foreach(SHADER ${SHADER_NAMES})
    set(COMP_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/src/${SHADER}.comp")
    set(SPV_OUT   "${CMAKE_CURRENT_BINARY_DIR}/src/${SHADER}.spv")
    set(HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/src/${SHADER}_spv.h")

    add_custom_command(
        OUTPUT ${SPV_OUT}
        COMMAND glslc -c ${COMP_SRC} -o ${SPV_OUT}
        DEPENDS ${COMP_SRC}
        COMMENT "Compiling to SPIR-V: ${SHADER}.spv"
    )

    add_custom_command(
        OUTPUT ${HEADER_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/src"
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/src" xxd -i ${SHADER}.spv > ${HEADER_OUT}
        DEPENDS ${SPV_OUT}
        COMMENT "Generating header via xxd: ${SHADER}_spv.h"
    )

    list(APPEND SPIRV_HEADERS ${HEADER_OUT})
endforeach()

# ETC2 encode shader
find_program(SLANG_COMPILER slangc)
option(FORCE_GLSL "Force the build to use GLSL instead of Slang" OFF)

set(ETC2_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/src/etc2_encode.slang")
set(ETC2_SPV "${CMAKE_CURRENT_BINARY_DIR}/src/etc2_encode.spv")
set(ETC2_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/etc2_encode_spv.h")

if(SLANG_COMPILER AND NOT FORCE_GLSL)
    set(ETC2_COMP "${CMAKE_CURRENT_BINARY_DIR}/src/etc2_encode.comp")
    add_custom_command(
        OUTPUT ${ETC2_COMP}
        COMMAND ${CMAKE_COMMAND} -E echo "// AUTO-GENERATED - DO NOT EDIT, see ${ETC2_SLANG}" > ${ETC2_COMP}
        COMMAND slangc ${ETC2_SLANG} -target glsl -line-directive-mode none -D DISABLE_RECONSTRUCTION >> ${ETC2_COMP}
        DEPENDS ${ETC2_SLANG}
        COMMENT "Generating GLSL shader: etc2_encode.comp"
    )
else()
    set(ETC2_COMP "${CMAKE_CURRENT_SOURCE_DIR}/src/etc2_encode.comp")
    message(STATUS "Shader Compiler: slangc not found or disabled. Using the auto-generated ${ETC2_COMP} instead.")
    if(FORCE_GLSL)
        message(WARNING "  Run cmake with -DFORCE_GLSL=OFF to use ${ETC2_SLANG}")
    endif()
endif()

add_custom_command(
    OUTPUT ${ETC2_SPV}
    COMMAND glslc -O -c ${ETC2_COMP} -o ${ETC2_SPV}
    DEPENDS ${ETC2_COMP}
    COMMENT "Compiling ${ETC2_COMP}"
)

add_custom_command(
    OUTPUT ${ETC2_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/src" xxd -i etc2_encode.spv > ${ETC2_HEADER}
    DEPENDS ${ETC2_SPV}
    COMMENT "Generating header via xxd: ${ETC2_HEADER}"
)
list(APPEND SPIRV_HEADERS ${ETC2_HEADER})

set(ASTC_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_encoder.slang")
set(ASTC_SPV "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder.spv")
set(ASTC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder_spv.h")

if(SLANG_COMPILER AND NOT FORCE_GLSL)
    set(ASTC_COMP "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder.comp")
    add_custom_command(
        OUTPUT ${ASTC_COMP}
        COMMAND ${CMAKE_COMMAND} -E echo "// AUTO-GENERATED - DO NOT EDIT, see ${ASTC_SLANG}" > ${ASTC_COMP}
        COMMAND slangc ${ASTC_SLANG} -target glsl -line-directive-mode none >> ${ASTC_COMP}
        DEPENDS ${ASTC_SLANG}
        COMMENT "Generating GLSL shader: astc_encoder.comp"
    )
else()
    set(ASTC_COMP "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_encoder.comp")
    message(STATUS "Shader Compiler: slangc not found or disabled. Using the auto-generated ${ASTC_COMP} instead.")
endif()

add_custom_command(
    OUTPUT ${ASTC_SPV}
    COMMAND glslc -O -c ${ASTC_COMP} -o ${ASTC_SPV}
    DEPENDS ${ASTC_COMP}
    COMMENT "Compiling ${ASTC_SPV}"
)

add_custom_command(
    OUTPUT ${ASTC_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/src" xxd -i astc_encoder.spv > ${ASTC_HEADER}
    DEPENDS ${ASTC_SPV}
    COMMENT "Generating header via xxd: ${ASTC_HEADER}"
)
list(APPEND SPIRV_HEADERS ${ASTC_HEADER})

set(LUT2_BIN "${CMAKE_CURRENT_SOURCE_DIR}/src/lut2.bin")
set(LUT2_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/lut2.h")
set(ASTC_2P_LUT_S2_BIN "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_2p_lut_s2.bin")
set(ASTC_2P_LUT_S2_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/astc_2p_lut_s2.h")

add_custom_command(
    OUTPUT ${LUT2_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_SOURCE_DIR}/src" xxd -i lut2.bin > ${LUT2_HEADER}
    DEPENDS ${LUT2_BIN}
    COMMENT "Generating header via xxd: ${LUT2_HEADER}"
)
list(APPEND SPIRV_HEADERS ${LUT2_HEADER})

add_custom_command(
    OUTPUT ${ASTC_2P_LUT_S2_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_SOURCE_DIR}/src" xxd -i astc_2p_lut_s2.bin > ${ASTC_2P_LUT_S2_HEADER}
    DEPENDS ${ASTC_2P_LUT_S2_BIN}
    COMMENT "Generating header via xxd: ${ASTC_2P_LUT_S2_HEADER}"
)
list(APPEND SPIRV_HEADERS ${ASTC_2P_LUT_S2_HEADER})

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Force re-compilation of bcn.cpp when ETC2_HEADER or SPIRV_HEADERS change
set_source_files_properties(src/bcn.cpp
    PROPERTIES OBJECT_DEPENDS "${SPIRV_HEADERS}"
)

add_library(bcn_layer SHARED ${SOURCES} ${SPIRV_HEADERS})

set_target_properties(bcn_layer PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "bcn_layer"
)

target_include_directories(bcn_layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/src
)

install(TARGETS bcn_layer DESTINATION ${LIB_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer.json" DESTINATION ${JSON_INSTALL_DIR})

message(STATUS "Run `BCN_TRANSCODE_TO_ETC2=1 BCN_LAYER_LOG_LEVEL=info ENABLE_BCN_COMPUTE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation vkcube` to test the layer")

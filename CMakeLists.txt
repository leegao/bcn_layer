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

if(NOT DEFINED JSON_LIBRARY_PATH)
    if(NOT "${LIB_INSTALL_DIR}" STREQUAL "lib/x86_64-linux-gnu")
        set(JSON_LIBRARY_PATH "./")
    else()
        set(JSON_LIBRARY_PATH "")
    endif()
endif()

if(NOT DEFINED JSON_LIBRARY32_PATH)
    if(NOT "${LIB_INSTALL_DIR}" STREQUAL "lib/x86_64-linux-gnu")
        set(JSON_LIBRARY32_PATH "./")
    else()
        set(JSON_LIBRARY32_PATH "")
    endif()
endif()

if(NOT DEFINED OUT_NAME)
    set(OUT_NAME bcn_layer)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libbcn_layer.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer.json"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libbcn_layer32.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer32.json"
    @ONLY
)

# BCn decode shaders
set(SHADER_NAMES s3tc rgtc bc6 bc7 s3tc_iv rgtc_iv bc6_iv bc7_iv watermark)
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

find_program(SLANG_COMPILER slangc)
option(FORCE_GLSL "Force the build to use GLSL instead of Slang" OFF)

set(SLANG_SHADER_NAMES etc2_encode astc_encoder analyze_astc_parameters)
foreach(SLANG_SHADER ${SLANG_SHADER_NAMES})
    set(SLANG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/${SLANG_SHADER}.slang")
    set(SLANG_SPV "${CMAKE_CURRENT_BINARY_DIR}/src/${SLANG_SHADER}.spv")
    set(SLANG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/${SLANG_SHADER}_spv.h")

    if(SLANG_COMPILER AND NOT FORCE_GLSL)
        set(SLANG_COMP_SRC "${CMAKE_CURRENT_BINARY_DIR}/src/${SLANG_SHADER}.comp")
        add_custom_command(
            OUTPUT ${SLANG_COMP_SRC}
            COMMAND ${CMAKE_COMMAND} -E echo "// AUTO-GENERATED - DO NOT EDIT, see ${SLANG_SRC}" > ${SLANG_COMP_SRC}
            COMMAND slangc ${SLANG_SRC} -target glsl -line-directive-mode none >> ${SLANG_COMP_SRC}
            DEPENDS ${SLANG_SRC} "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_pack.slang" "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_debug.slang" "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_debug.h"
            COMMENT "Generating GLSL shader: ${SLANG_SHADER}.comp"
        )
    else()
        set(SLANG_COMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/${SLANG_SHADER}.comp")
        message(STATUS "Shader Compiler: slangc not found or disabled. Using the auto-generated ${SLANG_COMP_SRC} instead.")
    endif()

    add_custom_command(
        OUTPUT ${SLANG_SPV}
        COMMAND glslc -c ${SLANG_COMP_SRC} -o ${SLANG_SPV}
        DEPENDS ${SLANG_COMP_SRC}
        COMMENT "Compiling ${SLANG_SPV}"
    )

    add_custom_command(
        OUTPUT ${SLANG_HEADER}
        COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/src" xxd -i ${SLANG_SHADER}.spv > ${SLANG_HEADER}
        DEPENDS ${SLANG_SPV}
        COMMENT "Generating header via xxd: ${SLANG_HEADER}"
    )
    list(APPEND SPIRV_HEADERS ${SLANG_HEADER})
endforeach()

set(ASTC_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_encoder.slang")
set(ASTC_SPV "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder.spv")
set(ASTC_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder_spv.h")
set(ASTC_DEBUG_SPV "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder_debug.spv")
set(ASTC_DEBUG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder_debug_spv.h")
if(SLANG_COMPILER AND NOT FORCE_GLSL)
    set(ASTC_COMP_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/src/astc_encoder_debug.comp")
    add_custom_command(
        OUTPUT ${ASTC_COMP_DEBUG}
        COMMAND ${CMAKE_COMMAND} -E echo "// AUTO-GENERATED - DO NOT EDIT, see ${ASTC_SLANG}" > ${ASTC_COMP_DEBUG}
        COMMAND slangc ${ASTC_SLANG} -target glsl -line-directive-mode none -D ENABLE_DEBUG >> ${ASTC_COMP_DEBUG}
        DEPENDS ${ASTC_SLANG} "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_pack.slang" "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_debug.slang" "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_debug.h"
        COMMENT "Generating GLSL shader: astc_encoder.comp"
    )
else()
    set(ASTC_COMP_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/src/astc_encoder_debug.comp")
    message(STATUS "Shader Compiler: slangc not found or disabled. Using the auto-generated ${ASTC_COMP_DEBUG} instead.")
endif()

add_custom_command(
    OUTPUT ${ASTC_DEBUG_SPV}
    COMMAND glslc -c ${ASTC_COMP_DEBUG} -o ${ASTC_DEBUG_SPV}
    DEPENDS ${ASTC_COMP_DEBUG}
    COMMENT "Compiling ${ASTC_DEBUG_SPV}"
)

add_custom_command(
    OUTPUT ${ASTC_DEBUG_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/src" xxd -i astc_encoder_debug.spv > ${ASTC_DEBUG_HEADER}
    DEPENDS ${ASTC_DEBUG_SPV}
    COMMENT "Generating header via xxd: ${ASTC_DEBUG_HEADER}"
)
list(APPEND SPIRV_HEADERS ${ASTC_DEBUG_HEADER})

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

if(CMAKE_SIZEOF_VOID_P EQUAL 8
    AND (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR ANDROID_ABI MATCHES "arm64-v8a"))
    set(HWCPIPE_WERROR OFF CACHE BOOL "" FORCE)

    add_subdirectory(third_party/libGPUCounters)

    # all_gpu_counters.cpp gets stuck on -O3, so let's compile it separately at -O0
    add_library(all_gpu_counters_obj OBJECT
        "third_party/libGPUCounters/hwcpipe/src/hwcpipe/all_gpu_counters.cpp"
    )
    target_compile_options(all_gpu_counters_obj PRIVATE "-O0")
    target_include_directories(all_gpu_counters_obj PRIVATE
        "third_party/libGPUCounters/hwcpipe/include"
        "third_party/libGPUCounters/hwcpipe/src"
        "third_party/libGPUCounters/backend/device/include"
    )
    set_target_properties(all_gpu_counters_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(all_gpu_counters_obj PRIVATE ANDROID)
    get_target_property(HWCP_SOURCES hwcpipe SOURCES)
    list(REMOVE_ITEM HWCP_SOURCES "src/hwcpipe/all_gpu_counters.cpp")
    set_target_properties(hwcpipe PROPERTIES SOURCES "${HWCP_SOURCES}")
    target_sources(hwcpipe PRIVATE $<TARGET_OBJECTS:all_gpu_counters_obj>)

    # third_party/libGPUCounters/backend/device/include/device/handle.hpp:90:30: error: ‘uint32_t’ has not been declared
    #   90 |     static handle_ptr create(uint32_t instance_number = 0);
    #                                     ^~~~~~~~
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(device PRIVATE "-include" "cstdint")
    endif()
    target_link_libraries(bcn_layer PRIVATE
        device
        hwcpipe
    )

    target_compile_definitions(bcn_layer PRIVATE HAS_LIBGPUCOUNTERS=1)
endif()

set_target_properties(bcn_layer PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "${OUT_NAME}"
)

target_include_directories(bcn_layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/src
)

install(TARGETS bcn_layer DESTINATION ${LIB_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer.json" DESTINATION ${JSON_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer32.json" DESTINATION ${JSON_INSTALL_DIR})


message(STATUS "Run `BCN_TRANSCODE_TO_ETC2=1 BCN_LAYER_LOG_LEVEL=info ENABLE_BCN_COMPUTE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation vkcube` to test the layer")

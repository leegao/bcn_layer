cmake_minimum_required(VERSION 3.16)
project(bcn_layer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "/home/leegao/.local") # or ~/.local/
endif()

set(JSON_INSTALL_DIR "share/vulkan/implicit_layer.d")
if(NOT DEFINED LIB_INSTALL_DIR)
    set(LIB_INSTALL_DIR "share/vulkan/implicit_layer.d")
endif()

if(NOT "${LIB_INSTALL_DIR}" STREQUAL "lib/x86_64-linux-gnu")
    set(JSON_LIBRARY_PATH "./libbcn_layer.so")
else()
    set(JSON_LIBRARY_PATH "libbcn_layer.so")
endif()

if(NOT DEFINED OUT_NAME)
    set(OUT_NAME etc2_layer)
endif()

# configure_file(
#     "${CMAKE_CURRENT_SOURCE_DIR}/libbcn_layer.json.in"
#     "${CMAKE_CURRENT_BINARY_DIR}/libbcn_layer.json"
#     @ONLY
# )

# BCn decode shaders
set(SHADER_NAMES etc2 astc_decoder)
set(SPIRV_HEADERS "")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src")
find_program(GLSL_COMPILER glslc REQUIRED)
foreach(SHADER ${SHADER_NAMES})
    set(COMP_SRC  "${CMAKE_CURRENT_SOURCE_DIR}/src/${SHADER}.comp")
    set(SPV_OUT   "${CMAKE_CURRENT_BINARY_DIR}/src/${SHADER}.spv")
    set(HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/src/${SHADER}_spv.h")

    add_custom_command(
        OUTPUT ${SPV_OUT}
        COMMAND glslc ${COMP_SRC} -o ${SPV_OUT}
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

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Force re-compilation of bcn.cpp when ETC2_HEADER or SPIRV_HEADERS change
set_source_files_properties(src/bcn.cpp
    PROPERTIES OBJECT_DEPENDS "${SPIRV_HEADERS}"
)

add_library(bcn_layer SHARED ${SOURCES} ${SPIRV_HEADERS})

set_target_properties(bcn_layer PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "${OUT_NAME}"
)

target_include_directories(bcn_layer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/src
)

install(TARGETS bcn_layer DESTINATION ${LIB_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/libetc2_layer.json" DESTINATION ${JSON_INSTALL_DIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/libetc2_layer32.json" DESTINATION ${JSON_INSTALL_DIR})

message(STATUS "Run `BCN_TRANSCODE_TO_ETC2=1 BCN_LAYER_LOG_LEVEL=info ENABLE_BCN_COMPUTE=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation vkcube` to test the layer")

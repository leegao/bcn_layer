import encoder_utils;

#include "astc_debug.h"

[[vk::binding(5, 0)]]
RWStructuredBuffer<AstcDebug> debug_buffer;

AstcDebug get_debug(uint buffer_index) { return debug_buffer[buffer_index]; }

void log_debug(uint buffer_index, uint4 ep0, uint4 ep1,
               PackedArray<8, 16> weights) {
  debug_buffer[buffer_index].params.ep0 = { uint8_t(ep0.x), uint8_t(ep0.y),
                                            uint8_t(ep0.z), uint8_t(ep0.w) };
  debug_buffer[buffer_index].params.ep1 = { uint8_t(ep1.x), uint8_t(ep1.y),
                                            uint8_t(ep1.z), uint8_t(ep1.w) };
  for (uint i = 0; i < 16; i++) {
    debug_buffer[buffer_index].params.weights[i] = uint8_t(weights[i]);
  }
}

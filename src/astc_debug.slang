import encoder_utils;

#include "astc_debug.h"

[[vk::binding(5, 0)]]
RWStructuredBuffer<AstcDebug> debug_buffer;

AstcDebug get_debug(uint buffer_index) { return debug_buffer[buffer_index]; }

void log_debug(uint buffer_index, uint4 ep0, uint4 ep1,
               PackedArray<8, 16> weights, in uint4 pixels[16],
               bool use_alpha) {
  debug_buffer[buffer_index].params.ep0 = { uint8_t(ep0.x), uint8_t(ep0.y),
                                            uint8_t(ep0.z), uint8_t(ep0.w) };
  debug_buffer[buffer_index].params.ep1 = { uint8_t(ep1.x), uint8_t(ep1.y),
                                            uint8_t(ep1.z), uint8_t(ep1.w) };
  for (uint i = 0; i < 16; i++) {
    debug_buffer[buffer_index].params.weights[i] = uint8_t(weights[i]);
  }
  debug_buffer[buffer_index].params.use_alpha = use_alpha;

  uint sum_error_squared = 0;
  for (uint i = 0; i < 16; i++) {
    int4 C = pixels[i];
    int4 reconstructed =
        int4(clamp(lerp(ep0, ep1, float(weights[i]) / 255.0), 0, 255));
    var err = (C - reconstructed);
    sum_error_squared += dot(err, err);
  }
  debug_buffer[buffer_index].sum_error_squared = sum_error_squared;
}

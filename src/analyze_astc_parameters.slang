import astc_debug;
import encoder_utils;

[[vk::binding(6, 0)]]
RWStructuredBuffer<AstcAnalysis> analysis_output;

struct PushConstants {
  int2 u_TextureDimensions;
  uint u_Flags;
};

[[vk::push_constant]]
PushConstants pushConstants;

int quantize(uint value, uint range) {
  uint quantized = uint((float(value) * range) / 255.0f + 0.5f);
  return (quantized * 255 + (range / 2)) / range;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gl_GroupThreadID: SV_GroupThreadID,
          uint3 gl_GlobalInvocationID: SV_DispatchThreadID) {
  int2 block_pos = int2(gl_GlobalInvocationID.xy);
  if (any(block_pos >= pushConstants.u_TextureDimensions / 4))
    return;

  var buffer_index =
      block_pos.y * (pushConstants.u_TextureDimensions.x / 4) + block_pos.x;
  var thread_id = gl_GroupThreadID.x + gl_GroupThreadID.y * 8;

  var debug = get_debug(buffer_index);
  var ep0 = reinterpret<uint8_t4>(debug.params.ep0);
  var ep1 = reinterpret<uint8_t4>(debug.params.ep1);
  var weights = debug.params.weights;
  var d = ep1 - ep0;

  uint sum_color_spread_squared =
      uint(d.x) * uint(d.x) + uint(d.y) * uint(d.y) + uint(d.z) * uint(d.z);
  uint sum_weights = 0;
  uint sum_weights_squared = 0;
  for (uint i = 0; i < 16; i++) {
    sum_weights += weights[i];
    sum_weights_squared += uint(weights[i]) * uint(weights[i]);
  }

  // quantization errors
  uint quantized_weight_errors_3 = 0;
  uint quantized_weight_errors_7 = 0;
  uint quantized_weight_errors_15 = 0;
  for (uint i = 0; i < 16; i++) {
    var q_w3 = quantize(weights[i], 3) - weights[i];
    var q_w7 = quantize(weights[i], 7) - weights[i];
    var q_w15 = quantize(weights[i], 15) - weights[i];
    quantized_weight_errors_3 += q_w3 * q_w3;
    quantized_weight_errors_7 += q_w7 * q_w7;
    quantized_weight_errors_15 += q_w15 * q_w15;
  }

  uint eps[6] = { ep0.x, ep0.y, ep0.z, ep1.x, ep1.y, ep1.z };
  uint quantized_color_errors_47 = 0;
  uint quantized_color_errors_191 = 0;
  for (uint i = 0; i < 6; i++) {
    var q_c47 = quantize(eps[i], 47) - eps[i];
    var q_c191 = quantize(eps[i], 191) - eps[i];
    quantized_color_errors_47 += q_c47 * q_c47;
    quantized_color_errors_191 += q_c191 * q_c191;
  }

  AstcAnalysis accumulator = {};

  if (/*sum_weights < 10 || sum_color_spread_squared < 384*/ false) {
    // shared_accumulator[thread_id] = {};
  } else {
    accumulator = {
      1,
      (uint64_t)sum_color_spread_squared,
      (uint64_t)sum_weights,
      (uint64_t)sum_weights_squared,
      (uint64_t)quantized_weight_errors_3,
      (uint64_t)quantized_weight_errors_7,
      (uint64_t)quantized_weight_errors_15,
      (uint64_t)quantized_color_errors_47,
      (uint64_t)quantized_color_errors_191,
    };
  }

  InterlockedAdd(analysis_output[0].count, accumulator.count);
  InterlockedAdd(analysis_output[0].sum_color_spread_squared,
                 accumulator.sum_color_spread_squared);
  InterlockedAdd(analysis_output[0].sum_weights, accumulator.sum_weights);
  InterlockedAdd(analysis_output[0].sum_weights_squared,
                 accumulator.sum_weights_squared);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_3,
                 accumulator.quantized_weight_errors_3);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_7,
                 accumulator.quantized_weight_errors_7);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_15,
                 accumulator.quantized_weight_errors_15);
  InterlockedAdd(analysis_output[0].quantized_color_errors_47,
                 accumulator.quantized_color_errors_47);
  InterlockedAdd(analysis_output[0].quantized_color_errors_191,
                 accumulator.quantized_color_errors_191);
}

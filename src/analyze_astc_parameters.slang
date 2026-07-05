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

int quantize_error(uint value, uint range) {
  return quantize(value, range) - value;
}

uint64_t quantization_error_exact(uint8_t4 ep0, uint8_t4 ep1,
                                  uint8_t weights[16], uint range_color,
                                  uint range_weight) {
  // this is approximately:
  // (ep1[c] - ep0[c]) * err(w[i]) + lerp(err(ep0[c], ep1[c], w[i])) + O(err^2)
  uint64_t error = 0;
  for (int i = 0; i < 16; i++) {
    for (int c = 0; c < 3; c++) {
      var exact = lerp(ep0[c], ep1[c], float(weights[i]) / 255.0f);
      var c0 = quantize(int(ep0[c]), range_color);
      var c1 = quantize(int(ep1[c]), range_color);
      var w = float(quantize(weights[i], range_weight)) / 255.0f;
      var reconstruction = lerp(c0, c1, w);
      var component_error = exact - reconstruction;
      error += uint(round(component_error * component_error));
    }
  }
  return error;
}

uint64_t quantization_error_approx1(uint8_t4 ep0, uint8_t4 ep1,
                                    uint8_t weights[16], uint range_color,
                                    uint range_weight) {
  // this is approximately:
  // (ep1[c] - ep0[c]) * err(w[i]) + lerp(err(ep0[c], ep1[c], w[i])) + O(err^2)
  uint64_t error = 0;
  for (int i = 0; i < 16; i++) {
    for (int c = 0; c < 3; c++) {
      var diff = float(int(ep1[c]) - int(ep0[c])) / 255.0f;
      var err_w = quantize_error(weights[i], range_weight) / 255.0f;
      var err_c0 = quantize_error(int(ep0[c]), range_color) / 255.0f;
      var err_c1 = quantize_error(int(ep1[c]), range_color) / 255.0f;
      var component_error =
          diff * err_w + lerp(err_c0, err_c1, float(weights[i]) / 255.0f);
      error += uint(round(component_error * component_error * 65025));
    }
  }
  return error;
}

uint64_t quantization_error_approx2(uint s0, uint s1, uint s2, uint err_w,
                                    uint err_c) {
  // this is approximately:
  //   s0 * err_w^2 + 2 * (s2 - s1) * err_c^2 + err_c^2
  // \approx s0 * err_w^2 + err_c^2
  float s0_ = float(s0) / 3 / 65025.0f;
  // float s1_ = float(s1) / 16 / 255.0f;
  // float s2_ = float(s2) / 16 / 65025.0f;
  float err_w_ = float(err_w) / 16 / 65025.0f;
  float err_c_ =
      float(err_c) / 6 / 65025.0f; // this drops to 0 for color range 0 .. 255,
                                   // but otherwise dominates
  var error = s0_ * err_w_ + err_c_; //  + 2 * (s2_ - s1_) * err_c_;
  return uint(round(error * 65025 * 48));
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
  var sum_error_squared = debug.sum_error_squared;
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

  var quantized_pixel_error_191_15_exact =
      quantization_error_exact(ep0, ep1, weights, 191, 15);
  var quantized_pixel_error_47_15_exact =
      quantization_error_exact(ep0, ep1, weights, 47, 15);
  var quantized_pixel_error_255_7_exact =
      quantization_error_exact(ep0, ep1, weights, 255, 7);
  var quantized_pixel_error_255_3_exact =
      quantization_error_exact(ep0, ep1, weights, 255, 3);

  var quantized_pixel_error_191_15_approx = quantization_error_approx2(
      sum_color_spread_squared, sum_weights, sum_weights_squared,
      quantized_weight_errors_15, quantized_color_errors_191);
  var quantized_pixel_error_47_15_approx = quantization_error_approx2(
      sum_color_spread_squared, sum_weights, sum_weights_squared,
      quantized_weight_errors_15, quantized_color_errors_47);
  var quantized_pixel_error_255_7_approx = quantization_error_approx2(
      sum_color_spread_squared, sum_weights, sum_weights_squared,
      quantized_weight_errors_7, 0);
  var quantized_pixel_error_255_3_approx = quantization_error_approx2(
      sum_color_spread_squared, sum_weights, sum_weights_squared,
      quantized_weight_errors_3, 0);

  // refining weight precision
  uint quantized_weight_errors_refined_3 = 0;
  uint quantized_weight_errors_refined_7 = 0;
  uint quantized_weight_errors_refined_15 = 0;
  for (uint i = 0; i < 16; i++) {
    if (weights[i] <= 2 || weights[i] >= 253)
      continue;
    var q_w3 = 602;
    quantized_weight_errors_refined_3 += 602; // 255^2 /(12 * 3^2)
    quantized_weight_errors_refined_7 += 111; // 255^2 /(12 * 7^2)
    quantized_weight_errors_refined_15 += 24; // 255^2 /(12 * 15^2)
  }

  var sum_quantization_error_squared = debug.params.use_alpha
                                           ? quantized_pixel_error_47_15_exact
                                           : quantized_pixel_error_191_15_exact;

  var sum_quantization_alt_error_squared =
      debug.params.use_alpha ? quantized_pixel_error_255_3_exact
                             : quantized_pixel_error_255_7_exact;

  InterlockedAdd(analysis_output[0].count, 1);
  InterlockedAdd(analysis_output[0].sum_error_squared, sum_error_squared);
  InterlockedAdd(analysis_output[0].sum_quantization_error_squared,
                 sum_quantization_error_squared);
  InterlockedAdd(analysis_output[0].sum_quantization_alt_error_squared,
                 sum_quantization_alt_error_squared);

  InterlockedAdd(analysis_output[0].sum_color_spread_squared,
                 sum_color_spread_squared);
  InterlockedAdd(analysis_output[0].sum_weights, sum_weights);
  InterlockedAdd(analysis_output[0].sum_weights_squared, sum_weights_squared);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_3,
                 quantized_weight_errors_3);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_7,
                 quantized_weight_errors_7);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_15,
                 quantized_weight_errors_15);
  InterlockedAdd(analysis_output[0].quantized_color_errors_47,
                 quantized_color_errors_47);
  InterlockedAdd(analysis_output[0].quantized_color_errors_191,
                 quantized_color_errors_191);

  InterlockedAdd(analysis_output[0].quantized_pixel_error_191_15_exact,
                 quantized_pixel_error_191_15_exact);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_47_15_exact,
                 quantized_pixel_error_47_15_exact);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_255_7_exact,
                 quantized_pixel_error_255_7_exact);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_255_3_exact,
                 quantized_pixel_error_255_3_exact);

  InterlockedAdd(analysis_output[0].quantized_pixel_error_191_15_approx,
                 quantized_pixel_error_191_15_approx);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_47_15_approx,
                 quantized_pixel_error_47_15_approx);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_255_7_approx,
                 quantized_pixel_error_255_7_approx);
  InterlockedAdd(analysis_output[0].quantized_pixel_error_255_3_approx,
                 quantized_pixel_error_255_3_approx);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_refined_3,
                 quantized_weight_errors_refined_3);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_refined_7,
                 quantized_weight_errors_refined_7);
  InterlockedAdd(analysis_output[0].quantized_weight_errors_refined_15,
                 quantized_weight_errors_refined_15);
}

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

groupshared AstcAnalysis shared_accumulator[64];

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gl_GroupThreadID: SV_GroupThreadID,
          uint3 gl_GlobalInvocationID: SV_DispatchThreadID) {
  int2 block_pos = int2(gl_GlobalInvocationID.xy);
  int2 tex_coord = block_pos * 4;

  if (any(tex_coord >= pushConstants.u_TextureDimensions))
    return;

  var buffer_index =
      block_pos.y * (pushConstants.u_TextureDimensions.x / 4) + block_pos.x;
  var thread_id = gl_GroupThreadID.x + gl_GroupThreadID.y * 8;

  var debug = get_debug(buffer_index);
  var ep0 = reinterpret<uint8_t4>(debug.params.ep0);
  var ep1 = reinterpret<uint8_t4>(debug.params.ep1);
  var d = ep1 - ep0;

  uint sum_color_spread_squared =
      uint(d.x) * uint(d.x) + uint(d.y) * uint(d.y) + uint(d.z) * uint(d.z);
  uint sum_weights = 0;
  uint sum_weights_squared = 0;
  for (uint i = 0; i < 16; i++) {
    sum_weights += debug.params.weights[i];
    sum_weights_squared +=
        uint(debug.params.weights[i]) * uint(debug.params.weights[i]);
  }

  if (sum_weights < 10 || sum_color_spread_squared < 384) {
    shared_accumulator[thread_id] = { 0, 0, 0, 0 };
  } else {
    shared_accumulator[thread_id] = {
      1,
      (uint64_t)sum_color_spread_squared,
      (uint64_t)sum_weights,
      (uint64_t)sum_weights_squared,
    };
  }
  GroupMemoryBarrierWithGroupSync();

  // Parallel reduction at the group level
  for (uint s = 32; s > 0; s >>= 1) {
    if (thread_id < s) {
      shared_accumulator[thread_id].count +=
          shared_accumulator[thread_id].count;
      shared_accumulator[thread_id].sum_color_spread_squared +=
          shared_accumulator[thread_id].sum_color_spread_squared;
      shared_accumulator[thread_id].sum_weights +=
          shared_accumulator[thread_id].sum_weights;
      shared_accumulator[thread_id].sum_weights_squared +=
          shared_accumulator[thread_id].sum_weights_squared;
    }
    GroupMemoryBarrierWithGroupSync();
  }

  if (thread_id == 0) {
    InterlockedAdd(analysis_output[0].count, shared_accumulator[0].count);
    InterlockedAdd(analysis_output[0].sum_color_spread_squared,
                   shared_accumulator[0].sum_color_spread_squared);
    InterlockedAdd(analysis_output[0].sum_weights,
                   shared_accumulator[0].sum_weights);
    InterlockedAdd(analysis_output[0].sum_weights_squared,
                   shared_accumulator[0].sum_weights_squared);
  }
}

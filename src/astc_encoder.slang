// MIT License
// Copyright (c) 2026 Lee Gao

import astc_pack;
import encoder_utils;

#ifdef ENABLE_DEBUG
import astc_debug;
#endif

// #define UNROLL_SOLVE_WEIGHTS
#define UNROLL_ENCODE_AABB

[[vk::binding(0, 0)]]
StructuredBuffer<uint> input_rgba8_buffer;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint4> astc_blocks;

struct Flags {
  PackedArray<1, 32> flags;
  property bool use_large_steps {
    get { return bool(flags[0]); }
  }

  property bool try_2p {
    get { return bool(flags[1]); }
  }

  property bool only_2p {
    get { return bool(flags[2]); }
  }

  property bool use_sfloat16 {
    get { return bool(flags[3]); }
  }

  property bool use_snorm8 {
    get { return bool(flags[4]); }
  }
};

struct PushConstants {
  int2 u_TextureDimensions;
  Flags u_Flags;
};

[[vk::push_constant]]
PushConstants pushConstants;

static uint buffer_index;
static uint8_t4 pixels[16];

extension uint8_t4 {
  half4 to_unorm() { return half4(this) / 255.0h; }
}

void solve_weights(half4 ep0, half4 ep1, out uint8_t weights[16]) {
  let L = ep1 - ep0;
  let invLenSq = 1.0h / (dot(L, L) + 0.000001h);
  for (int i = 0; i < 16; i++) {
#ifndef UNROLL_SOLVE_WEIGHTS
    // 3x + 1 mod 3 is always 1, but our compilers aren't smart enough to figure
    // this out yet
    // This acts as an optimization barrier to keep the mali compiler from
    // unrolling this loop, which blows up our register file and forces a stack
    // spill
    let always_true = bool(((pushConstants.u_TextureDimensions.x * 3) + 1) % 3);
    if (!always_true) {
      continue;
    }
#endif
    let C = pixels[i].to_unorm();
    let pDotL = dot(C - ep0, L);
    let w = pDotL * invLenSq;
    weights[i] = uint8_t(saturate(w) * 255u);
  }
}

void encode_block_AABB(out uint8_t4 endpoint_start, out uint8_t4 endpoint_end,
                       out uint8_t weights[16]) {
  endpoint_start = uint8_t4(255u);
  endpoint_end = uint8_t4(0u);
  for (int i = 0; i < 16; i += 1) {
#ifndef UNROLL_ENCODE_AABB
    let always_true = bool(((pushConstants.u_TextureDimensions.x * 3) + 1) % 3);
    if (!always_true) {
      continue;
    }
#endif
    endpoint_start = min(endpoint_start, pixels[i]);
    endpoint_end = max(endpoint_end, pixels[i]);
  }

  var ep0 = half4(endpoint_start) / 255.0h;
  var ep1 = half4(endpoint_end) / 255.0h;
  solve_weights(ep0, ep1, weights);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gl_GlobalInvocationID: SV_DispatchThreadID) {
  int2 block_pos = int2(gl_GlobalInvocationID.xy);
  int2 tex_coord = block_pos * 4;

#ifdef ENABLE_DIAGNOSTICS
  uint2 startTime = clockRealtime2x32EXT();
#endif

  if (any(tex_coord >= pushConstants.u_TextureDimensions))
    return;

  buffer_index =
      block_pos.y * (pushConstants.u_TextureDimensions.x / 4) + block_pos.x;

  // Encoding
  uint width = pushConstants.u_TextureDimensions.x;
  bool use_alpha = false;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      uint r, g, b, a;

      if (pushConstants.u_Flags.use_sfloat16) {
        var tex_coord = ((tex_coord.y + y) * width + (tex_coord.x + x)) * 2;
        var hi = input_rgba8_buffer[tex_coord];
        r = sfloat16_to_uint8((hi >> 0) & 0xFFFFu);
        g = sfloat16_to_uint8((hi >> 16) & 0xFFFFu);
        var lo = input_rgba8_buffer[tex_coord + 1];
        b = sfloat16_to_uint8((lo >> 0) & 0xFFFFu);
        a = sfloat16_to_uint8((lo >> 16) & 0xFFFFu);
      } else if (pushConstants.u_Flags.use_snorm8) {
        var tex_coord = (tex_coord.y + y) * width + (tex_coord.x + x);
        var tex_element = input_rgba8_buffer[tex_coord];
        unpack_snorm_to_unorm_float(tex_element, r, g, b, a);
      } else {
        var tex_coord = (tex_coord.y + y) * width + (tex_coord.x + x);
        var tex_element = input_rgba8_buffer[tex_coord];
        a = (tex_element >> 24) & 0xFFu;
        b = (tex_element >> 16) & 0xFFu;
        g = (tex_element >> 8) & 0xFFu;
        r = (tex_element >> 0) & 0xFFu;
      }

      pixels[y * 4 + x] =
          uint8_t4(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a));
      if (a < 255)
        use_alpha = true;
    }
  }

  uint8_t4 ep0, ep1;
  uint8_t weights_1p[16];
  uint quant_weight_mode_1p;
  uint8_t4 eps[2] = {};
  uint4 packed_1p;

  // TODO(leegao): look for bit tricks for the 1-partition AABB case
  encode_block_AABB(ep0, ep1, weights_1p);
  quant_weight_mode_1p = 8u; // get_quant_ep_mode is 19 for rgb, 13 for rgba
  eps[0] = uint8_t4(ep0);
  eps[1] = uint8_t4(ep1);
  // TODO(leegao): this is the bottleneck
  packed_1p = pack_astc_4x4_block(eps, weights_1p, use_alpha);
  astc_blocks[buffer_index] = packed_1p;

#ifdef ENABLE_DEBUG
  log_debug(buffer_index, ep0, ep1, weights_1p, pixels, use_alpha);
#endif
}

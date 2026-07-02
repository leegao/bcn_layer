// MIT License
// Copyright (c) 2026 Lee Gao

import astc_pack;
import encoder_utils;

#ifdef ENABLE_DEBUG
import astc_debug;
#endif

[[vk::binding(0, 0)]]
StructuredBuffer<uint> input_rgba8_buffer;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint4> astc_blocks;

typedef uint8_t4[16] Reconstruction;
#ifdef ENABLE_DIAGNOSTICS

struct Diagnostics {
  uint2 start_time;
  float mse;
  uint unused;
#ifndef DISABLE_RECONSTRUCTION
  Reconstruction reconstructed;
#else
  uint reconstructed[16];
#endif
};

[[vk::binding(4, 0)]]
RWStructuredBuffer<Diagnostics> diagnostics;
#endif

[[vk::binding(2, 0)]]
StructuredBuffer<uint> lut2;

[[vk::binding(3, 0)]]
StructuredBuffer<uint> astc_2p_lut_s2;

float reconstruct(uint4 ep0, uint4 ep1, uint weights[16]) {
  float mse = 0.0;
  for (uint i = 0; i < 16; i++) {
    int4 C = pixels[i];
    int4 reconstructed =
        int4(clamp(lerp(ep0, ep1, float(weights[i]) / 255.0), 0, 255));
#ifdef ENABLE_DIAGNOSTICS
#ifndef DISABLE_RECONSTRUCTION
    diagnostics[buffer_index].reconstructed[i] = uint8_t4(reconstructed);
#endif
#endif
    var err = (C - reconstructed) / 255.0;
    mse += dot(err, err);
  }
#ifdef ENABLE_DIAGNOSTICS
  diagnostics[buffer_index].mse = mse / 3.0 / 16.0;
#endif
  return mse / 3.0 / 16.0;
}

float reconstruct_2p(uint4 ep0, uint4 ep1, uint4 ep2, uint4 ep3,
                     uint weights[16], PartitionMap partition_map) {
  float mse = 0.0;
  for (uint i = 0; i < 16; i++) {
    let partition = partition_map[i];
    int4 C = pixels[i];
    int4 reconstructed =
        int4(clamp(lerp(ep0, ep1, float(weights[i]) / 255.0), 0, 255));
    if (partition == 1) {
      reconstructed =
          int4(clamp(lerp(ep2, ep3, float(weights[i]) / 255.0), 0, 255));
    }
#ifdef ENABLE_DIAGNOSTICS
#ifndef DISABLE_RECONSTRUCTION
    diagnostics[buffer_index].reconstructed[i] = uint8_t4(reconstructed);
#endif
#endif
    var err = (C - reconstructed) / 255.0;
    mse += dot(err, err);
  }
#ifdef ENABLE_DIAGNOSTICS
  diagnostics[buffer_index].mse = mse / 3.0 / 16.0;
#endif
  return mse / 3.0 / 16.0;
}

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
static uint4 pixels[16];

extension uint4 {
  half4 to_unorm() { return half4(this) / 255.0h; }
}

void solve_weights(PartitionMap partition_map, int partition, half4 ep0,
                   half4 ep1, out PackedArray<8, 16> weights) {
  let L = ep1 - ep0;
  let invLenSq = 1.0h / (dot(L, L) + 0.000001h);
  for (int i = 0; i < 16; i++) {
    if (partition_map[i] != partition)
      continue;
    let C = pixels[i].to_unorm();
    let pDotL = dot(C - ep0, L);
    let w = pDotL * invLenSq;
    weights[i] = uint(saturate(w) * 255);
  }
}

void encode_block_AABB(out uint4 endpoint_start, out uint4 endpoint_end,
                       out PackedArray<8, 16> weights) {
  uint min_r = 255, min_g = 255, min_b = 255, min_a = 255;
  uint max_r = 0, max_g = 0, max_b = 0, max_a = 0;
  // [00]  01   02  [03]
  //  04   05  [06]  07
  //  08  [09]  10   11
  // [12]  13   14  [15]
  // TODO(leega0): not the bottleneck
  for (int i = 0; i < 16; i += 1) {
    min_r = min(min_r, pixels[i].x);
    min_g = min(min_g, pixels[i].y);
    min_b = min(min_b, pixels[i].z);
    min_a = min(min_a, pixels[i].w);
    max_r = max(max_r, pixels[i].x);
    max_g = max(max_g, pixels[i].y);
    max_b = max(max_b, pixels[i].z);
    max_a = max(max_a, pixels[i].w);
  }

  endpoint_start = uint4(min_r, min_g, min_b, min_a);
  endpoint_end = uint4(max_r, max_g, max_b, max_a);
  var ep0 = half4(endpoint_start) / 255.0h;
  var ep1 = half4(endpoint_end) / 255.0h;
  PartitionMap partition_map = PartitionMap( {});
  solve_weights(partition_map, 0, ep0, ep1, weights);
}

typedef PackedArray<2, 16> PartitionMap;

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

      pixels[y * 4 + x] = uint4(r, g, b, a);
      if (a < 255)
        use_alpha = true;
    }
  }

  uint4 ep0, ep1;
  PackedArray<8, 16> weights_1p;
  PartitionMap partition_map;
  uint astc_seed;
  uint quant_weight_mode_1p;
  uint4 eps[2] = {};
  uint4 packed_1p;

  // TODO(leegao): look for bit tricks for the 1-partition AABB case
  encode_block_AABB(ep0, ep1, weights_1p);
  quant_weight_mode_1p = 8u; // get_quant_ep_mode is 19 for rgb, 13 for rgba
  eps[0] = uint4(ep0);
  eps[1] = uint4(ep1);
  // TODO(leegao): this is the bottleneck
  packed_1p = pack_astc_4x4_block(eps, weights_1p, use_alpha);
  astc_blocks[buffer_index] = packed_1p;

#ifdef ENABLE_DEBUG
  log_debug(buffer_index, ep0, ep1, weights_1p);
#endif

#ifdef ENABLE_DIAGNOSTICS
  diagnostics[buffer_index].start_time = startTime;
  // diagnostics[buffer_index].mse =
  //     get_quant_ep_mode(quant_weight_mode_1p, 16, 8, 1u);
#endif
}

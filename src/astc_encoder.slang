// MIT License
// Copyright (c) 2026 Lee Gao

import astc_pack;
import encoder_utils;

[[vk::binding(0, 0)]]
StructuredBuffer<uint> input_rgba8_buffer;

[[vk::binding(1, 0)]]
RWStructuredBuffer<uint4> astc_blocks;

typedef uint8_t4[16] Reconstruction;
#ifdef ENABLE_DIAGNOSTICS
struct ASTCParameters {
  uint32_t ep0;
  uint32_t ep1;
  uint8_t weights[16];
  uint astc_seed;
  uint partition_map;
};

struct Diagnostics {
  uint2 start_time;
  float mse;
  uint unused;
#ifndef DISABLE_RECONSTRUCTION
  Reconstruction reconstructed;
#else
  uint reconstructed[16];
#endif
  ASTCParameters params;
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
  property bool use_aabb {
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
                   half4 ep1, out uint weights[16]) {
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
                       out uint weights[16]) {
  uint min_r = 255, min_g = 255, min_b = 255, min_a = 255;
  uint max_r = 0, max_g = 0, max_b = 0, max_a = 0;
  for (int i = 0; i < 16; i++) {
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

struct PrincipleComponent {
  half4 centroid;
  half4 axis;
}

PrincipleComponent find_best_eps_via_pca(const PartitionMap partition_map,
                                         uint partition, out half4 ep0,
                                         out half4 ep1) {
  half4 axis = half4(0.17h, 0.83h, 0.38h, 0.2h);
  axis = normalize(axis);
  half4 centroid = half4(0);
  uint count = 0;

  for (uint i = 0; i < 16; i++) {
    if (partition_map[i] != partition)
      continue;
    centroid += pixels[i].to_unorm();
    count += 1;
  }
  centroid /= half(count);

  if (count == 0) {
    ep0 = half4(0.0h);
    ep1 = half4(0.0h);
    return { centroid, axis };
  }

  if (count == 1) {
    ep0 = saturate(centroid);
    ep1 = saturate(centroid);
    return { centroid, axis };
  }

  half4x4 C = (half4x4)0;
  for (uint i = 0; i < 16; i++) {
    if (partition_map[i] != partition)
      continue;
    half4 d = pixels[i].to_unorm() - centroid;
    C[0] += d.x * d; // Row 0: [xx, xy, xz, xw]
    C[1] += d.y * d; // Row 1: [yx, yy, yz, yw]
    C[2] += d.z * d; // Row 2: [zx, zy, zz, zw]
    C[3] += d.w * d; // Row 3: [wx, wy, wz, ww]
  }
  C /= half(count);

  // Check for degenerate cases of single color (low trace of C)
  var trace = C[0].x + C[1].y + C[2].z + C[3].w;
  if (trace < 3e-4) {
    ep0 = saturate(centroid);
    ep1 = saturate(centroid);
    return { centroid, axis };
  }

  for (int iter = 0; iter < 4; iter++) {
    axis = mul(C, axis);
    var lenSq = dot(axis, axis);
    if (lenSq > 1e-8) {
      axis *= rsqrt(lenSq);
    }
  }

  // The data is isotropic (C ~ I), so any direction passing through the
  // centroid is valid
  if (dot(axis, axis) < 1e-8) {
    ep0 = saturate(centroid);
    ep1 = saturate(centroid);
    // ep1 = saturate(pixels[0].to_unorm());
    return { centroid, axis };
  }

  axis = normalize(axis);
  var min_t = 1000.0h;
  var max_t = -1000.0h;

  for (int i = 0; i < 16; i++) {
    if (partition_map[i] != partition)
      continue;
    var d = half4(pixels[i]) / 255.0h - centroid;
    var t = dot(d, axis);
    min_t = min(min_t, t);
    max_t = max(max_t, t);
  }

  ep0 = saturate(centroid + axis * (min_t));
  ep1 = saturate(centroid + axis * (max_t));
  return { centroid, axis };
}

void encode_block(out uint4 ep0_u8, out uint4 ep1_u8, out uint weights[16]) {
  half4 ep0 = half4(0.0h);
  half4 ep1 = half4(0.0h);
  PartitionMap partition_map = PartitionMap( {});
  find_best_eps_via_pca(partition_map, 0, ep0, ep1);
  // 99.9% of the time, this will catch blue contract
  if (ep1.x + ep1.y + ep1.z < ep0.x + ep0.y + ep0.z) {
    var temp = ep0;
    ep0 = ep1;
    ep1 = temp;
  }

  ep0_u8 = uint4(ep0 * 255.0h);
  ep1_u8 = uint4(ep1 * 255.0h);
  solve_weights(partition_map, 0, ep0, ep1, weights);
  return;
}

typedef PackedArray<2, 16> PartitionMap;

void kmeans(out PartitionMap partition_map) {
  half4 centroids[2];
  PackedArray<8, 2> partition_counts;
  centroids[0] = pixels[0].to_unorm();
  centroids[1] = pixels[15].to_unorm();

  const static int max_iterations = 3;
  for (uint iter = 0; iter < max_iterations; iter++) {
    partition_counts.data[0] = 0;
    for (uint i = 0; i < 16; i++) {
      var p = pixels[i].to_unorm();

      var best_dist = 1000.0h;
      uint best_centroid = 0;

      // Nearest centroid through projection
      for (uint centroid = 0; centroid < 2; centroid++) {
        var delta = p - centroids[centroid];
        var d = dot(delta, delta);
        if (d < best_dist) {
          best_dist = d;
          best_centroid = centroid;
        }
      }

      partition_map[i] = best_centroid;
      partition_counts[best_centroid]++;
    }

    // Update centroids
    if (iter < max_iterations - 1) {
      half4 new_sums[2] = {};
      for (uint i = 0; i < 16; i++) {
        var p = pixels[i].to_unorm();
        var idx = partition_map[i];
        new_sums[idx] += p;
      }
      for (uint centroid = 0; centroid < 2; centroid++) {
        var count = partition_counts[centroid];
        if (count != 0) {
          centroids[centroid] = new_sums[centroid] / half(count);
        }
      }
    }
  }
}

extension half4 {
  half4 mean(half4 ep0, half4 ep1) { return half4(this) / 255.0h; }
}

void cluster_lines(inout PartitionMap partition_map, PrincipleComponent l0,
                   PrincipleComponent l1) {
  for (int i = 0; i < 16; i++) {
    var d0 = pixels[i].to_unorm() - l0.centroid;
    var proj0 = d0 - dot(d0, l0.axis) * l0.axis;
    var err0 = dot(proj0, proj0);

    var d1 = pixels[i].to_unorm() - l1.centroid;
    var proj1 = d1 - dot(d1, l1.axis) * l1.axis;
    var err1 = dot(proj1, proj1);
    if (err0 < err1) {
      partition_map[i] = 0;
    } else {
      partition_map[i] = 1;
    }
  }
}

uint get_closest_seed2(uint input, out uint final_pattern) {
  uint key = canonicalize_lut2(input);
  uint seed = (astc_2p_lut_s2[key / 3] >> ((key % 3) * 10)) & 0x3FF;
  final_pattern = lut2[seed];
  return seed;
}

void snap(inout PartitionMap partition_map, out uint astc_seed) {
  static_assert(partition_map.kStorageCount == 1,
                "PartitionMap must have exactly 1 storage element");

  var raw_map = partition_map.data[0];
  uint final_mask;
  astc_seed = get_closest_seed2(raw_map, final_mask);
  partition_map.data[0] = final_mask;
}

void encode_block_2p(out uint4 ep0_u8, out uint4 ep1_u8, out uint4 ep2_u8,
                     out uint4 ep3_u8, out uint weights[16],
                     out PartitionMap partition_map, out uint astc_seed) {
  half4 ep0 = 0.0h, ep1 = 0.0h, ep2 = 0.0h, ep3 = 0.0h;

  // Line-clustering E-M updates
  // partition_map = PartitionMap( { 1, 1, 1, 1, 1, 1, 1, 1 });
  // var l0 = find_best_eps_via_pca(partition_map, 0, ep0, ep1);
  // var l1 = find_best_eps_via_pca(partition_map, 1, ep2, ep3);
  // cluster_lines(partition_map, l0, l1);
  // l0 = find_best_eps_via_pca(partition_map, 0, ep0, ep1);
  // l1 = find_best_eps_via_pca(partition_map, 1, ep2, ep3);
  // cluster_lines(partition_map, l0, l1);
  // l0 = find_best_eps_via_pca(partition_map, 0, ep0, ep1);
  // l1 = find_best_eps_via_pca(partition_map, 1, ep2, ep3);
  // cluster_lines(partition_map, l0, l1);

  kmeans(partition_map);
  snap(partition_map, astc_seed);
  find_best_eps_via_pca(partition_map, 0, ep0, ep1);
  find_best_eps_via_pca(partition_map, 1, ep2, ep3);
  // 99.9% of the time, this will catch blue contract
  if (ep1.x + ep1.y + ep1.z < ep0.x + ep0.y + ep0.z) {
    var temp = ep0;
    ep0 = ep1;
    ep1 = temp;
  }
  if (ep3.x + ep3.y + ep3.z < ep2.x + ep2.y + ep2.z) {
    var temp = ep2;
    ep2 = ep3;
    ep3 = temp;
  }

  ep0_u8 = uint4(ep0 * 255.0h);
  ep1_u8 = uint4(ep1 * 255.0h);
  ep2_u8 = uint4(ep2 * 255.0h);
  ep3_u8 = uint4(ep3 * 255.0h);
  solve_weights(partition_map, 0, ep0, ep1, weights);
  solve_weights(partition_map, 1, ep2, ep3, weights);
  return;
}

void swap_endpoints(inout uint4 ep0, inout uint4 ep1, inout uint weights[16]) {
  // See Table C.2.17
  var temp = ep0;
  ep0 = ep1;
  ep1 = temp;
  for (int i = 0; i < 16; i++) {
    weights[i] = clamp(255 - int(weights[i]), 0, 255);
  }
}

uint quantize(inout uint4 ep0, inout uint4 ep1, inout uint weights[16],
              bool use_alpha) {
  // TODO: find an optimal quant mode
  var quant_weight_mode = 8u;
  var quant_ep_mode =
      get_quant_ep_mode(quant_weight_mode, 16, use_alpha ? 8 : 6, 1);
  quantize_u8(ep0, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  quantize_u8(ep1, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  if (will_blue_contract(ep0.xyz, ep1.xyz, quant_ep_mode))
    swap_endpoints(ep0, ep1, weights);
  quantize_u8(weights, WEIGHT_MAX_VALUES[quant_weight_mode]);
  return quant_weight_mode;
}

uint quantize(inout uint4 ep0, inout uint4 ep1, inout uint4 ep2,
              inout uint4 ep3, inout uint weights[16], bool use_alpha) {
  // TODO: find an optimal quant mode
  var quant_weight_mode = 4u;
  var quant_ep_mode =
      get_quant_ep_mode(quant_weight_mode, 16, use_alpha ? 16 : 12, 2);
  quantize_u8(ep0, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  quantize_u8(ep1, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  quantize_u8(ep2, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  quantize_u8(ep3, ENDPOINT_MAX_VALUES[quant_ep_mode]);
  if (will_blue_contract(ep0.xyz, ep1.xyz, quant_ep_mode))
    swap_endpoints(ep0, ep1, weights);
  if (will_blue_contract(ep2.xyz, ep3.xyz, quant_ep_mode))
    swap_endpoints(ep2, ep3, weights);
  quantize_u8(weights, WEIGHT_MAX_VALUES[quant_weight_mode]);
  return quant_weight_mode;
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

      pixels[y * 4 + x] = uint4(r, g, b, a);
      if (a < 255)
        use_alpha = true;
    }
  }

  uint4 ep0, ep1, ep2, ep3;
  uint weights_1p[16], weights_2p[16];
  PartitionMap partition_map;
  uint astc_seed;
  uint quant_weight_mode_1p, quant_weight_mode_2p;
  float mse_1p = 1000, mse_2p = 1000;
  uint4 eps[4] = {};
  uint4 packed_1p, packed_2p;

  // TODO(leegao): look for bit tricks for the 1-partition AABB case
  // if (!pushConstants.u_Flags.only_2p) {
  //   // if (pushConstants.u_Flags.use_aabb) {
  //   //   encode_block_AABB(ep0, ep1, weights_1p);
  //   // } else {
  //   //   encode_block(ep0, ep1, weights_1p);
  //   // }
  encode_block_AABB(ep0, ep1, weights_1p);
  quant_weight_mode_1p = 8u;
  // quant_weight_mode_1p = quantize(ep0, ep1, weights_1p, use_alpha);
  // mse_1p = reconstruct(ep0, ep1, weights_1p);
  eps[0] = uint4(ep0);
  eps[1] = uint4(ep1);
  packed_1p = pack_astc_4x4_block<1>(eps, weights_1p, quant_weight_mode_1p, 0u,
                                     use_alpha);
  astc_blocks[buffer_index] = packed_1p;
  // }
  // astc_blocks[buffer_index] = uint4(0);

  // if (pushConstants.u_Flags.try_2p || pushConstants.u_Flags.only_2p) {
  //   encode_block_2p(ep0, ep1, ep2, ep3, weights_2p, partition_map,
  //   astc_seed); quant_weight_mode_2p = quantize(ep0, ep1, ep2, ep3,
  //   weights_2p, use_alpha); mse_2p = reconstruct_2p(ep0, ep1, ep2, ep3,
  //   weights_2p, partition_map); eps = { uint4(ep0), uint4(ep1), uint4(ep2),
  //   uint4(ep3) }; packed_2p = pack_astc_4x4_block<2>(eps, weights_2p,
  //   quant_weight_mode_2p,
  //                                      astc_seed, use_alpha);
  // }

  //   if (mse_1p < mse_2p) {
  //     astc_blocks[buffer_index] = packed_1p;
  // #ifdef ENABLE_DIAGNOSTICS
  //     diagnostics[buffer_index].mse = mse_1p;
  // #endif
  //   } else {
  //     astc_blocks[buffer_index] = packed_2p;
  //   }

#ifdef ENABLE_DIAGNOSTICS
  diagnostics[buffer_index].start_time = startTime;
  diagnostics[buffer_index].params.ep0 = ep0.x | (ep0.y << 8) | (ep0.z << 16);
  diagnostics[buffer_index].params.ep1 = ep1.x | (ep1.y << 8) | (ep1.z << 16);
  for (int i = 0; i < 16; i++) {
    diagnostics[buffer_index].params.weights[i] =
        uint8_t(weights_2p[i] & 0xFFu);
  }
  diagnostics[buffer_index].params.astc_seed = astc_seed;
  diagnostics[buffer_index].params.partition_map = partition_map.data[0];
#endif
}

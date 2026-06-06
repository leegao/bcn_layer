// Packing functions for BISE encoding for eps and weights for ASTC
import encoder_utils;

// static const uint3 bits_trits_quints_table[21] = {
// ...
//   uint3(4, 0, 0), // 8: QUANT_16 *
// ...
//   uint3(4, 1, 0), // 13: QUANT_48 *
// ...
//   uint3(6, 1, 0), // 19: QUANT_192 *
// };

static const uint WEIGHT_MAX_VALUES[12] = { 1, 2,  3,  4,  5,  7,
                                            9, 11, 15, 19, 23, 31 };
static const uint ENDPOINT_MAX_VALUES[21] = { 1,  2,  3,  4,   5,   7,   9,
                                              11, 15, 19, 23,  31,  39,  47,
                                              63, 79, 95, 127, 159, 191, 255 };

static const uint packed_trit_data[61] = {
  0x04020100, 0x09080605, 0x1211100A, 0x18161514, 0x07031A19, 0x1B17130F,
  0x200E0D0C, 0x25242221, 0x2A292826, 0x34323130, 0x39383635, 0x2F27233A,
  0x2C3B3733, 0x41402E2D, 0x46454442, 0x504A4948, 0x55545251, 0x5A595856,
  0x534F4743, 0x4D4C5B57, 0x8281804E, 0x88868584, 0x91908A89, 0x96959492,
  0x839A9998, 0x97938F87, 0x8E8D8C9B, 0xA4A2A1A0, 0xA9A8A6A5, 0xB2B1B0AA,
  0xB8B6B5B4, 0xA7A3BAB9, 0xBBB7B3AF, 0xC0AEADAC, 0xC5C4C2C1, 0xCAC9C8C6,
  0xD4D2D1D0, 0xD9D8D6D5, 0xCFC7C3DA, 0xCCDBD7D3, 0x6160CECD, 0x66656462,
  0x706A6968, 0x75747271, 0x7A797876, 0x736F6763, 0x6D6C7B77, 0xE2E1E06E,
  0xE8E6E5E4, 0xF1F0EAE9, 0xF6F5F4F2, 0xE3FAF9F8, 0xF7F3EFE7, 0xEEEDECFB,
  0x3C1E1D1C, 0x5D5C3E3D, 0x9E9D9C5E, 0xDCBEBDBC, 0x3F1FDEDD, 0xFFBF9F7F,
  0x00FEFDFC,
};

// Static lookup table to invert the scrambling in C.2.17, see
// https://registry.khronos.org/OpenGL/extensions/OES/OES_texture_compression_astc.txt
// See generate_endpoint_scramble_lut.py for the generation
static const uint packed_unscrambler_data[60] = {
  0x02201000, 0x14042212, 0x26160624, 0x0A281808, 0x1C0C2A1A, 0x2E1E0E2C,
  0x2D0F1F2F, 0x1B2B0D1D, 0x0919290B, 0x25071727, 0x13230515, 0x01112103,
  0x02804000, 0x44048242, 0x86460684, 0x0A884808, 0x4C0C8A4A, 0x8E4E0E8C,
  0x12905010, 0x54149252, 0x96561694, 0x1A985818, 0x5C1C9A5A, 0x9E5E1E9C,
  0x22A06020, 0x6424A262, 0xA66626A4, 0x2AA86828, 0x6C2CAA6A, 0xAE6E2EAC,
  0x32B07030, 0x7434B272, 0xB67636B4, 0x3AB87838, 0x7C3CBA7A, 0xBE7E3EBC,
  0xBD3F7FBF, 0x7BBB3D7D, 0x3979B93B, 0xB53777B7, 0x73B33575, 0x3171B133,
  0xAD2F6FAF, 0x6BAB2D6D, 0x2969A92B, 0xA52767A7, 0x63A32565, 0x2161A123,
  0x9D1F5F9F, 0x5B9B1D5D, 0x1959991B, 0x95175797, 0x53931555, 0x11519113,
  0x8D0F4F8F, 0x4B8B0D4D, 0x0949890B, 0x85074787, 0x43830545, 0x01418103,

};

inline uint get_integer_from_trits(uint index) {
  uint wordIndex = index >> 2;
  uint shift = (index & 3) << 3;
  return (packed_trit_data[wordIndex] >> shift) & 0xFFu;
}

inline uint unscramble_quantized(uint quant_mode, uint q_val) {
  let offset = quant_mode == 19 ? 48u : 0u;
  let index = q_val + offset;

  uint wordIndex = index >> 2;
  uint shift = (index & 3) << 3;
  return (packed_unscrambler_data[wordIndex] >> shift) & 0xFFu;
}

void write_bits_fwd(inout uint block_x, inout uint block_y,
                    inout uint bit_offset, uint value, uint count) {
  uint clean_value = value & ((1u << count) - 1u);
  uint bit_idx = bit_offset & 31u;
  uint val0 = clean_value << bit_idx;
  bool does_overflow = (bit_idx + count > 32u);
  uint val1 = does_overflow ? (clean_value >> ((32u - bit_idx) & 31u)) : 0u;

  uint word_idx = bit_offset >> 5u;
  if (word_idx == 0u) {
    block_x |= val0;
    block_y |= val1;
  } else if (word_idx == 1u) {
    block_y |= val0;
  }

  bit_offset += count;
}

void encode_trits(inout uint block_x, inout uint block_y, inout uint bit_offset,
                  const PackedArray<8, 8> numbers, uint bits, uint count) {
  for (uint i = 0; i < count; i += 5) {
    PackedArray<8, 5> group;
    group.data = {};
    for (uint j = 0; j < 5; ++j) {
      if (i + j < count)
        group[j] = numbers[i + j];
    }
    PackedArray<8, 5> m, t;
    for (uint j = 0; j < 5; ++j) {
      m[j] = group[j] & ((1 << bits) - 1);
      t[j] = group[j] >> bits;
    }

    var index = t[4] * 81 + t[3] * 27 + t[2] * 9 + t[1] * 3 + t[0];
    uint packhigh = get_integer_from_trits(index);
    write_bits_fwd(block_x, block_y, bit_offset, m[0], bits);
    write_bits_fwd(block_x, block_y, bit_offset, packhigh & 3, 2);
    write_bits_fwd(block_x, block_y, bit_offset, m[1], bits);
    write_bits_fwd(block_x, block_y, bit_offset, (packhigh >> 2) & 3, 2);
    write_bits_fwd(block_x, block_y, bit_offset, m[2], bits);
    write_bits_fwd(block_x, block_y, bit_offset, (packhigh >> 4) & 1, 1);
    write_bits_fwd(block_x, block_y, bit_offset, m[3], bits);
    write_bits_fwd(block_x, block_y, bit_offset, (packhigh >> 5) & 3, 2);
    write_bits_fwd(block_x, block_y, bit_offset, m[4], bits);
    write_bits_fwd(block_x, block_y, bit_offset, (packhigh >> 7) & 1, 1);
  }
}

uint4 pack_astc_4x4_block(const uint4 eps[2], const PackedArray<8, 16> weights,
                          bool use_alpha) {
  uint4 final_block = uint4(0, 0, 0, 0);

  const uint quant_weight_mode = 8u;
  const uint num_weights = 16u;
  let num_endpoints = use_alpha ? 8u : 6u;
  let quant_ep_mode = use_alpha ? 13u : 19u;
  let cem = use_alpha ? 12u : 8u; // CEM_LDR_RGBA_DIRECT or CEM_LDR_RGB_DIRECT
  let ep_bits = use_alpha ? 4u : 6u;

  // Blockmode - C.2.10
  const uint a = (4 - 2) & 0x3; // 2
  const uint b = (4 - 4) & 0x3; // 0
  const uint d = 0;             // hard coded single-plane mode
  const uint h = (quant_weight_mode < 6) ? 0 : 1;
  const uint r = (quant_weight_mode % 6) + 2;

  const uint block_mode = ((r >> 1) & 0x3) | ((r & 0x1) << 4) |
                          ((a & 0x3) << 5) | ((b & 0x3) << 7) | (h << 9) |
                          (d << 10);

  final_block.x = (block_mode & 0x7FFu) | ((cem & 0xFu) << 13u);
  uint fwd_offset = 17u;

  // Weights - C.2.17
  let w_max = WEIGHT_MAX_VALUES[quant_weight_mode];
  [[unroll]]
  for (uint i = 0u; i < 16u; ++i) {
    uint q_w = uint((float(weights[i]) * float(w_max)) / 255.0f + 0.5f);
    q_w = clamp(q_w, 0u, w_max);
    uint rev4 = reversebits(q_w) >> 28u;
    if (i < 8u) {
      final_block.w |= (rev4 << (28u - 4u * i));
    } else {
      final_block.z |= (rev4 << (28u - 4u * (i - 8u)));
    }
  }

  // Endpoints - C.2.13
  let ep_max = use_alpha ? ENDPOINT_MAX_VALUES[13u] : ENDPOINT_MAX_VALUES[19u];
  PackedArray<8, 8> raw_endpoints;
  uint ep_idx = 0;
  // Interleave endpoints
  let ep0 = eps[0u];
  let ep1 = eps[1u];
  raw_endpoints[ep_idx++] = ep0.x;
  raw_endpoints[ep_idx++] = ep1.x;
  raw_endpoints[ep_idx++] = ep0.y;
  raw_endpoints[ep_idx++] = ep1.y;
  raw_endpoints[ep_idx++] = ep0.z;
  raw_endpoints[ep_idx++] = ep1.z;
  raw_endpoints[ep_idx++] = ep0.w;
  raw_endpoints[ep_idx++] = ep1.w;

  PackedArray<8, 8> quantized_endpoints;
  [[unroll]]
  for (uint i = 0; i < num_endpoints; ++i) {
    uint q_ep = uint((float(raw_endpoints[i]) * float(ep_max)) / 255.0f + 0.5f);
    q_ep = clamp(q_ep, 0u, ep_max);
    quantized_endpoints[i] = unscramble_quantized(quant_ep_mode, q_ep);
  }

  encode_trits(final_block.x, final_block.y, fwd_offset, quantized_endpoints,
               ep_bits, num_endpoints);

  return final_block;
}

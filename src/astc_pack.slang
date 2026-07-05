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

static const uint8_t packed_trit_data[243] = {
  0,   1,   2,   4,   5,   6,   8,   9,   10,  16,  17,  18,  20,  21,  22,
  24,  25,  26,  3,   7,   15,  19,  23,  27,  12,  13,  14,  32,  33,  34,
  36,  37,  38,  40,  41,  42,  48,  49,  50,  52,  53,  54,  56,  57,  58,
  35,  39,  47,  51,  55,  59,  44,  45,  46,  64,  65,  66,  68,  69,  70,
  72,  73,  74,  80,  81,  82,  84,  85,  86,  88,  89,  90,  67,  71,  79,
  83,  87,  91,  76,  77,  78,  128, 129, 130, 132, 133, 134, 136, 137, 138,
  144, 145, 146, 148, 149, 150, 152, 153, 154, 131, 135, 143, 147, 151, 155,
  140, 141, 142, 160, 161, 162, 164, 165, 166, 168, 169, 170, 176, 177, 178,
  180, 181, 182, 184, 185, 186, 163, 167, 175, 179, 183, 187, 172, 173, 174,
  192, 193, 194, 196, 197, 198, 200, 201, 202, 208, 209, 210, 212, 213, 214,
  216, 217, 218, 195, 199, 207, 211, 215, 219, 204, 205, 206, 96,  97,  98,
  100, 101, 102, 104, 105, 106, 112, 113, 114, 116, 117, 118, 120, 121, 122,
  99,  103, 111, 115, 119, 123, 108, 109, 110, 224, 225, 226, 228, 229, 230,
  232, 233, 234, 240, 241, 242, 244, 245, 246, 248, 249, 250, 227, 231, 239,
  243, 247, 251, 236, 237, 238, 28,  29,  30,  60,  61,  62,  92,  93,  94,
  156, 157, 158, 188, 189, 190, 220, 221, 222, 31,  63,  127, 159, 191, 255,
  252, 253, 254
};

// Static lookup table to invert the scrambling in C.2.17, see
// https://registry.khronos.org/OpenGL/extensions/OES/OES_texture_compression_astc.txt
// See generate_endpoint_scramble_lut.py for the generation
static const uint8_t packed_unscrambler_data[240] = {
  0,   16,  32,  2,   18,  34,  4,   20,  36,  6,   22,  38,  8,   24,  40,
  10,  26,  42,  12,  28,  44,  14,  30,  46,  47,  31,  15,  45,  29,  13,
  43,  27,  11,  41,  25,  9,   39,  23,  7,   37,  21,  5,   35,  19,  3,
  33,  17,  1,   0,   64,  128, 2,   66,  130, 4,   68,  132, 6,   70,  134,
  8,   72,  136, 10,  74,  138, 12,  76,  140, 14,  78,  142, 16,  80,  144,
  18,  82,  146, 20,  84,  148, 22,  86,  150, 24,  88,  152, 26,  90,  154,
  28,  92,  156, 30,  94,  158, 32,  96,  160, 34,  98,  162, 36,  100, 164,
  38,  102, 166, 40,  104, 168, 42,  106, 170, 44,  108, 172, 46,  110, 174,
  48,  112, 176, 50,  114, 178, 52,  116, 180, 54,  118, 182, 56,  120, 184,
  58,  122, 186, 60,  124, 188, 62,  126, 190, 191, 127, 63,  189, 125, 61,
  187, 123, 59,  185, 121, 57,  183, 119, 55,  181, 117, 53,  179, 115, 51,
  177, 113, 49,  175, 111, 47,  173, 109, 45,  171, 107, 43,  169, 105, 41,
  167, 103, 39,  165, 101, 37,  163, 99,  35,  161, 97,  33,  159, 95,  31,
  157, 93,  29,  155, 91,  27,  153, 89,  25,  151, 87,  23,  149, 85,  21,
  147, 83,  19,  145, 81,  17,  143, 79,  15,  141, 77,  13,  139, 75,  11,
  137, 73,  9,   135, 71,  7,   133, 69,  5,   131, 67,  3,   129, 65,  1,
};

inline uint get_integer_from_trits(uint index) {
  return packed_trit_data[index];
}

inline uint unscramble_quantized(uint index) {
  return packed_unscrambler_data[index];
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

#ifdef TEMPLATED_ENCODE_TRITS
void encode_trits<let bits : uint>(inout uint block_x, inout uint block_y,
                                   inout uint bit_offset,
                                   const uint8_t numbers[8], uint count) {
#else
void encode_trits(inout uint block_x, inout uint block_y, inout uint bit_offset,
                  const uint8_t numbers[8], uint bits, uint count) {
#endif
  for (uint i = 0; i < count; i += 5) {
    uint8_t group[5] = {};
    for (uint j = 0; j < 5; ++j) {
      if (i + j < count)
        group[j] = numbers[i + j];
    }
    uint8_t m[5], t[5];
    for (uint j = 0; j < 5; ++j) {
      m[j] = uint8_t(group[j] & ((1 << bits) - 1));
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

uint4 pack_astc_4x4_block(const uint8_t4 eps[2], const uint8_t weights[16],
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
  let w_max_scaled = ((w_max << 16u) + 127u) / 255u;
  [[unroll]]
  for (uint i = 0u; i < 16u; ++i) {
    // uint q_w = uint((float(weights[i]) * float(w_max)) / 255.0f + 0.5f);
    // This is equivalent to scaling everytihng up by 2^16
    uint q_w = (uint(weights[i]) * w_max_scaled + 32768u) >> 16u;
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
  let ep_max_scaled = ((ep_max << 16u) + 127u) / 255u;
  let unscrambler_offset = use_alpha ? 0u : 48u;

  uint8_t quantized_endpoints[8];
  [[unroll]]
  for (uint i = 0; i < num_endpoints; ++i) {
    uint8_t raw_endpoint = eps[i & 1u][i >> 1u];
    uint q_ep = (uint(raw_endpoint) * ep_max_scaled + 32768u) >> 16u;
    q_ep = clamp(q_ep, 0u, ep_max);
    quantized_endpoints[i] =
        uint8_t(unscramble_quantized(q_ep + unscrambler_offset));
  }

#ifdef TEMPLATED_ENCODE_TRITS
  if (use_alpha) {
    encode_trits<4>(final_block.x, final_block.y, fwd_offset,
                    quantized_endpoints, 8u);
  } else {
    encode_trits<6>(final_block.x, final_block.y, fwd_offset,
                    quantized_endpoints, 6u);
  }
#else
  encode_trits(final_block.x, final_block.y, fwd_offset, quantized_endpoints,
               ep_bits, num_endpoints);
#endif

  return final_block;
}

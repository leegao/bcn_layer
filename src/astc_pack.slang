// Packing functions for BISE encoding for eps and weights for ASTC

static const uint3 bits_trits_quints_table[21] = {
  // 2^e0 * 3^e1 * 5^e2
  uint3(1, 0, 0), // 0: QUANT_2
  uint3(0, 1, 0), // 1: QUANT_3
  uint3(2, 0, 0), // 2: QUANT_4
  uint3(0, 0, 1), // 3: QUANT_5
  uint3(1, 1, 0), // 4: QUANT_6
  uint3(3, 0, 0), // 5: QUANT_8
  uint3(1, 0, 1), // 6: QUANT_10
  uint3(2, 1, 0), // 7: QUANT_12
  uint3(4, 0, 0), // 8: QUANT_16
  uint3(2, 0, 1), // 9: QUANT_20
  uint3(3, 1, 0), // 10: QUANT_24
  uint3(5, 0, 0), // 11: QUANT_32
  uint3(3, 0, 1), // 12: QUANT_40
  uint3(4, 1, 0), // 13: QUANT_48
  uint3(6, 0, 0), // 14: QUANT_64
  uint3(4, 0, 1), // 15: QUANT_80
  uint3(5, 1, 0), // 16: QUANT_96
  uint3(7, 0, 0), // 17: QUANT_128
  uint3(5, 0, 1), // 18: QUANT_160
  uint3(6, 1, 0), // 19: QUANT_192
  uint3(8, 0, 0), // 20: QUANT_256
};

extension uint3 {
  property uint bits {
    get { return this.x; }
  }

  property bool trit {
    get { return (bool)this.y; }
  }

  property bool quint {
    get { return (bool)this.z; }
  }
}

static const uint WEIGHT_MAX_VALUES[12] = { 1, 2,  3,  4,  5,  7,
                                            9, 11, 15, 19, 23, 31 };
static const uint ENDPOINT_MAX_VALUES[21] = { 1,  2,  3,  4,   5,   7,   9,
                                              11, 15, 19, 23,  31,  39,  47,
                                              63, 79, 95, 127, 159, 191, 255 };

static const int integer_from_trits[243] = {
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

static const int integer_from_quints[125] = {
  0,   1,   2,   3,   4,   8,   9,   10,  11,  12,  16,  17,  18,  19,
  20,  24,  25,  26,  27,  28,  5,   13,  21,  29,  6,   32,  33,  34,
  35,  36,  40,  41,  42,  43,  44,  48,  49,  50,  51,  52,  56,  57,
  58,  59,  60,  37,  45,  53,  61,  14,  64,  65,  66,  67,  68,  72,
  73,  74,  75,  76,  80,  81,  82,  83,  84,  88,  89,  90,  91,  92,
  69,  77,  85,  93,  22,  96,  97,  98,  99,  100, 104, 105, 106, 107,
  108, 112, 113, 114, 115, 116, 120, 121, 122, 123, 124, 101, 109, 117,
  125, 30,  102, 103, 70,  71,  38,  110, 111, 78,  79,  46,  118, 119,
  86,  87,  54,  126, 127, 94,  95,  62,  39,  47,  55,  63,  31
};

void write_bits(inout uint4 block, inout uint bit_offset, uint value,
                uint count, bool bwd = false) {
  for (uint i = 0; i < count; ++i) {
    uint bit = (value >> i) & 1;

    uint word_idx, bit_idx;
    let target_bit = bwd ? 127 - bit_offset : bit_offset;
    word_idx = target_bit >> 5;
    bit_idx = target_bit & 31;

    if (word_idx == 0)
      block.x |= (bit << bit_idx);
    else if (word_idx == 1)
      block.y |= (bit << bit_idx);
    else if (word_idx == 2)
      block.z |= (bit << bit_idx);
    else if (word_idx == 3)
      block.w |= (bit << bit_idx);
    bit_offset++;
  }
}

void encode_bise(inout uint4 block, inout uint bit_offset, const uint numbers[],
                 uint range_mode, uint count, bool bwd = false) {
  uint3 tq = bits_trits_quints_table[range_mode];
  uint bits = tq.bits;
  if (tq.trit) {
    // Trits Mode
    for (uint i = 0; i < count; i += 5) {
      uint group[5] = { 0, 0, 0, 0, 0 };
      for (uint j = 0; j < 5; ++j) {
        if (i + j < count)
          group[j] = numbers[i + j];
      }
      uint m[5], t[5];
      for (uint j = 0; j < 5; ++j) {
        m[j] = group[j] & ((1 << bits) - 1);
        t[j] = group[j] >> bits;
      }

      uint packhigh = integer_from_trits[t[4] * 81 + t[3] * 27 + t[2] * 9 +
                                         t[1] * 3 + t[0]];
      write_bits(block, bit_offset, m[0], bits, bwd);
      write_bits(block, bit_offset, packhigh & 3, 2, bwd);
      write_bits(block, bit_offset, m[1], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 2) & 3, 2, bwd);
      write_bits(block, bit_offset, m[2], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 4) & 1, 1, bwd);
      write_bits(block, bit_offset, m[3], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 5) & 3, 2, bwd);
      write_bits(block, bit_offset, m[4], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 7) & 1, 1, bwd);
    }
  } else if (tq.quint) {
    // Quints Mode
    for (uint i = 0; i < count; i += 3) {
      uint group[3] = { 0, 0, 0 };
      for (uint j = 0; j < 3; ++j) {
        if (i + j < count)
          group[j] = numbers[i + j];
      }
      uint m[3], q[3];
      for (uint j = 0; j < 3; ++j) {
        m[j] = group[j] & ((1 << bits) - 1);
        q[j] = group[j] >> bits;
      }

      uint packhigh = integer_from_quints[q[2] * 25 + q[1] * 5 + q[0]];
      write_bits(block, bit_offset, m[0], bits, bwd);
      write_bits(block, bit_offset, packhigh & 7, 3, bwd);
      write_bits(block, bit_offset, m[1], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 3) & 3, 2, bwd);
      write_bits(block, bit_offset, m[2], bits, bwd);
      write_bits(block, bit_offset, (packhigh >> 5) & 3, 2, bwd);
    }
  } else {
    // Bit Mode
    for (uint i = 0; i < count; ++i) {
      write_bits(block, bit_offset, numbers[i], bits, bwd);
    }
  }
}

uint compute_ise_bitcount(uint items, uint range_mode) {
  uint3 tq = bits_trits_quints_table[range_mode];
  if (tq.trit)
    return ((8 + 5 * tq.bits) * items + 4) / 5;
  if (tq.quint)
    return ((7 + 3 * tq.bits) * items + 2) / 3;
  return items * tq.bits;
}

// Static lookup table to invert the scrambling in C.2.17, see
// https://registry.khronos.org/OpenGL/extensions/OES/OES_texture_compression_astc.txt
// T = D * C + B;
// T = T ^ A;
// T = (A & 0x20) | (T >> 2);
// if (T > 32) { T += 1; }
// See generate_endpoint_scramble_lut.py for the generation
static const uint unscrambler_lut[688] = {
  0,   2,   4,   5,   3,   1,   0,   2,   4,   6,   8,   9,   7,   5,   3,
  1,   0,   4,   8,   2,   6,   10,  11,  7,   3,   9,   5,   1,   0,   4,
  8,   12,  16,  2,   6,   10,  14,  18,  19,  15,  11,  7,   3,   17,  13,
  9,   5,   1,   0,   8,   16,  2,   10,  18,  4,   12,  20,  6,   14,  22,
  23,  15,  7,   21,  13,  5,   19,  11,  3,   17,  9,   1,   0,   8,   16,
  24,  32,  2,   10,  18,  26,  34,  4,   12,  20,  28,  36,  6,   14,  22,
  30,  38,  39,  31,  23,  15,  7,   37,  29,  21,  13,  5,   35,  27,  19,
  11,  3,   33,  25,  17,  9,   1,   0,   16,  32,  2,   18,  34,  4,   20,
  36,  6,   22,  38,  8,   24,  40,  10,  26,  42,  12,  28,  44,  14,  30,
  46,  47,  31,  15,  45,  29,  13,  43,  27,  11,  41,  25,  9,   39,  23,
  7,   37,  21,  5,   35,  19,  3,   33,  17,  1,   0,   16,  32,  48,  64,
  2,   18,  34,  50,  66,  4,   20,  36,  52,  68,  6,   22,  38,  54,  70,
  8,   24,  40,  56,  72,  10,  26,  42,  58,  74,  12,  28,  44,  60,  76,
  14,  30,  46,  62,  78,  79,  63,  47,  31,  15,  77,  61,  45,  29,  13,
  75,  59,  43,  27,  11,  73,  57,  41,  25,  9,   71,  55,  39,  23,  7,
  69,  53,  37,  21,  5,   67,  51,  35,  19,  3,   65,  49,  33,  17,  1,
  0,   32,  64,  2,   34,  66,  4,   36,  68,  6,   38,  70,  8,   40,  72,
  10,  42,  74,  12,  44,  76,  14,  46,  78,  16,  48,  80,  18,  50,  82,
  20,  52,  84,  22,  54,  86,  24,  56,  88,  26,  58,  90,  28,  60,  92,
  30,  62,  94,  95,  63,  31,  93,  61,  29,  91,  59,  27,  89,  57,  25,
  87,  55,  23,  85,  53,  21,  83,  51,  19,  81,  49,  17,  79,  47,  15,
  77,  45,  13,  75,  43,  11,  73,  41,  9,   71,  39,  7,   69,  37,  5,
  67,  35,  3,   65,  33,  1,   0,   32,  64,  96,  128, 2,   34,  66,  98,
  130, 4,   36,  68,  100, 132, 6,   38,  70,  102, 134, 8,   40,  72,  104,
  136, 10,  42,  74,  106, 138, 12,  44,  76,  108, 140, 14,  46,  78,  110,
  142, 16,  48,  80,  112, 144, 18,  50,  82,  114, 146, 20,  52,  84,  116,
  148, 22,  54,  86,  118, 150, 24,  56,  88,  120, 152, 26,  58,  90,  122,
  154, 28,  60,  92,  124, 156, 30,  62,  94,  126, 158, 159, 127, 95,  63,
  31,  157, 125, 93,  61,  29,  155, 123, 91,  59,  27,  153, 121, 89,  57,
  25,  151, 119, 87,  55,  23,  149, 117, 85,  53,  21,  147, 115, 83,  51,
  19,  145, 113, 81,  49,  17,  143, 111, 79,  47,  15,  141, 109, 77,  45,
  13,  139, 107, 75,  43,  11,  137, 105, 73,  41,  9,   135, 103, 71,  39,
  7,   133, 101, 69,  37,  5,   131, 99,  67,  35,  3,   129, 97,  65,  33,
  1,   0,   64,  128, 2,   66,  130, 4,   68,  132, 6,   70,  134, 8,   72,
  136, 10,  74,  138, 12,  76,  140, 14,  78,  142, 16,  80,  144, 18,  82,
  146, 20,  84,  148, 22,  86,  150, 24,  88,  152, 26,  90,  154, 28,  92,
  156, 30,  94,  158, 32,  96,  160, 34,  98,  162, 36,  100, 164, 38,  102,
  166, 40,  104, 168, 42,  106, 170, 44,  108, 172, 46,  110, 174, 48,  112,
  176, 50,  114, 178, 52,  116, 180, 54,  118, 182, 56,  120, 184, 58,  122,
  186, 60,  124, 188, 62,  126, 190, 191, 127, 63,  189, 125, 61,  187, 123,
  59,  185, 121, 57,  183, 119, 55,  181, 117, 53,  179, 115, 51,  177, 113,
  49,  175, 111, 47,  173, 109, 45,  171, 107, 43,  169, 105, 41,  167, 103,
  39,  165, 101, 37,  163, 99,  35,  161, 97,  33,  159, 95,  31,  157, 93,
  29,  155, 91,  27,  153, 89,  25,  151, 87,  23,  149, 85,  21,  147, 83,
  19,  145, 81,  17,  143, 79,  15,  141, 77,  13,  139, 75,  11,  137, 73,
  9,   135, 71,  7,   133, 69,  5,   131, 67,  3,   129, 65,  1,
};

static const int unscrambler_offsets[21] = { -1, -1,  -1,  -1, 0,   -1,  6,
                                             16, -1,  28,  48, -1,  72,  112,
                                             -1, 160, 240, -1, 336, 496, -1 };

uint unscramble_quantized(uint quant_mode, uint q_val) {
  int offset = unscrambler_offsets[quant_mode];
  if (offset < 0) {
    return q_val;
  }
  return unscrambler_lut[offset + q_val];
}

uint get_quant_ep_mode(uint quant_weight_mode, uint num_weights,
                       uint num_endpoints, uint partitions) {

  let config_bits =
      partitions == 1 ? 17u : 29u; // 11 (block mode) + 2 (partition flag) + 4|6
                                   // (CEM) + 10 (partition index)
  let weight_bits = compute_ise_bitcount(num_weights, quant_weight_mode);
  let remaining_bits = uint(128u - config_bits - weight_bits);
  // Find the ep mode, which is the maximal mode that fits remaining bits
  uint quant_ep_mode;
  for (int mode_idx = 20; mode_idx >= 0; --mode_idx) {
    if (compute_ise_bitcount(num_endpoints, mode_idx) <= remaining_bits) {
      quant_ep_mode = mode_idx;
      break;
    }
  }
  return quant_ep_mode;
}

uint4 pack_astc_4x4_block<let partitions : uint>(const uint4 eps[4],
                                                 const uint weights[16],
                                                 uint quant_weight_mode,
                                                 uint partition_seed,
                                                 bool use_alpha) {
  uint4 final_block = uint4(0, 0, 0, 0);

  let num_weights = 16u;
  let num_endpoints = (use_alpha ? 8u : 6u) * partitions;
  let quant_ep_mode = get_quant_ep_mode(quant_weight_mode, num_weights,
                                        num_endpoints, partitions);

  // Blockmode - C.2.10
  uint a = (4 - 2) & 0x3; // 2
  uint b = (4 - 4) & 0x3; // 0
  uint d = 0;             // hard coded single-plane mode
  uint h = (quant_weight_mode < 6) ? 0 : 1;
  uint r = (quant_weight_mode % 6) + 2;

  uint block_mode = ((r >> 1) & 0x3);
  block_mode |= (r & 0x1) << 4;
  block_mode |= (a & 0x3) << 5;
  block_mode |= (b & 0x3) << 7;
  block_mode |= h << 9;
  block_mode |= d << 10;

  let cem = use_alpha ? 12u : 8u; // CEM_LDR_RGBA_DIRECT or CEM_LDR_RGB_DIRECT

  // Weights - C.2.17
  let w_max = WEIGHT_MAX_VALUES[quant_weight_mode];
  uint quantized_weights[16];
  for (uint i = 0; i < 16; ++i) {
    uint q_w = uint((float(weights[i]) * float(w_max)) / 255.0f + 0.5f);
    q_w = clamp(q_w, 0u, w_max);
    quantized_weights[i] = unscramble_quantized(quant_weight_mode, q_w);
  }

  // Endpoints - C.2.13
  let ep_max = ENDPOINT_MAX_VALUES[quant_ep_mode];

  uint raw_endpoints[partitions * 8u];
  uint ep_idx = 0;

  // Interleave endpoints
  for (uint p = 0; p < partitions; ++p) {
    var ep0 = eps[p * 2u + 0u];
    var ep1 = eps[p * 2u + 1u];
    raw_endpoints[ep_idx++] = ep0.x;
    raw_endpoints[ep_idx++] = ep1.x;
    raw_endpoints[ep_idx++] = ep0.y;
    raw_endpoints[ep_idx++] = ep1.y;
    raw_endpoints[ep_idx++] = ep0.z;
    raw_endpoints[ep_idx++] = ep1.z;
    if (use_alpha) {
      raw_endpoints[ep_idx++] = ep0.w;
      raw_endpoints[ep_idx++] = ep1.w;
    }
  }

  uint quantized_endpoints[partitions * 8u];
  for (uint i = 0; i < num_endpoints; ++i) {
    uint q_ep = uint((float(raw_endpoints[i]) * float(ep_max)) / 255.0f + 0.5f);
    q_ep = clamp(q_ep, 0u, ep_max);
    quantized_endpoints[i] = unscramble_quantized(quant_ep_mode, q_ep);
  }

  uint fwd_offset = 0;
  write_bits(final_block, fwd_offset, block_mode, 11);
  // 1P -> 00, 2P -> 01, 3P -> 10
  write_bits(final_block, fwd_offset, partitions - 1u, 2);
  if (partitions > 1u) {
    write_bits(final_block, fwd_offset, partition_seed, 10);
    write_bits(final_block, fwd_offset, 0u, 2); // in 2P, CEM is 6 bits
  }
  write_bits(final_block, fwd_offset, cem, 4);

  // Endpoints are encoded in forward direction (lsb -> msb)
  encode_bise(final_block, fwd_offset, quantized_endpoints, quant_ep_mode,
              num_endpoints);

  // Weights are encoded in reverse direction (msb -> lsb)
  uint bwd_offset = 0;
  encode_bise(final_block, bwd_offset, quantized_weights, quant_weight_mode,
              num_weights, /*bwd*/ true);

  return final_block;
}

// Lookup table that takes a (quant_mode, unscrambled_val) back to what the
// hardware would have decoded this value to.
// This is needed to accurately perform blue-contract detection
// See generate_endpoint_unquant_lut.py
static const uint unquant_lut[1206] = {
  0,   255, 0,   130, 255, 0,   85,  170, 255, 0,   65,  130, 190, 255, 0,
  255, 51,  204, 102, 153, 0,   36,  73,  109, 146, 182, 219, 255, 0,   255,
  28,  227, 56,  199, 84,  171, 113, 142, 0,   255, 69,  186, 23,  232, 92,
  163, 46,  209, 116, 139, 0,   17,  34,  51,  68,  85,  102, 119, 136, 153,
  170, 187, 204, 221, 238, 255, 0,   255, 67,  188, 13,  242, 80,  175, 27,
  228, 94,  161, 40,  215, 107, 148, 54,  201, 121, 134, 0,   255, 33,  222,
  66,  189, 99,  156, 11,  244, 44,  211, 77,  178, 110, 145, 22,  233, 55,
  200, 88,  167, 121, 134, 0,   8,   16,  24,  33,  41,  49,  57,  66,  74,
  82,  90,  99,  107, 115, 123, 132, 140, 148, 156, 165, 173, 181, 189, 198,
  206, 214, 222, 231, 239, 247, 255, 0,   255, 32,  223, 65,  190, 97,  158,
  6,   249, 39,  216, 71,  184, 104, 151, 13,  242, 45,  210, 78,  177, 110,
  145, 19,  236, 52,  203, 84,  171, 117, 138, 26,  229, 58,  197, 91,  164,
  123, 132, 0,   255, 16,  239, 32,  223, 48,  207, 65,  190, 81,  174, 97,
  158, 113, 142, 5,   250, 21,  234, 38,  217, 54,  201, 70,  185, 86,  169,
  103, 152, 119, 136, 11,  244, 27,  228, 43,  212, 59,  196, 76,  179, 92,
  163, 108, 147, 124, 131, 0,   4,   8,   12,  16,  20,  24,  28,  32,  36,
  40,  44,  48,  52,  56,  60,  65,  69,  73,  77,  81,  85,  89,  93,  97,
  101, 105, 109, 113, 117, 121, 125, 130, 134, 138, 142, 146, 150, 154, 158,
  162, 166, 170, 174, 178, 182, 186, 190, 195, 199, 203, 207, 211, 215, 219,
  223, 227, 231, 235, 239, 243, 247, 251, 255, 0,   255, 16,  239, 32,  223,
  48,  207, 64,  191, 80,  175, 96,  159, 112, 143, 3,   252, 19,  236, 35,
  220, 51,  204, 67,  188, 83,  172, 100, 155, 116, 139, 6,   249, 22,  233,
  38,  217, 54,  201, 71,  184, 87,  168, 103, 152, 119, 136, 9,   246, 25,
  230, 42,  213, 58,  197, 74,  181, 90,  165, 106, 149, 122, 133, 13,  242,
  29,  226, 45,  210, 61,  194, 77,  178, 93,  162, 109, 146, 125, 130, 0,
  255, 8,   247, 16,  239, 24,  231, 32,  223, 40,  215, 48,  207, 56,  199,
  64,  191, 72,  183, 80,  175, 88,  167, 96,  159, 104, 151, 112, 143, 120,
  135, 2,   253, 10,  245, 18,  237, 26,  229, 35,  220, 43,  212, 51,  204,
  59,  196, 67,  188, 75,  180, 83,  172, 91,  164, 99,  156, 107, 148, 115,
  140, 123, 132, 5,   250, 13,  242, 21,  234, 29,  226, 37,  218, 45,  210,
  53,  202, 61,  194, 70,  185, 78,  177, 86,  169, 94,  161, 102, 153, 110,
  145, 118, 137, 126, 129, 0,   2,   4,   6,   8,   10,  12,  14,  16,  18,
  20,  22,  24,  26,  28,  30,  32,  34,  36,  38,  40,  42,  44,  46,  48,
  50,  52,  54,  56,  58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,
  80,  82,  84,  86,  88,  90,  92,  94,  96,  98,  100, 102, 104, 106, 108,
  110, 112, 114, 116, 118, 120, 122, 124, 126, 129, 131, 133, 135, 137, 139,
  141, 143, 145, 147, 149, 151, 153, 155, 157, 159, 161, 163, 165, 167, 169,
  171, 173, 175, 177, 179, 181, 183, 185, 187, 189, 191, 193, 195, 197, 199,
  201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223, 225, 227, 229,
  231, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253, 255, 0,   255,
  8,   247, 16,  239, 24,  231, 32,  223, 40,  215, 48,  207, 56,  199, 64,
  191, 72,  183, 80,  175, 88,  167, 96,  159, 104, 151, 112, 143, 120, 135,
  1,   254, 9,   246, 17,  238, 25,  230, 33,  222, 41,  214, 49,  206, 57,
  198, 65,  190, 73,  182, 81,  174, 89,  166, 97,  158, 105, 150, 113, 142,
  121, 134, 3,   252, 11,  244, 19,  236, 27,  228, 35,  220, 43,  212, 51,
  204, 59,  196, 67,  188, 75,  180, 83,  172, 91,  164, 99,  156, 107, 148,
  115, 140, 123, 132, 4,   251, 12,  243, 20,  235, 28,  227, 36,  219, 44,
  211, 52,  203, 60,  195, 68,  187, 76,  179, 84,  171, 92,  163, 100, 155,
  108, 147, 116, 139, 124, 131, 6,   249, 14,  241, 22,  233, 30,  225, 38,
  217, 46,  209, 54,  201, 62,  193, 70,  185, 78,  177, 86,  169, 94,  161,
  102, 153, 110, 145, 118, 137, 126, 129, 0,   255, 4,   251, 8,   247, 12,
  243, 16,  239, 20,  235, 24,  231, 28,  227, 32,  223, 36,  219, 40,  215,
  44,  211, 48,  207, 52,  203, 56,  199, 60,  195, 64,  191, 68,  187, 72,
  183, 76,  179, 80,  175, 84,  171, 88,  167, 92,  163, 96,  159, 100, 155,
  104, 151, 108, 147, 112, 143, 116, 139, 120, 135, 124, 131, 1,   254, 5,
  250, 9,   246, 13,  242, 17,  238, 21,  234, 25,  230, 29,  226, 33,  222,
  37,  218, 41,  214, 45,  210, 49,  206, 53,  202, 57,  198, 61,  194, 65,
  190, 69,  186, 73,  182, 77,  178, 81,  174, 85,  170, 89,  166, 93,  162,
  97,  158, 101, 154, 105, 150, 109, 146, 113, 142, 117, 138, 121, 134, 125,
  130, 2,   253, 6,   249, 10,  245, 14,  241, 18,  237, 22,  233, 26,  229,
  30,  225, 34,  221, 38,  217, 42,  213, 46,  209, 50,  205, 54,  201, 58,
  197, 62,  193, 66,  189, 70,  185, 74,  181, 78,  177, 82,  173, 86,  169,
  90,  165, 94,  161, 98,  157, 102, 153, 106, 149, 110, 145, 114, 141, 118,
  137, 122, 133, 126, 129, 0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,
  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,
  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,
  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,
  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
  115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129,
  130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
  145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
  160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174,
  175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
  190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204,
  205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
  220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234,
  235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249,
  250, 251, 252, 253, 254, 255,
};

static const int unquant_offsets[21] = { 0,   2,   5,   9,   14,  20,  28,
                                         38,  50,  66,  86,  110, 142, 182,
                                         230, 294, 374, 470, 598, 758, 950 };

uint unquantize(uint quant_mode, uint unscrambled) {
  int offset = unquant_offsets[quant_mode];
  return unquant_lut[offset + unscrambled];
}

bool will_blue_contract(const uint3 ep0, const uint3 ep1, uint quant_ep_mode) {
  // See C.2.13
  let ep_max = ENDPOINT_MAX_VALUES[quant_ep_mode];
  uint eps[6] = { ep0.x, ep1.x, ep0.y, ep1.y, ep0.z, ep1.z };

  uint v[6];
  for (uint i = 0; i < 6; ++i) {
    uint q_ep = uint((float(eps[i]) * float(ep_max)) / 255.0f + 0.5f);
    q_ep = clamp(q_ep, 0u, ep_max);
    var unscrambled = unscramble_quantized(quant_ep_mode, q_ep);
    v[i] = unquantize(quant_ep_mode, unscrambled);
  }

  // Mode 8  LDR RGB, Direct
  //     s0= v0+v2+v4; s1= v1+v3+v5;
  //     if (s1>=s0){e0=(v0,v2,v4,0xFF);
  //                 e1=(v1,v3,v5,0xFF); }
  //     else { e0=blue_contract(v1,v3,v5,0xFF);
  //            e1=blue_contract(v0,v2,v4,0xFF); }
  uint s0 = v[0] + v[2] + v[4];
  uint s1 = v[1] + v[3] + v[5];
  return (s1 < s0);
}

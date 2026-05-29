/* Copyright (c) 2020-2026 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// 147: R8G8B8_UNORM
// 148: R8G8B8_SRGB
// 151: R8G8B8A8_UNORM
// 152: R8G8B8A8_SRGB
struct Registers {
  int2 resolution;
  uint format; // 147, 148, 151, or 152
  uint tile_stride;
};

[vk::push_constant]
Registers registers;

[vk::binding(0, 0)]
RWStructuredBuffer<uint> uOutput;

[vk::binding(1, 0)]
StructuredBuffer<uint2> uInput;

static const int2 etc1_color_modifier_table[8] = {
  int2(2, 8),   int2(5, 17),  int2(9, 29),   int2(13, 42),
  int2(18, 60), int2(24, 80), int2(33, 106), int2(47, 183)
};

static const int4 etc2_alpha_modifier_table[16] = {
  int4(2, 5, 8, 14), int4(2, 6, 9, 12), int4(1, 4, 7, 12), int4(1, 3, 5, 12),
  int4(2, 5, 7, 11), int4(2, 6, 8, 10), int4(3, 6, 7, 10), int4(2, 4, 7, 10),
  int4(1, 5, 7, 9),  int4(1, 4, 7, 9),  int4(1, 3, 7, 9),  int4(1, 4, 6, 9),
  int4(2, 3, 6, 9),  int4(0, 1, 2, 9),  int4(3, 5, 7, 8),  int4(2, 4, 6, 8)
};

static const int etc2_distance_table[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

float linear_to_srgb(float c) {
  c = clamp(c, 0.0f, 1.0f);
  return (c <= 0.0031308f) ? (c * 12.92f)
                           : (1.055f * pow(c, 1.0f / 2.4f) - 0.055f);
}

uint flip_endian(uint v) {
  uint4 words = uint4(v) >> uint4(0, 8, 16, 24);
  words &= 0xffu;
  return (words.x << 24u) | (words.y << 16u) | (words.z << 8u) |
         (words.w << 0u);
}

uint2 flip_endian(uint2 v) { return uint2(flip_endian(v.y), flip_endian(v.x)); }

uint decode_etc2_alpha(uint2 payload, int linear_pixel) {
  int bit_offset = 45 - 3 * linear_pixel;
  int base = int(bitfieldExtract(payload.y, 24, 8));
  int multiplier = int(bitfieldExtract(payload.y, 20, 4));
  int table = int(bitfieldExtract(payload.y, 16, 4));

  int lsb_index =
      int(bitfieldExtract(payload[bit_offset >> 5], bit_offset & 31, 2));
  bit_offset += 2;
  int msb = int((payload[bit_offset >> 5] >> (bit_offset & 31)) & 1);
  int mod = etc2_alpha_modifier_table[table][lsb_index] ^ (msb - 1);
  int a = base + mod * multiplier;
  return clamp(a, 0, 0xff);
}

[numthreads(8, 8, 1)]
[shader("compute")]
void main(uint3 gl_WorkGroupID: SV_GroupID,
          uint3 gl_LocalInvocationID: SV_GroupThreadID) {
  int2 coord = gl_WorkGroupID.xy * 8 + gl_LocalInvocationID.xy;

  if (any(coord >= registers.resolution))
    return;

  int ALPHA_BITS = (registers.format == 151 || registers.format == 152) ? 8 : 0;
  bool is_srgb = (registers.format == 148 || registers.format == 152);

  var tile_coord = coord >> 2;
  var pixel_coord = coord & 3;
  var linear_pixel = 4 * pixel_coord.x + pixel_coord.y;
  uint tile_index = tile_coord.y * registers.tile_stride + tile_coord.x;

  uint2 color_payload;
  uint alpha_result;
  bool punchthrough;

  if (ALPHA_BITS == 8) {
    uint2 alpha_payload = flip_endian(uInput[tile_index * 2]);
    alpha_result = decode_etc2_alpha(alpha_payload, linear_pixel);
    color_payload = flip_endian(uInput[tile_index * 2 + 1]);
  } else {
    color_payload = flip_endian(uInput[tile_index]);
    alpha_result = 0xffu;
  }

  uint3 rgb_result;
  int3 base_rgb;
  uint flip = color_payload.y & 1u;
  uint subblock = (pixel_coord[flip] & 2u) >> 1u;
  bool etc1_compat = false;

  if (ALPHA_BITS == 1)
    punchthrough = (color_payload.y & 2u) == 0u;
  else
    punchthrough = false;

  if (ALPHA_BITS != 1 && (color_payload.y & 2u) == 0u) {
    // Individual mode (ETC1)
    etc1_compat = true;
    base_rgb = int3(color_payload.yyy >> (uint3(28, 20, 12) - 4 * subblock));
    base_rgb &= 0xf;
    base_rgb *= 0x11;
  } else {
    int r = int(bitfieldExtract(color_payload.y, 27, 5));
    int rd = bitfieldExtract(int(color_payload.y), 24, 3);
    int g = int(bitfieldExtract(color_payload.y, 19, 5));
    int gd = bitfieldExtract(int(color_payload.y), 16, 3);
    int b = int(bitfieldExtract(color_payload.y, 11, 5));
    int bd = bitfieldExtract(int(color_payload.y), 8, 3);

    int r1 = r + rd;
    int g1 = g + gd;
    int b1 = b + bd;

    if (uint(r1) > 31) {
      int r1_val = int(bitfieldExtract(color_payload.y, 56 - 32, 2)) |
                   (int(bitfieldExtract(color_payload.y, 59 - 32, 2)) << 2);
      int g1_val = int(bitfieldExtract(color_payload.y, 52 - 32, 4));
      int b1_val = int(bitfieldExtract(color_payload.y, 48 - 32, 4));
      int r2 = int(bitfieldExtract(color_payload.y, 44 - 32, 4));
      int g2 = int(bitfieldExtract(color_payload.y, 40 - 32, 4));
      int b2 = int(bitfieldExtract(color_payload.y, 36 - 32, 4));
      uint da = (bitfieldExtract(color_payload.y, 34 - 32, 2) << 1) |
                (color_payload.y & 1u);
      int dist = etc2_distance_table[da];

      int msb = int((color_payload.x >> (15 + linear_pixel)) & 2u);
      int lsb = int((color_payload.x >> linear_pixel) & 1u);
      int index = msb | lsb;

      if (punchthrough)
        punchthrough = index == 2;

      if (index == 0) {
        rgb_result = uint3(r1_val, g1_val, b1_val);
        rgb_result *= 0x11u;
      } else {
        int mod = 2 - index;
        int3 rgb = int3(r2, g2, b2) * 0x11 + mod * dist;
        rgb_result = uint3(clamp(rgb, int3(0), int3(255)));
      }
    } else if (uint(g1) > 31) {
      int r1_val = int(bitfieldExtract(color_payload.y, 59 - 32, 4));
      int g1_val = (int(bitfieldExtract(color_payload.y, 56 - 32, 3)) << 1) |
                   int((color_payload.y >> 20u) & 1u);
      int b1_val = int(bitfieldExtract(color_payload.y, 47 - 32, 3)) |
                   int((color_payload.y >> 16u) & 8u);
      int r2 = int(bitfieldExtract(color_payload.y, 43 - 32, 4));
      int g2 = int(bitfieldExtract(color_payload.y, 39 - 32, 4));
      int b2 = int(bitfieldExtract(color_payload.y, 35 - 32, 4));
      uint da = color_payload.y & 4u;
      uint db = color_payload.y & 1u;
      uint d = da + 2 * db;
      d += uint((r1_val * 0x10000 + g1_val * 0x100 + b1_val) >=
                (r2 * 0x10000 + g2 * 0x100 + b2));
      int dist = etc2_distance_table[d];
      int msb = int((color_payload.x >> (15 + linear_pixel)) & 2u);
      int lsb = int((color_payload.x >> linear_pixel) & 1u);

      if (punchthrough)
        punchthrough = (msb + lsb) == 2;

      int3 base_v = msb != 0 ? int3(r2, g2, b2) : int3(r1_val, g1_val, b1_val);
      base_v *= 0x11;
      int mod = 1 - 2 * lsb;
      base_v += mod * dist;
      rgb_result = uint3(clamp(base_v, int3(0), int3(0xff)));
    } else if (uint(b1) > 31) {
      // Planar mode
      int r = int(bitfieldExtract(color_payload.y, 57 - 32, 6));
      int g = int(bitfieldExtract(color_payload.y, 49 - 32, 6)) |
              (int(color_payload.y >> 18) & 0x40);
      int b = int(bitfieldExtract(color_payload.y, 39 - 32, 3)) |
              (int(bitfieldExtract(color_payload.y, 43 - 32, 2)) << 3) |
              (int(color_payload.y >> 11) & 0x20);
      int rh = int(color_payload.y & 1u) |
               (int(bitfieldExtract(color_payload.y, 2, 5)) << 1);
      int rv = int(bitfieldExtract(color_payload.x, 13, 6));
      int gh = int(bitfieldExtract(color_payload.x, 25, 7));
      int gv = int(bitfieldExtract(color_payload.x, 6, 7));
      int bh = int(bitfieldExtract(color_payload.x, 19, 6));
      int bv = int(bitfieldExtract(color_payload.x, 0, 6));

      r = (r << 2) | (r >> 4);
      rh = (rh << 2) | (rh >> 4);
      rv = (rv << 2) | (rv >> 4);
      g = (g << 1) | (g >> 6);
      gh = (gh << 1) | (gh >> 6);
      gv = (gv << 1) | (gv >> 6);
      b = (b << 2) | (b >> 4);
      bh = (bh << 2) | (bh >> 4);
      bv = (bv << 2) | (bv >> 4);

      int3 rgb = int3(r, g, b);
      int3 dx = int3(rh, gh, bh) - rgb;
      int3 dy = int3(rv, gv, bv) - rgb;
      dx *= pixel_coord.x;
      dy *= pixel_coord.y;
      rgb = rgb + ((dx + dy + 2) >> 2);
      rgb_result = uint3(clamp(rgb, int3(0), int3(255)));
      punchthrough = false;
    } else {
      // Differential mode (ETC1)
      etc1_compat = true;
      base_rgb = int3(r, g, b) + int(subblock) * int3(rd, gd, bd);
      base_rgb = (base_rgb << 3) | (base_rgb >> 2);
    }
  }

  if (etc1_compat) {
    uint etc1_table_index =
        bitfieldExtract(color_payload.y, 5 - 3 * int(subblock != 0u), 3);
    int msb = int((color_payload.x >> (15 + linear_pixel)) & 2u);
    int lsb = int((color_payload.x >> linear_pixel) & 1u);
    int sgn = 1 - msb;
    if (punchthrough) {
      sgn *= lsb;
      punchthrough = (msb + lsb) == 2;
    }
    int offset = etc1_color_modifier_table[etc1_table_index][lsb] * sgn;
    base_rgb = clamp(base_rgb + offset, int3(0), int3(255));
    rgb_result = uint3(base_rgb);
  }

  if (ALPHA_BITS == 1 && punchthrough) {
    rgb_result = uint3(0);
    alpha_result = 0;
  }

  if (is_srgb) {
    rgb_result.r = uint(linear_to_srgb(float(rgb_result.r) / 255.0f) * 255.0f);
    rgb_result.g = uint(linear_to_srgb(float(rgb_result.g) / 255.0f) * 255.0f);
    rgb_result.b = uint(linear_to_srgb(float(rgb_result.b) / 255.0f) * 255.0f);
  }

  uint output_idx = coord.y * registers.resolution.x + coord.x;
  uOutput[output_idx] = pack_u8(uint4(rgb_result, alpha_result));
}

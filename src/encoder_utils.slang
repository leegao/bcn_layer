
#ifdef ENABLE_DIAGNOSTICS
// Not available on Mali/Adreno :(
__glsl_extension(GL_EXT_shader_realtime_clock) uint2 clockRealtime2x32EXT() {
  __target_switch {
  case spirv:
    return spirv_asm {
            OpCapability ShaderClockKHR;
            OpExtension "SPV_KHR_shader_clock";
            result : $$uint2 = OpReadClockKHR Device;
        };

  case glsl:
    __intrinsic_asm "clockRealtime2x32EXT()";

  default:
    return 0;
  }
}
#endif

uint quantize_u8(uint value, uint range) {
  uint quantized = uint((float(value) * float(range)) / 255.0f + 0.5f);
  quantized = clamp(quantized, 0u, range);
  // var quantized = round(float(value) / 255.0f * range);
  return uint(round(quantized / float(range) * 255));
}

void quantize_u8<E : __BuiltinIntegerType, let M : int>(
    inout vector<E, M> value, uint range) {
  for (int i = 0; i < M; i++) {
    var val = E(value[i]).toUInt();
    value[i] = E(quantize_u8(val, range));
  }
}

void quantize_u8<E : __BuiltinIntegerType, let M : int>(inout Array<E, M> value,
                                                        uint range) {
  for (int i = 0; i < M; i++) {
    var val = E(value[i]).toUInt();
    value[i] = E(quantize_u8(val, range));
  }
}

struct PackedArray<let M : uint, let N : uint> {
  static const uint kElementsPerStorage = 32 / M;
  static const uint kStorageCount =
      (N + kElementsPerStorage - 1u) / kElementsPerStorage;
  static const uint kMask = (1u << M) - 1u;

  uint data[kStorageCount];

  __init(uint initialValues[N]) {
    this.data = {};
    for (int n = 0; n < N; n++) {
      this[n] = initialValues[n];
    }
  }

  inline __subscript(uint n)->uint {
    get {
      var arrayIndex = n / kElementsPerStorage;
      var bitOffset = (n % kElementsPerStorage) * M;

      return (data[arrayIndex] >> bitOffset) & kMask;
    }
    set {
      var arrayIndex = n / kElementsPerStorage;
      var bitOffset = (n % kElementsPerStorage) * M;
      var clearMask = ~(kMask << bitOffset);

      data[arrayIndex] =
          (data[arrayIndex] & clearMask) | ((newValue & kMask) << bitOffset);
    }
  }
};

static const uint M5 = 0x55555555; // 0101...
static const uint MA = 0xAAAAAAAA; // 1010...

uint permute_swap(uint x) { return x ^ (~(x >> 1) & M5); }

uint hamming_distance_2b(uint x, uint y) {
  uint z = x ^ y;
  z |= z >> 1;
  z &= M5;
  return countbits(z);
}

uint count_diffs(uint val) {
  return countbits((val | (val >> 1)) & 0x55555555u);
}

uint lut2_key(uint x) {
  // pack into bits, preserve proper endianness
  uint result = 0;
  [unroll]
  for (int i = 15; i >= 0; i--) {
    result = result * 2 + ((x >> (i * 2)) & 3);
  }
  return result;
}

uint canonicalize_lut2(uint x) {
  var p0 = lut2_key(x);
  var p1 = lut2_key(permute_swap(x));
  return min(p0, p1);
}

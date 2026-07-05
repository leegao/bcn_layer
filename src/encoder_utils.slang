
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

uint sfloat16_to_uint8(uint val) {
  return clamp(uint(round(f16tof32(uint(val) & 0xFFFFu) * 255.0)), 0u, 255u);
}

void unpack_snorm_to_unorm_float(uint packed_val, out uint r, out uint g,
                                 out uint b, out uint a) {
  uint4 raw_bytes =
      uint4((packed_val >> 0) & 0xFFu, (packed_val >> 8) & 0xFFu,
            (packed_val >> 16) & 0xFFu, (packed_val >> 24) & 0xFFu);
  int4 signed_bytes = int4(raw_bytes << 24) >> 24;
  float4 snorm_float = max(float4(-1.0), float4(signed_bytes) / 127.0);

  float4 unorm_float = (snorm_float * 0.5) + 0.5;
  r = uint(unorm_float.x * 255.0 + 0.5);
  g = uint(unorm_float.y * 255.0 + 0.5);
  b = uint(unorm_float.z * 255.0 + 0.5);
  a = uint(unorm_float.w * 255.0 + 0.5);
}

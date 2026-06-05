#include "bcn.hpp"
#include "buffer.hpp"
#include "command_buffer.hpp"
#include "image.hpp"
#include "etc2_spv.h"
#include "astc_decoder_spv.h"

VkFormat get_format_for_etc2(VkFormat format) {
    return VK_FORMAT_R8G8B8A8_UNORM; // Consider using RGB8 instead
}

bool is_supported_etc2_format(struct device *device, VkFormat format) {
   	switch(format) {
	    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
  		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
			return true;
		default:
			return false;
	}
}

VkFormat get_format_for_astc(VkFormat format) {
    return VK_FORMAT_R8G8B8A8_UNORM;
}

bool is_supported_astc_format(struct device *device, VkFormat format) {
    switch(format) {
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        case VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK:
            return true;
        default:
            return false;
    }
}

static VkResult
create_new_pool(struct device *device) {
	VkResult result;
	VkLayerDispatchTable table = device->table;

	VkDescriptorPoolSize desc_sizes[] =
	{
	    {
	    	.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	    	.descriptorCount = 2
	    }
	};

	VkDescriptorPoolCreateInfo descpool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .pNext = nullptr,
	    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
	    .maxSets = 32,
	    .poolSizeCount = 1,
	    .pPoolSizes = desc_sizes
	};

	VkDescriptorPool descriptorPool;
	result = table.CreateDescriptorPool(device->handle,
		&descpool_info, NULL, &descriptorPool);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create descriptor pool, res %d", result);
		return result;
	}

	device->pools.push_back(descriptorPool);

	return VK_SUCCESS;
}


struct ASTCQuantizationMode
{
	uint8_t bits, trits, quints;
};

static void build_astc_unquant_weight_lut(uint8_t *lut, size_t range, const ASTCQuantizationMode &mode)
{
	for (size_t i = 0; i < range; i++)
	{
		auto &v = lut[i];

		if (!mode.quints && !mode.trits)
		{
			switch (mode.bits)
			{
			case 1:
				v = i * 63;
				break;

			case 2:
				v = i * 0x15;
				break;

			case 3:
				v = i * 9;
				break;

			case 4:
				v = (i << 2) | (i >> 2);
				break;

			case 5:
				v = (i << 1) | (i >> 4);
				break;

			default:
				v = 0;
				break;
			}
		}
		else if (mode.bits == 0)
		{
			if (mode.trits)
				v = 32 * i;
			else
				v = 16 * i;
		}
		else
		{
			unsigned b = (i >> 1) & 1;
			unsigned c = (i >> 2) & 1;
			unsigned A, B, C, D;

			A = 0x7f * (i & 1);
			D = i >> mode.bits;
			B = 0;

			if (mode.trits)
			{
				static const unsigned Cs[3] = { 50, 23, 11 };
				C = Cs[mode.bits - 1];
				if (mode.bits == 2)
					B = 0x45 * b;
				else if (mode.bits == 3)
					B = 0x21 * b + 0x42 * c;
			}
			else
			{
				static const unsigned Cs[2] = { 28, 13 };
				C = Cs[mode.bits - 1];
				if (mode.bits == 2)
					B = 0x42 * b;
			}

			unsigned unq = D * C + B;
			unq ^= A;
			unq = (A & 0x20) | (unq >> 2);
			v = unq;
		}

		// Expand [0, 63] to [0, 64].
		if (mode.bits != 0 && v > 32)
			v++;
	}
}

static void build_astc_unquant_endpoint_lut(uint8_t *lut, size_t range, const ASTCQuantizationMode &mode)
{
	for (size_t i = 0; i < range; i++)
	{
		auto &v = lut[i];

		if (!mode.quints && !mode.trits)
		{
			// Bit-replication.
			switch (mode.bits)
			{
			case 1:
				v = i * 0xff;
				break;

			case 2:
				v = i * 0x55;
				break;

			case 3:
				v = (i << 5) | (i << 2) | (i >> 1);
				break;

			case 4:
				v = i * 0x11;
				break;

			case 5:
				v = (i << 3) | (i >> 2);
				break;

			case 6:
				v = (i << 2) | (i >> 4);
				break;

			case 7:
				v = (i << 1) | (i >> 6);
				break;

			default:
				v = i;
				break;
			}
		}
		else
		{
			unsigned A, B, C, D;
			unsigned b = (i >> 1) & 1;
			unsigned c = (i >> 2) & 1;
			unsigned d = (i >> 3) & 1;
			unsigned e = (i >> 4) & 1;
			unsigned f = (i >> 5) & 1;

			B = 0;
			D = i >> mode.bits;
			A = (i & 1) * 0x1ff;

			if (mode.trits)
			{
				static const unsigned Cs[6] = { 204, 93, 44, 22, 11, 5 };
				C = Cs[mode.bits - 1];

				switch (mode.bits)
				{
				case 2:
					B = b * 0x116;
					break;

				case 3:
					B = b * 0x85 + c * 0x10a;
					break;

				case 4:
					B = b * 0x41 + c * 0x82 + d * 0x104;
					break;

				case 5:
					B = b * 0x20 + c * 0x40 + d * 0x81 + e * 0x102;
					break;

				case 6:
					B = b * 0x10 + c * 0x20 + d * 0x40 + e * 0x80 + f * 0x101;
					break;
				}
			}
			else
			{
				static const unsigned Cs[5] = { 113, 54, 26, 13, 6 };
				C = Cs[mode.bits - 1];

				switch (mode.bits)
				{
				case 2:
					B = b * 0x10c;
					break;

				case 3:
					B = b * 0x82 + c * 0x105;
					break;

				case 4:
					B = b * 0x40 + c * 0x81 + d * 0x102;
					break;

				case 5:
					B = b * 0x20 + c * 0x40 + d * 0x80 + e * 0x101;
					break;
				}
			}

			unsigned unq = D * C + B;
			unq ^= A;
			unq = (A & 0x80) | (unq >> 2);
			v = uint8_t(unq);
		}
	}
}

static unsigned astc_value_range(const ASTCQuantizationMode &mode)
{
	unsigned value_range = 1u << mode.bits;
	if (mode.trits)
		value_range *= 3;
	if (mode.quints)
		value_range *= 5;

	if (value_range == 1)
		value_range = 0;
	return value_range;
}

// In order to decode color endpoints, we need to convert available bits and number of values
// into a format of (bits, trits, quints). A simple LUT texture is a reasonable approach for this.
// Decoders are expected to have some form of LUT to deal with this ...
static const ASTCQuantizationMode astc_quantization_modes[] = {
	{ 8, 0, 0 },
	{ 6, 1, 0 },
	{ 5, 0, 1 },
	{ 7, 0, 0 },
	{ 5, 1, 0 },
	{ 4, 0, 1 },
	{ 6, 0, 0 },
	{ 4, 1, 0 },
	{ 3, 0, 1 },
	{ 5, 0, 0 },
	{ 3, 1, 0 },
	{ 2, 0, 1 },
	{ 4, 0, 0 },
	{ 2, 1, 0 },
	{ 1, 0, 1 },
	{ 3, 0, 0 },
	{ 1, 1, 0 },
};

constexpr size_t astc_num_quantization_modes = sizeof(astc_quantization_modes) / sizeof(astc_quantization_modes[0]);

static const ASTCQuantizationMode astc_weight_modes[] = {
	{ 0, 0, 0 }, // Invalid
	{ 0, 0, 0 }, // Invalid
	{ 1, 0, 0 },
	{ 0, 1, 0 },
	{ 2, 0, 0 },
	{ 0, 0, 1 },
	{ 1, 1, 0 },
	{ 3, 0, 0 },
	{ 0, 0, 0 }, // Invalid
	{ 0, 0, 0 }, // Invalid
	{ 1, 0, 1 },
	{ 2, 1, 0 },
	{ 4, 0, 0 },
	{ 2, 0, 1 },
	{ 3, 1, 0 },
	{ 5, 0, 0 },
};

constexpr size_t astc_num_weight_modes = sizeof(astc_weight_modes) / sizeof(astc_weight_modes[0]);

struct ASTCLutHolder
{
	ASTCLutHolder();

	void init_color_endpoint();
	void init_weight_luts();
	void init_trits_quints();

	struct
	{
		size_t unquant_offset = 0;
		uint8_t unquant_lut[2048];
		uint16_t lut[9][128][4];
		size_t unquant_lut_offsets[astc_num_quantization_modes];
	} color_endpoint;

	struct
	{
		size_t unquant_offset = 0;
		uint8_t unquant_lut[2048];
		uint8_t lut[astc_num_weight_modes][4];
	} weights;

	struct
	{
		uint16_t trits_quints[256 + 128];
	} integer;

	struct PartitionTable
	{
		PartitionTable() = default;
		PartitionTable(unsigned width, unsigned height);
		std::vector<uint8_t> lut_buffer;
		unsigned lut_width = 0;
		unsigned lut_height = 0;
	};

	std::mutex table_lock;
	std::unordered_map<unsigned, PartitionTable> tables;

	PartitionTable &get_partition_table(unsigned width, unsigned height);
};

static uint32_t astc_hash52(uint32_t p)
{
	p ^= p >> 15; p -= p << 17; p += p << 7; p += p << 4;
	p ^= p >>  5; p += p << 16; p ^= p >> 7; p ^= p >> 3;
	p ^= p <<  6; p ^= p >> 17;
	return p;
}

// Copy-paste from spec.
static int astc_select_partition(int seed, int x, int y, int z, int partitioncount, bool small_block)
{
	if (small_block)
	{
		x <<= 1;
		y <<= 1;
		z <<= 1;
	}

	seed += (partitioncount - 1) * 1024;
	uint32_t rnum = astc_hash52(seed);
	uint8_t seed1 = rnum & 0xF;
	uint8_t seed2 = (rnum >> 4) & 0xF;
	uint8_t seed3 = (rnum >> 8) & 0xF;
	uint8_t seed4 = (rnum >> 12) & 0xF;
	uint8_t seed5 = (rnum >> 16) & 0xF;
	uint8_t seed6 = (rnum >> 20) & 0xF;
	uint8_t seed7 = (rnum >> 24) & 0xF;
	uint8_t seed8 = (rnum >> 28) & 0xF;
	uint8_t seed9 = (rnum >> 18) & 0xF;
	uint8_t seed10 = (rnum >> 22) & 0xF;
	uint8_t seed11 = (rnum >> 26) & 0xF;
	uint8_t seed12 = ((rnum >> 30) | (rnum << 2)) & 0xF;

	seed1 *= seed1; seed2 *= seed2; seed3 *= seed3; seed4 *= seed4;
	seed5 *= seed5; seed6 *= seed6; seed7 *= seed7; seed8 *= seed8;
	seed9 *= seed9; seed10 *= seed10; seed11 *= seed11; seed12 *= seed12;

	int sh1, sh2, sh3;
	if (seed & 1)
	{
		sh1 = seed & 2 ? 4 : 5;
		sh2 = partitioncount == 3 ? 6 : 5;
	}
	else
	{
		sh1 = partitioncount == 3 ? 6 : 5;
		sh2 = seed & 2 ? 4 : 5;
	}
	sh3 = (seed & 0x10) ? sh1 : sh2;

	seed1 >>= sh1; seed2 >>= sh2; seed3 >>= sh1; seed4 >>= sh2;
	seed5 >>= sh1; seed6 >>= sh2; seed7 >>= sh1; seed8 >>= sh2;
	seed9 >>= sh3; seed10 >>= sh3; seed11 >>= sh3; seed12 >>= sh3;

	int a = seed1 * x + seed2 * y + seed11 * z + (rnum >> 14);
	int b = seed3 * x + seed4 * y + seed12 * z + (rnum >> 10);
	int c = seed5 * x + seed6 * y + seed9 * z + (rnum >> 6);
	int d = seed7 * x + seed8 * y + seed10 * z + (rnum >> 2);

	a &= 0x3f; b &= 0x3f; c &= 0x3f; d &= 0x3f;

	if (partitioncount < 4)
		d = 0;
	if (partitioncount < 3)
		c = 0;

	if (a >= b && a >= c && a >= d)
		return 0;
	else if (b >= c && b >= d)
		return 1;
	else if (c >= d)
		return 2;
	else
		return 3;
}

ASTCLutHolder::PartitionTable::PartitionTable(unsigned block_width, unsigned block_height)
{
	bool small_block = (block_width * block_height) < 31;

	lut_width = block_width * 32;
	lut_height = block_height * 32;
	lut_buffer.resize(lut_width * lut_height);

	for (unsigned seed_y = 0; seed_y < 32; seed_y++)
	{
		for (unsigned seed_x = 0; seed_x < 32; seed_x++)
		{
			unsigned seed = seed_y * 32 + seed_x;
			for (unsigned block_y = 0; block_y < block_height; block_y++)
			{
				for (unsigned block_x = 0; block_x < block_width; block_x++)
				{
					int part2 = astc_select_partition(seed, block_x, block_y, 0, 2, small_block);
					int part3 = astc_select_partition(seed, block_x, block_y, 0, 3, small_block);
					int part4 = astc_select_partition(seed, block_x, block_y, 0, 4, small_block);
					lut_buffer[(seed_y * block_height + block_y) * lut_width + (seed_x * block_width + block_x)] =
							(part2 << 0) | (part3 << 2) | (part4 << 4);
				}
			}
		}
	}
}

ASTCLutHolder::PartitionTable &ASTCLutHolder::get_partition_table(unsigned width, unsigned height)
{
	std::lock_guard<std::mutex> holder{table_lock};
	auto itr = tables.find(width * 16 + height);
	if (itr != tables.end())
	{
		return itr->second;
	}
	else
	{
		auto &t = tables[width * 16 + height];
		t = { width, height };
		return t;
	}
}

static ASTCLutHolder &get_astc_luts()
{
	static ASTCLutHolder holder;
	return holder;
}

ASTCLutHolder::ASTCLutHolder()
{
	init_color_endpoint();
	init_weight_luts();
	init_trits_quints();
}

void ASTCLutHolder::init_color_endpoint()
{
	auto &unquant_lut = color_endpoint.unquant_lut;

	for (size_t i = 0; i < astc_num_quantization_modes; i++)
	{
		auto value_range = astc_value_range(astc_quantization_modes[i]);
		color_endpoint.unquant_lut_offsets[i] = color_endpoint.unquant_offset;
		build_astc_unquant_endpoint_lut(unquant_lut + color_endpoint.unquant_offset, value_range, astc_quantization_modes[i]);
		color_endpoint.unquant_offset += value_range;
	}

	auto &lut = color_endpoint.lut;

	// We can have a maximum of 9 endpoint pairs, i.e. 18 endpoint values in total.
	for (unsigned pairs_minus_1 = 0; pairs_minus_1 < 9; pairs_minus_1++)
	{
		for (unsigned remaining = 0; remaining < 128; remaining++)
		{
			bool found_mode = false;
			for (auto &mode : astc_quantization_modes)
			{
				unsigned num_values = (pairs_minus_1 + 1) * 2;
				unsigned total_bits = mode.bits * num_values +
				                      (mode.quints * 7 * num_values + 2) / 3 +
				                      (mode.trits * 8 * num_values + 4) / 5;

				if (total_bits <= remaining)
				{
					found_mode = true;
					lut[pairs_minus_1][remaining][0] = mode.bits;
					lut[pairs_minus_1][remaining][1] = mode.trits;
					lut[pairs_minus_1][remaining][2] = mode.quints;
					lut[pairs_minus_1][remaining][3] = color_endpoint.unquant_lut_offsets[&mode - astc_quantization_modes];
					break;
				}
			}

			if (!found_mode)
				memset(lut[pairs_minus_1][remaining], 0, sizeof(lut[pairs_minus_1][remaining]));
		}
	}
}

void ASTCLutHolder::init_weight_luts()
{
	auto &lut = weights.lut;
	auto &unquant_lut = weights.unquant_lut;
	auto &unquant_offset = weights.unquant_offset;

	for (size_t i = 0; i < astc_num_weight_modes; i++)
	{
		auto value_range = astc_value_range(astc_weight_modes[i]);
		lut[i][0] = astc_weight_modes[i].bits;
		lut[i][1] = astc_weight_modes[i].trits;
		lut[i][2] = astc_weight_modes[i].quints;
		lut[i][3] = unquant_offset;
		build_astc_unquant_weight_lut(unquant_lut + unquant_offset, value_range, astc_weight_modes[i]);
		unquant_offset += value_range;
	}
}

void ASTCLutHolder::init_trits_quints()
{
	// From specification.
	auto &trits_quints = integer.trits_quints;

	for (unsigned T = 0; T < 256; T++)
	{
		unsigned C;
		uint8_t t0, t1, t2, t3, t4;

		if (((T >> 2) & 7) == 7)
		{
			C = (((T >> 5) & 7) << 2) | (T & 3);
			t4 = t3 = 2;
		}
		else
		{
			C = T & 0x1f;
			if (((T >> 5) & 3) == 3)
			{
				t4 = 2;
				t3 = (T >> 7) & 1;
			}
			else
			{
				t4 = (T >> 7) & 1;
				t3 = (T >> 5) & 3;
			}
		}

		if ((C & 3) == 3)
		{
			t2 = 2;
			t1 = (C >> 4) & 1;
			t0 = (((C >> 3) & 1) << 1) | (((C >> 2) & 1) & ~(((C >> 3) & 1)));
		}
		else if (((C >> 2) & 3) == 3)
		{
			t2 = 2;
			t1 = 2;
			t0 = C & 3;
		}
		else
		{
			t2 = (C >> 4) & 1;
			t1 = (C >> 2) & 3;
			t0 = (((C >> 1) & 1) << 1) | ((C & 1) & ~(((C >> 1) & 1)));
		}

		trits_quints[T] = t0 | (t1 << 3) | (t2 << 6) | (t3 << 9) | (t4 << 12);
	}

	for (unsigned Q = 0; Q < 128; Q++)
	{
		unsigned C;
		uint8_t q0, q1, q2;
		if (((Q >> 1) & 3) == 3 && ((Q >> 5) & 3) == 0)
		{
			q2 = ((Q & 1) << 2) | ((((Q >> 4) & 1) & ~(Q & 1)) << 1) | (((Q >> 3) & 1) & ~(Q & 1));
			q1 = q0 = 4;
		}
		else
		{
			if (((Q >> 1) & 3) == 3)
			{
				q2 = 4;
				C = (((Q >> 3) & 3) << 3) | ((~(Q >> 5) & 3) << 1) | (Q & 1);
			}
			else
			{
				q2 = (Q >> 5) & 3;
				C = Q & 0x1f;
			}

			if ((C & 7) == 5)
			{
				q1 = 4;
				q0 = (C >> 3) & 3;
			}
			else
			{
				q1 = (C >> 3) & 3;
				q0 = C & 7;
			}
		}

		trits_quints[256 + Q] = q0 | (q1 << 3) | (q2 << 6);
	}
}

static RawBuffer create_and_upload_ssbo(struct device *dev, VkDeviceSize size, const void *data)
{
    VkLayerDispatchTable table = dev->table;
    VkDevice device = dev->handle;
    RawBuffer out = { VK_NULL_HANDLE, VK_NULL_HANDLE, size };

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    table.CreateBuffer(device, &buffer_info, nullptr, &out.handle);

    VkMemoryRequirements mem_reqs;
    table.GetBufferMemoryRequirements(device, out.handle, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = dev->memoryIndex,
    };
    table.AllocateMemory(device, &alloc_info, nullptr, &out.memory);
    table.BindBufferMemory(device, out.handle, out.memory, 0);

    if (data) {
        void* mapped;
        table.MapMemory(device, out.memory, 0, size, 0, &mapped);
        memcpy(mapped, data, size);
        table.UnmapMemory(device, out.memory);
    }
    return out;
}

VkResult
create_etc2_compute_pipelines(struct device *dev)
{
	VkResult result;
	VkLayerDispatchTable table = dev->table;
	VkDevice device = dev->handle;

	VkShaderModule etc2ShaderModule;
	VkShaderModuleCreateInfo etc2_shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = etc2_spv_len,
		.pCode = (const uint32_t *)etc2_spv,
	};

	table.CreateShaderModule(device, &etc2_shader_info, nullptr, &etc2ShaderModule);

	VkPipelineShaderStageCreateInfo shader_stage_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = etc2ShaderModule,
			.pName = "main",
		},
	};

	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		}
	};

	VkDescriptorSetLayoutCreateInfo descriptor_set_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = bindings
	};

	result = table.CreateDescriptorSetLayout(device,
		&descriptor_set_create_info, NULL, &dev->setLayout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create descriptor set layout, res %d", result);
		return result;
	}

	VkPushConstantRange push_constant = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(struct push_constants)
	};

	VkPipelineLayoutCreateInfo layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &dev->setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant
	};

	result = table.CreatePipelineLayout(device,
		&layout_create_info, NULL, &dev->layout);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create pipeline layout");
		return result;
	}

	VkComputePipelineCreateInfo pipeline_create_info[] = {
        {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = shader_stage_infos[0],
            .layout = dev->layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
        },
	};

	VkPipeline pipelines[1];

	result = table.CreateComputePipelines(device,
		VK_NULL_HANDLE, 1, pipeline_create_info, NULL, pipelines);

	if (result != VK_SUCCESS) {
		Logger::log("error", "Failed to create compute pipeline, res %d", result);
		return result;
	}

	dev->etc2Pipeline = pipelines[0];

	table.DestroyShaderModule(device, etc2ShaderModule, nullptr);

	result = create_new_pool(dev);

	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to create descriptor pool, res %d", result);
		return result;
	}

	return VK_SUCCESS;
}

VkResult create_astc_compute_pipelines(struct device *dev)
{
    Logger::log("info", "create_astc_compute_pipelines");
    VkResult result;
    VkLayerDispatchTable table = dev->table;
    VkDevice device = dev->handle;

    VkShaderModule astcShaderModule;
    VkShaderModuleCreateInfo astc_shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = astc_decoder_spv_len,
        .pCode = (const uint32_t *)astc_decoder_spv,
    };
    result = table.CreateShaderModule(device, &astc_shader_info, nullptr, &astcShaderModule);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to create ASTC shader module, res %d", result);
        return result;
    }

    VkDescriptorSetLayoutBinding astc_lut_bindings[6];
    for (uint32_t i = 0; i < 6; ++i) {
        astc_lut_bindings[i] = { .binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    }
    VkDescriptorSetLayoutCreateInfo astc_lut_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 6,
        .pBindings = astc_lut_bindings
    };
    result = table.CreateDescriptorSetLayout(device, &astc_lut_layout_info, NULL, &dev->astcLutSetLayout);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to create ASTC LUT descriptor set layout, res %d", result);
        return result;
    }

    // Shared Set 0 + ASTC Specific Layout Set 1
    VkPushConstantRange push_constant = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(struct astc_push_constants)
    };
    VkDescriptorSetLayout astc_layouts[] = {
        dev->setLayout,
        dev->astcLutSetLayout
    };

    VkPipelineLayoutCreateInfo astc_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = astc_layouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant
    };
    result = table.CreatePipelineLayout(device, &astc_layout_create_info, NULL, &dev->astcLayout);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to create ASTC pipeline layout, res %d", result);
        return result;
    }

    auto &luts = get_astc_luts();
    auto &table_4x4 = luts.get_partition_table(4, 4);

    Logger::log("info", "create_astc_compute_pipelines2");
    dev->astcLutBuffers[0] = create_and_upload_ssbo(dev, sizeof(luts.color_endpoint.lut), luts.color_endpoint.lut);
    dev->astcLutBuffers[1] = create_and_upload_ssbo(dev, luts.color_endpoint.unquant_offset, luts.color_endpoint.unquant_lut);
    dev->astcLutBuffers[2] = create_and_upload_ssbo(dev, sizeof(luts.weights.lut), luts.weights.lut);
    dev->astcLutBuffers[3] = create_and_upload_ssbo(dev, luts.weights.unquant_offset, luts.weights.unquant_lut);
    dev->astcLutBuffers[4] = create_and_upload_ssbo(dev, sizeof(luts.integer.trits_quints), luts.integer.trits_quints);
    dev->astcLutBuffers[5] = create_and_upload_ssbo(dev, table_4x4.lut_buffer.size(), table_4x4.lut_buffer.data());

    VkDescriptorPoolSize static_pool_size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 6 };
    VkDescriptorPoolCreateInfo static_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &static_pool_size
    };
    result = table.CreateDescriptorPool(device, &static_pool_info, nullptr, &dev->astcStaticPool);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to create ASTC static descriptor pool, res %d", result);
        return result;
    }

    VkDescriptorSetAllocateInfo static_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dev->astcStaticPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dev->astcLutSetLayout
    };
    result = table.AllocateDescriptorSets(device, &static_alloc_info, &dev->astcLutSet);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to allocate ASTC LUT descriptor set, res %d", result);
        return result;
    }

    VkDescriptorBufferInfo lut_buffer_infos[6];
    VkWriteDescriptorSet lut_writes[6];
    for (uint32_t i = 0; i < 6; ++i) {
        lut_buffer_infos[i] = { .buffer = dev->astcLutBuffers[i].handle, .offset = 0, .range = VK_WHOLE_SIZE };
        lut_writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dev->astcLutSet,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &lut_buffer_infos[i]
        };
    }
    table.UpdateDescriptorSets(device, 6, lut_writes, 0, nullptr);

    VkPipelineShaderStageCreateInfo shader_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = astcShaderModule,
        .pName = "main",
    };

    VkComputePipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shader_stage_info,
        .layout = dev->astcLayout
    };
    result = table.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &dev->astcPipeline);
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to create ASTC compute pipeline, res %d", result);
        return result;
    }

    table.DestroyShaderModule(device, astcShaderModule, nullptr);
    return result;
}

VkResult
decompress_etc2_compute(struct device *dev,
		       		   VkCommandBuffer commandbuffer,
		       		   VkFormat format,
		       		   VkBufferImageCopy *copy_region,
		       		   struct buffer *srcBuffer,
		       		   struct buffer *stagingBuffer)
{
	VkResult result;
	VkLayerDispatchTable table;
	VkDevice device;

	table = dev->table;
	device = dev->handle;

	struct command_buffer *cb;
	{
	    scoped_lock l(global_lock);
	    cb = get_command_buffer(commandbuffer);
	    if (!cb)
	        return VK_ERROR_NOT_PERMITTED;
	}

	int width = copy_region->imageExtent.width;
	int height = copy_region->imageExtent.height;
	int offset = copy_region->bufferOffset;
	int bufferRowLength = copy_region->bufferRowLength;
	int rowLength = (bufferRowLength == 0) ? width : bufferRowLength;
	int tile_stride = (rowLength + 3) / 4;

	struct push_constants constants = {
		.width = width,
		.height = height,
		.format = format,
		.tile_stride = tile_stride,
	};

	VkDescriptorSet descriptorSet;
	VkDescriptorSetAllocateInfo desc_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = dev->pools.back(),
		.descriptorSetCount = 1,
		.pSetLayouts = &dev->setLayout
	};

	result = table.AllocateDescriptorSets(device,
		&desc_alloc_info, &descriptorSet);

	// TODO: redesign this to reuse descriptors, since we're basically just using up
	// unlimited number of them right now (on many drivers, you'll also get
	// fragmented pools after 3-4 dispatches)
	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
		create_new_pool(dev);

		desc_alloc_info.descriptorPool = dev->pools.back();

		result = table.AllocateDescriptorSets(device,
			&desc_alloc_info, &descriptorSet);
	}

	if (result != VK_SUCCESS) {
	    Logger::log("error", "Failed to allocate descriptor set: %d", result);
		return result;
	}

	VkWriteDescriptorSet desc_writes[2];

	VkDescriptorBufferInfo src_info = {
		.buffer = srcBuffer->handle,
		.offset = static_cast<VkDeviceSize>(offset),
		.range = VK_WHOLE_SIZE
	};

	VkDescriptorBufferInfo dst_info = {
		.buffer = stagingBuffer->handle,
		.offset = 0,
		.range = VK_WHOLE_SIZE
	};

	desc_writes[0] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptorSet,
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &src_info,
	};

	desc_writes[1] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptorSet,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pBufferInfo = &dst_info,
	};

	table.UpdateDescriptorSets(device, 2, desc_writes, 0, NULL);
	table.CmdBindPipeline(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->etc2Pipeline);
	table.CmdPushConstants(commandbuffer,
		dev->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
		sizeof(constants), &constants);
	table.CmdBindDescriptorSets(commandbuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, dev->layout, 0, 1,
		&descriptorSet, 0, nullptr);
	table.CmdDispatch(commandbuffer,
		(width + 7) / 8, (height + 7) / 8, 1);

	return VK_SUCCESS;
}

VkResult decompress_astc_compute(struct device *dev,
                                 VkCommandBuffer commandbuffer,
                                 VkFormat format,
                                 VkBufferImageCopy *copy_region,
                                 struct buffer *srcBuffer,
                                 struct buffer *stagingBuffer)
{
    VkResult result;
    VkLayerDispatchTable table = dev->table;
    VkDevice device = dev->handle;

    struct command_buffer *cb;
    {
        scoped_lock l(global_lock);
        cb = get_command_buffer(commandbuffer);
        if (!cb) return VK_ERROR_NOT_PERMITTED;
    }

    int width = copy_region->imageExtent.width;
    int height = copy_region->imageExtent.height;
    int offset = copy_region->bufferOffset;

    VkDescriptorSet descriptorSet0;
    VkDescriptorSetAllocateInfo desc_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = dev->pools.back(),
        .descriptorSetCount = 1,
        .pSetLayouts = &dev->setLayout // Layout for Set 0
    };

    result = table.AllocateDescriptorSets(device, &desc_alloc_info, &descriptorSet0);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        create_new_pool(dev);
        desc_alloc_info.descriptorPool = dev->pools.back();
        result = table.AllocateDescriptorSets(device, &desc_alloc_info, &descriptorSet0);
    }
    if (result != VK_SUCCESS) {
        Logger::log("error", "Failed to allocate ASTC descriptor set 0: %d", result);
        return result;
    }

    VkDescriptorBufferInfo src_info = { .buffer = srcBuffer->handle, .offset = static_cast<VkDeviceSize>(offset), .range = VK_WHOLE_SIZE };
    VkDescriptorBufferInfo dst_info = { .buffer = stagingBuffer->handle, .offset = 0, .range = VK_WHOLE_SIZE };

    VkWriteDescriptorSet desc_writes[2] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSet0, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &src_info },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = descriptorSet0, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dst_info }
    };
    table.UpdateDescriptorSets(device, 2, desc_writes, 0, NULL);

    struct astc_push_constants constants = {
        .error_color = { 0xff, 0, 0xff, 0xff },
        .width = width,
        .height = height
    };

    table.CmdBindPipeline(commandbuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dev->astcPipeline);
    table.CmdPushConstants(commandbuffer, dev->astcLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    VkDescriptorSet active_descriptor_sets[2] = { descriptorSet0, dev->astcLutSet };
    table.CmdBindDescriptorSets(commandbuffer,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                dev->astcLayout,
                                0,
                                2,
                                active_descriptor_sets,
                                0, nullptr);

    uint32_t blocks_x = (width  + 4  - 1) / 4;
    uint32_t blocks_y = (height + 4 - 1) / 4;
    uint32_t group_count_x = (blocks_x + 1) / 2;
    uint32_t group_count_y = (blocks_y + 1) / 2;
    uint32_t group_count_z = 1;
    table.CmdDispatch(commandbuffer, group_count_x, group_count_y, group_count_z);

    return VK_SUCCESS;
}

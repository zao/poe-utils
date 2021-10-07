#include "lv_bptc.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

size_t lv_bptc_output_size(lv_bptc_format format, int width, int height, void const* src_data, size_t src_size) {
	size_t pixels = (size_t)width * (size_t)height;
	switch (format) {
	case LV_BPTC_FORMAT_BC6H_SF16:
	case LV_BPTC_FORMAT_BC6H_UF16:
		return pixels * 3 * 2;
	case LV_BPTC_FORMAT_BC7_UNORM:
		return pixels * 4;
	}
	return 0;
}

bool lv_bptc_decode_bc6h(bool is_signed, int width, int height, void const* src_data, size_t src_size, void* dst_data, size_t dst_size) {
	return false;
}

static int bc7_mode(uint8_t mode_byte) {
	int mode = 0;
	while ((mode_byte & 1) == 0) {
		mode_byte >>= 1;
		++mode;
	}
	return mode;
}


using BC6PixelBlock = std::array<uint16_t, 4*4*3>;
using BC7PixelBlock = std::array<uint8_t, 4*4*4>;

BC6PixelBlock solid_bc6_block(uint16_t r, uint16_t g, uint16_t b)
{
	BC6PixelBlock block;
	auto* p = block.data();
	for (size_t row = 0; row < 4; ++row) {
		for (size_t col = 0; col < 4; ++col) {
			*p++ = r;
			*p++ = g;
			*p++ = b;
		}
	}
	return block;
}

BC7PixelBlock solid_bc7_block(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	BC7PixelBlock block;
	auto* p = block.data();
	for (size_t row = 0; row < 4; ++row) {
		for (size_t col = 0; col < 4; ++col) {
			*p++ = r;
			*p++ = g;
			*p++ = b;
			*p++ = a;
		}
	}
	return block;
}

static BC6PixelBlock dummy_fill_bc6 = solid_bc6_block(0x2E66u, 0x3266u, 0x34CCu);
static BC7PixelBlock dummy_fill_bc7 = solid_bc7_block(0x19u, 0x33u, 0x4Cu, 0xFFu);

template <typename Block, size_t PixBytes = sizeof(Block) / 4 / 4>
void blit_block_4x4(void* dst, size_t pix_width, size_t pix_height, size_t block_x, size_t block_y, Block block) {
	size_t const PIX_STRIDE = pix_width * PixBytes;
	uint8_t* p = (uint8_t*)dst;
	uint8_t const* pixel = (uint8_t const*)block.data();

	for (size_t row = 0; row < 4; ++row) {
		size_t pix_y = block_y * 4 + row;
		if (pix_y < pix_height) {
			for (size_t col = 0; col < 4; ++col) {
				size_t pix_x = block_x * PixBytes + col;
				if (pix_x < pix_width) {
					uint8_t* q = p + (pix_y * PIX_STRIDE) + (pix_x * PixBytes);
					memcpy(q, pixel, PixBytes);
				}
				pixel += PixBytes;
			}
		}
	}
}

static uint8_t bc7_debug_colors[8][4] = {
	{0x00, 0x00, 0x00, 0xFF},
	{0x22, 0x22, 0x22, 0xFF},
	{0x44, 0x44, 0x44, 0xFF},
	{0x66, 0x66, 0x66, 0xFF},
	{0x88, 0x88, 0x88, 0xFF},
	{0xAA, 0xAA, 0xAA, 0xFF},
	{0xC0, 0xC0, 0xCC, 0xFF},
	{0xFF, 0xFF, 0xFF, 0xFF},
};

struct BitStream {
	BitStream(uint8_t const* byte_data, size_t bit_start, size_t bit_count)
	: data(byte_data), bit_start(bit_start), remaining_bits(bit_count), seen_error(false)
	{
		if (int start_byte = bit_start / 8) {
			data += start_byte;
			bit_start %= 8;
		}
	}

	template <typename P>
	bool read_bits(P& out, size_t bit_count) {
		out = P{};
		static uint8_t const BIT_MASKS[9] = {0, 0b1, 0b11, 0b111, 0b1111, 0b1'1111, 0b11'1111, 0b111'1111, 0b1111'1111};
		if (bit_count > remaining_bits) {
			seen_error = true;
			return false;
		}

		size_t out_off = 0;
		while (bit_count > 0) {
			// Determine bit range to copy from current byte
			size_t remaining_in_byte = (std::min)(remaining_bits, 8 - bit_start);
			size_t copy_width = (std::min)(remaining_in_byte, bit_count);

			// Extract and write the bit range from current byte
			uint8_t bits = (*data >> bit_start) & BIT_MASKS[copy_width];
			out |= (P)(bits) << out_off;
			out_off += copy_width;
			remaining_bits -= copy_width;
			bit_count -= copy_width;

			// Advance bit position in byte, wrap to next byte if needed
			bit_start = (bit_start + copy_width) % 8;
			if (bit_count == 0) {
				++data;
			}
		}
		return true;
	}

	uint8_t const* data;
	size_t bit_start;
	size_t remaining_bits;
	bool seen_error;
};

using BC7Partition = std::array<uint8_t, 16>;

BC7Partition bc7_partition_2[64] = {
	{ 0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1 },
    { 0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1 },
    { 0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1 },
    { 0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1 },
    { 0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1 },
    { 0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1 },
    { 0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1 },
    { 0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1 },
    { 0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1 },
    { 0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1 },
    { 0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1 },
    { 0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1 },
    { 0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1 },
    { 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1 },
    { 0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1 },
    { 0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1 },
    { 0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1 },
    { 0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0 },
    { 0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0 },
    { 0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0 },
    { 0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0 },
    { 0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0 },
    { 0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0 },
    { 0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1 },
    { 0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0 },
    { 0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0 },
    { 0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0 },
    { 0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0 },
    { 0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0 },
    { 0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0 },
    { 0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0 },
    { 0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0 },
    { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 },
    { 0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1 },
    { 0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0 },
    { 0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0 },
    { 0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0 },
    { 0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0 },
    { 0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1 },
    { 0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1 },
    { 0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0 },
    { 0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0 },
    { 0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0 },
    { 0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0 },
    { 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0 },
    { 0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1 },
    { 0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1 },
    { 0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0 },
    { 0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0 },
    { 0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0 },
    { 0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0 },
    { 0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0 },
    { 0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1 },
    { 0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1 },
    { 0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0 },
    { 0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0 },
    { 0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1 },
    { 0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1 },
    { 0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1 },
    { 0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1 },
    { 0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1 },
    { 0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0 },
    { 0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0 },
    { 0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1 },
};

BC7Partition bc7_partition_3[64] = {
	{ 0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2 },
    { 0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1 },
    { 0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1 },
    { 0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1 },
    { 0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2 },
    { 0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2 },
    { 0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1 },
    { 0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1 },
    { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2 },
    { 0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2 },
    { 0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2 },
    { 0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2 },
    { 0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2 },
    { 0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2 },
    { 0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2 },
    { 0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0 },
    { 0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2 },
    { 0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0 },
    { 0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2 },
    { 0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1 },
    { 0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2 },
    { 0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1 },
    { 0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2 },
    { 0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0 },
    { 0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0 },
    { 0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2 },
    { 0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0 },
    { 0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1 },
    { 0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2 },
    { 0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2 },
    { 0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1 },
    { 0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1 },
    { 0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2 },
    { 0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1 },
    { 0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2 },
    { 0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0 },
    { 0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0 },
    { 0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0 },
    { 0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0 },
    { 0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1 },
    { 0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1 },
    { 0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2 },
    { 0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1 },
    { 0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2 },
    { 0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1 },
    { 0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1 },
    { 0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1 },
    { 0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1 },
    { 0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2 },
    { 0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1 },
    { 0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2 },
    { 0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2 },
    { 0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2 },
    { 0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2 },
    { 0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2 },
    { 0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2 },
    { 0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2 },
    { 0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2 },
    { 0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2 },
    { 0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2 },
    { 0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1 },
    { 0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2 },
    { 0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2 },
    { 0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0 },
};

uint8_t bc7_anchor_2_of_2[64] = {
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15, 2, 8, 2, 2, 8, 8,15,
     2, 8, 2, 2, 8, 8, 2, 2,
    15,15, 6, 8, 2, 8,15,15,
     2, 8, 2, 2, 2,15,15, 6,
     6, 2, 6, 8,15,15, 2, 2,
    15,15,15,15,15, 2, 2,15,
};

uint8_t bc7_anchor_2_of_3[64] = {
	 3, 3,15,15, 8, 3,15,15,
     8, 8, 6, 6, 6, 5, 3, 3,
     3, 3, 8,15, 3, 3, 6,10,
     5, 8, 8, 6, 8, 5,15,15,
     8,15, 3, 5, 6,10, 8,15,
    15, 3,15, 5,15,15,15,15,
     3,15, 5, 5, 5, 8, 5,10,
     5,10, 8,13,15,12, 3, 3,
};

uint8_t bc7_anchor_3_of_3[64] = {
    15, 8, 8, 3,15,15, 3, 8,
    15,15,15,15,15,15,15, 8,
    15, 8,15, 3,15, 8,15, 8,
     3,15, 6,10,15,15,10, 8,
    15, 3,15,10,10, 8, 9,10,
     6,15, 8,15, 3, 6, 6, 8,
    15, 3,15,15,15,15,15,15,
    15,15,15,15, 3,15,15, 8,
};

struct BC7Mode {
	int mode;
	int subsets;
	int partition_bits;
	int rotation_bits;
	int index_selection_bits;
	int color_bits;
	int alpha_bits;
	int endpoint_p_bits;
	int shared_p_bits;
	int index_bits_per_element;
	int secondary_index_bits_per_element;
} bc7_modes[] = {
	// Mode NS PB RB ISB CB AB EPB SPB IB IB2
    // ---- -- -- -- --- -- -- --- --- -- ---
    {  0,   3, 4, 0, 0,  4, 0, 1,  0,  3, 0 },
    {  1,   2, 6, 0, 0,  6, 0, 0,  1,  3, 0 },
    {  2,   3, 6, 0, 0,  5, 0, 0,  0,  2, 0 },
    {  3,   2, 6, 0, 0,  7, 0, 1,  0,  2, 0 },
    {  4,   1, 0, 2, 1,  5, 6, 0,  0,  2, 3 },
    {  5,   1, 0, 2, 0,  7, 8, 0,  0,  2, 2 },
    {  6,   1, 0, 0, 0,  7, 7, 1,  0,  4, 0 },
    {  7,   2, 6, 0, 0,  5, 5, 1,  0,  2, 0 },
};

using bc7_alpha_bits = uint8_t;
using bc7_color_bits = std::array<uint8_t, 3>;

struct BC7Fields {
	BC7Fields(BC7Mode params, uint8_t const* block) {
		params = params;
		uint8_t mode_shift = params.mode + 1;
		BitStream bs(block, mode_shift, 128 - mode_shift);
		bs.read_bits(partition, params.partition_bits);
		
		std::array<uint8_t, 16> subset_partition{};
		uint8_t anchors[3]{};
		if (params.subsets == 2) {
			subset_partition = bc7_partition_2[partition];
			anchors[1] = bc7_anchor_2_of_2[partition];
		}
		else if (params.subsets == 3) {
			subset_partition = bc7_partition_3[partition];
			anchors[1] = bc7_anchor_2_of_3[partition];
			anchors[2] = bc7_anchor_3_of_3[partition];
		}

		bs.read_bits(rotation, params.rotation_bits);
		bs.read_bits(index_selection, params.index_selection_bits);

		for (size_t comp = 0; comp < 3; ++comp) {
			for (size_t subset = 0; subset < params.subsets; ++subset) {
				for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
					bs.read_bits(color_bits[subset][endpoint][comp], params.color_bits);
				}
			}
		}

		for (size_t subset = 0; subset < params.subsets; ++subset) {
			for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
				bs.read_bits(alpha_bits[subset][endpoint], params.alpha_bits);
			}
		}

		for (size_t subset = 0; subset < params.subsets; ++subset) {
			if (params.endpoint_p_bits) {
				bs.read_bits(p_bits[subset][0], params.endpoint_p_bits);
				bs.read_bits(p_bits[subset][1], params.endpoint_p_bits);
			}
			if (params.shared_p_bits) {
				bs.read_bits(p_bits[subset][0], params.shared_p_bits);
				p_bits[subset][1] = p_bits[subset][0];
			}
		}

		for (size_t index = 0; index < 16; ++index) {
			uint8_t subset = subset_partition[index];
			uint8_t index_bits = params.index_bits_per_element;
			if (index == anchors[subset]) {
				--index_bits;
			}
			bs.read_bits(primary_indices[index], index_bits);
		}
		if (params.secondary_index_bits_per_element) {
			for (size_t index = 0; index < 16; ++index) {
				uint8_t subset = subset_partition[index];
				uint8_t index_bits = params.secondary_index_bits_per_element;
				if (index == anchors[subset]) {
					--index_bits;
				}
				bs.read_bits(secondary_indices[index], index_bits);
			}
		}
		assert(bs.remaining_bits == 0 && !bs.seen_error);
	}

	uint8_t color_index_width() const {
		return index_selection ? params.secondary_index_bits_per_element : params.index_bits_per_element;
	}

	uint8_t alpha_index_width() const {
		return index_selection ? params.index_bits_per_element : params.secondary_index_bits_per_element;
	}

	uint8_t const* color_indices() const {
		return index_selection ? secondary_indices : primary_indices;
	}

	uint8_t const* alpha_indices() const {
		return index_selection ? primary_indices : secondary_indices;
	} 

	BC7Mode params{};
	uint8_t partition{};
	uint8_t rotation{};
	uint8_t index_selection{};
	bc7_color_bits color_bits[3][2]{};
	bc7_alpha_bits alpha_bits[3][2]{};
	uint8_t p_bits[3][2]{};
	uint8_t primary_indices[16]{};
	uint8_t secondary_indices[16]{};
};

struct BC7Endpoints {
	BC7Endpoints(BC7Mode params, BC7Fields fields) {
		bool has_p_bits = params.endpoint_p_bits || params.shared_p_bits;
		for (size_t subset = 0; subset < params.subsets; ++subset) {
			for (size_t endpoint = 0; endpoint < 2; ++endpoint) {
				uint8_t p = fields.p_bits[subset][endpoint];
				int p_count = has_p_bits ? 1 : 0;
				for (size_t comp = 0; comp < 3; ++comp) {
					uint8_t color = fields.color_bits[subset][endpoint][comp];
					uint8_t c = expand_value(color, params.color_bits, p, p_count);
					colors[subset][endpoint][comp] = c;
				}
				uint8_t a = 0xFF;
				if (params.alpha_bits) {
					uint8_t alpha = fields.alpha_bits[subset][endpoint];
					a = expand_value(alpha, params.alpha_bits, p, p_count);
				}
				alphas[subset][endpoint] = a;
			}
		}
	}

	uint8_t expand_value(uint8_t value, int value_count, uint8_t p, int p_count) {
		uint8_t ret = value << p_count;
		if (p_count) {
			ret |= p;
		}
		int precision = value_count + p_count;
		ret <<= 8 - precision;
		if (precision < 8) {
			ret |= ret >> precision;
		}
		return ret;
	}

	bc7_color_bits colors[3][2]{};
	bc7_alpha_bits alphas[3][2]{};
};

static uint8_t bc7_interpolate(uint8_t e0, uint8_t e1, uint8_t index, uint8_t index_precision) {
	static uint16_t const weight_2[] = { 0, 21, 43, 64 };
	static uint16_t const weight_3[] = { 0, 9, 18, 27, 37, 46, 55, 64 };
	static uint16_t const weight_4[] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
	uint16_t w;
	if (index_precision == 2) {
		w = weight_2[index];
	} else if (index_precision == 3) {
		w = weight_3[index];
	} else {
		w = weight_4[index];
	}
	return (uint8_t)(((64 - w) * (uint16_t)e0 + w * (uint16_t)e1 + 32) >> 6);
}

bool lv_bptc_decode_bc7(int width, int height, void const* src_data, size_t src_size, void* dst_data, size_t dst_size) {
	uint8_t const* base = (uint8_t const*)src_data;
	size_t block_w = (width + 3) / 4;
	size_t block_h = (height + 3) / 4;
	
	for (size_t block_y = 0; block_y < block_h; ++block_y) {
		for (size_t block_x = 0; block_x < block_w; ++block_x) {
			uint8_t const* block = base + 16 * (block_x + block_w * block_y);
			uint8_t mode_byte = block[0];
			if (mode_byte == 0) {
				return false; // reserved and forbidden
			}
			uint8_t mode = bc7_mode(mode_byte);
			BC7Mode params = bc7_modes[mode];

			// Field order:
			//  partition number, rotation, index selection, color, alpha,
			//  per-endpoint p-bit, shared p-bit, primary indices, secondary indices

			BC7Fields fields(params, block);

			BC7Endpoints endpoints(params, fields);

			BC7PixelBlock pixels{};
			BC7Partition partition{};
			if (params.subsets == 2) {
				partition = bc7_partition_2[fields.partition];
			}
			else if (params.subsets == 3) {
				partition = bc7_partition_3[fields.partition];
			}

			uint8_t* p = pixels.data();
			for (size_t idx = 0; idx < 16; ++idx) {
				int subset = partition[idx];
				uint8_t r = 0x40;
				uint8_t g = 0x40;
				uint8_t b = 0x40;
				uint8_t a = 0xFF;

				{
					auto color_index = fields.color_indices()[idx];
					auto color_index_width = fields.color_index_width();
					r = bc7_interpolate(
						endpoints.colors[subset][0][0],
						endpoints.colors[subset][1][0],
						color_index,
						color_index_width);
					g = bc7_interpolate(
						endpoints.colors[subset][0][1],
						endpoints.colors[subset][1][1],
						color_index,
						color_index_width);
					b = bc7_interpolate(
						endpoints.colors[subset][0][2],
						endpoints.colors[subset][1][2],
						color_index,
						color_index_width);
					if (fields.alpha_bits) {
						auto alpha_index = fields.alpha_indices()[idx];
						auto alpha_index_width = fields.alpha_index_width();
						a = bc7_interpolate(
							endpoints.alphas[subset][0],
							endpoints.alphas[subset][1],
							alpha_index,
							alpha_index_width);
					}
					switch (fields.rotation) {
						case 1: std::swap(a, r); break;
						case 2: std::swap(a, g); break;
						case 3: std::swap(a, b); break;
					}
				}

				p[0] = r;
				p[1] = g;
				p[2] = b;
				p[3] = a;
				p += 4;
			}
			blit_block_4x4(dst_data, width, height, block_x, block_y, pixels);
		}
	}
	return false;
}

bool lv_bptc_decode(lv_bptc_format format, int width, int height, void const* src_data, size_t src_size, void* dst_data, size_t dst_size) {
	bool is_signed = false;
	switch (format) {
	case LV_BPTC_FORMAT_BC6H_SF16: is_signed = true;
	case LV_BPTC_FORMAT_BC6H_UF16:
		return lv_bptc_decode_bc6h(is_signed, width, height, src_data, src_size, dst_data, dst_size);
	case LV_BPTC_FORMAT_BC7_UNORM:
		return lv_bptc_decode_bc7(width, height, src_data, src_size, dst_data, dst_size);
	}
	return false;
}
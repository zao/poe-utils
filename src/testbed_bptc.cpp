#include "lv_bptc.h"
#include <gli/texture2d.hpp>
#include <gli/load.hpp>

#include "cmp_core.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 4) {
        return 1;
    }
    std::string srcPath = argv[1];
    std::string dstPath = argv[2];
    std::string refPath = argv[3];

    gli::texture2d srcTex{gli::load(srcPath)};
    int refWidth{}, refHeight{}, refComp{};
    std::shared_ptr<uint8_t[]> refTex((uint8_t *)stbi_load(refPath.c_str(), &refWidth, &refHeight, &refComp, 4), &free);

    auto extent = srcTex.extent();
    if (extent.x != refWidth || extent.y != refHeight) {
        return 1;
    }

    auto srcData = srcTex.data(0, 0, 0);
    auto srcSize = srcTex.size(0);
    std::vector<uint8_t> dstData(4 * extent.x * extent.y);
    bool success =
        lv_bptc_decode(LV_BPTC_FORMAT_BC7_UNORM, extent.x, extent.y, srcData, srcSize, dstData.data(), dstData.size());

    struct BlockCoord {
        int x, y;

        auto operator<=>(BlockCoord const &) const = default;
    };

    int blockW = (extent.x + 3) / 4;
    int blockH = (extent.y + 3) / 4;

    auto *refBegin = (uint8_t const *)refTex.get();
    auto refEnd = refBegin + refWidth * refHeight * 4;

    auto *dstBegin = dstData.data();
    auto dstEnd = dstBegin + dstData.size();

    if (refEnd - refBegin != dstEnd - dstBegin) {
        return 1;
    }

    std::map<BlockCoord, int> differingBlockModes;
    std::map<int, std::set<BlockCoord>> compDiffByMode[4];
    std::map<int, std::set<BlockCoord>> diffByMode;

    auto refPtr = refBegin;
    auto dstPtr = dstBegin;
    while (true) {
        auto [refHit, dstHit] = std::mismatch(refPtr, refEnd, dstPtr, dstEnd);
        if (refHit == refEnd) {
            break;
        }
        auto byteOffset = std::distance(refBegin, refHit);
        auto rowStride = extent.x * 4;
        auto pixelY = byteOffset / rowStride;
        auto pixelX = (byteOffset % rowStride) / 4;
        BlockCoord hit{
            .x = (int)(pixelX / 4),
            .y = (int)(pixelY / 4),
        };
        uint8_t comp = (int)(byteOffset % 4);
        auto mode = lv_bptc_block_mode(srcData, srcSize, hit.x, hit.y, blockW);
        differingBlockModes.insert({hit, mode});
        compDiffByMode[comp][mode].insert(hit);
        diffByMode[mode].insert(hit);
        refPtr = std::next(refHit);
        dstPtr = std::next(dstHit);
    }

#if 0
    if (differingBlockModes.size()) {
        fprintf(stdout, "Differing blocks:\n");
        for (auto [diff, mode] : differingBlockModes) {
            fprintf(stdout, "x: %d, y: %d, mode: %d\n", diff.x, diff.y, mode);
        }
        fprintf(stdout, "---\n");
    } else {
        fprintf(stdout, "No differing blocks.\n");
    }
#endif

    std::map<int, size_t> numBlocksByMode;
    for (size_t blockY = 0; blockY < blockH; ++blockY) {
        for (size_t blockX = 0; blockX < blockW; ++blockX) {
            int mode = lv_bptc_block_mode(srcData, srcSize, blockX, blockY, blockW);
            ++numBlocksByMode[mode];
        }
    }

    char compNames[] = {'R', 'G', 'B', 'A'};
    fprintf(stdout, "Mismatching blocks:\n");
    for (size_t mode = 0; mode < 8; ++mode) {
        auto count = numBlocksByMode[mode];
        auto differing = diffByMode[mode].size();
        fprintf(stdout, "mode %d: pop: %d, any diff: %d", mode, count, differing);
        for (int comp = 0; comp < 4; ++comp) {
            auto diff = compDiffByMode[comp][mode].size();
            fprintf(stdout, ", %c: %d", compNames[comp], diff);
        }
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "---\n");
#if 0
    if (!diffByMode.empty()) {
        fprintf(stdout, "Differing blocks by mode:\n");
        for (auto &[mode, blocks] : diffByMode) {
            fprintf(stdout, "mode: %d, [", mode);
            fprintf(stdout, "%d blocks", blocks.size());
#if 0
            char const* sep = "";
            for (auto block : blocks) {
                fprintf(stdout, "%s{%d, %d}", sep, block.x, block.y);
                sep = ", ";
            }
#endif
            fprintf(stdout, "]\n");
        }
        fprintf(stdout, "---\n");
    } else {
        fprintf(stdout, "No differing blocks by mode.\n");
    }
#endif

    auto print_block_header = [](FILE *fh, uint8_t mode) {
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
            {0, 3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {1, 2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {2, 3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
            {3, 2, 6, 0, 0, 7, 0, 1, 0, 2, 0}, {4, 1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {5, 1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
            {6, 1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {7, 2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
        };
        char buf[16 * 8 + 7 + 1 + 1024]{};
        auto &params = bc7_modes[mode];
        char *p = buf;
        int written = 0;

        auto emit = [&](char ch) {
            if (written && (written % 8) == 0) {
                *p++ = ' ';
            }
            *p++ = ch;
            ++written;
        };

        auto emit_n = [&](char ch, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                emit(ch);
            }
        };

        emit_n('M', mode + 1);
        emit_n('P', params.partition_bits);
        emit_n('R', params.rotation_bits);
        emit_n('I', params.index_selection_bits);
        emit_n('r', params.subsets * 2 * params.color_bits);
        emit_n('g', params.subsets * 2 * params.color_bits);
        emit_n('b', params.subsets * 2 * params.color_bits);
        emit_n('a', params.subsets * 2 * params.alpha_bits);
        emit_n('e', params.subsets * 2 * params.endpoint_p_bits);
        emit_n('s', params.subsets * params.shared_p_bits);
        if (params.index_bits_per_element) {
            emit_n('1', 16 * params.index_bits_per_element - params.subsets);
        }
        if (params.secondary_index_bits_per_element) {
            emit_n('2', 16 * params.secondary_index_bits_per_element - params.subsets);
        }

        // flush out
        fprintf(fh, "%s\n", buf);
    };

    auto print_block_bits = [](FILE *fh, uint8_t const *p) {
        char const *sep = "";
        for (size_t i = 0; i < 16; ++i) {
            fprintf(fh, "%s", sep);
            sep = " ";
            for (size_t j = 0; j < 8; ++j) {
                fprintf(fh, "%d", (p[i] >> j) & 1);
            }
        }
        fprintf(fh, "\n");
    };

    for (auto &coord : diffByMode[7]) {
        uint8_t const *blockData = (uint8_t const *)srcData + 16 * (coord.x + coord.y * blockW);
        print_block_bits(stdout, blockData);
        
        uint8_t their_pixels[16 * 4]{};
        DecompressBlockBC7(blockData, their_pixels);

        uint8_t my_pixels[16 * 4]{};
        lv_bptc_decode_block_bc7(blockData, my_pixels);
        auto print_result = [](FILE* fh, auto pixels) {
            uint8_t const* p = pixels;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    fprintf(fh, "%02X%02X%02X%02X ", p[3], p[0], p[1], p[2]);
                    p += 4;
                }
                fprintf(fh, "\n");
            }
        };
        fprintf(stdout, "CMP:\n");
        print_result(stdout, their_pixels);
        fprintf(stdout, "---\n");
        fprintf(stdout, "LV:\n");
        print_result(stdout, my_pixels);
        break;
    }
    return 0;

    fprintf(stdout, "                                                                                                  "
                    "              1111 11111111 11111111 11111111\n");
    fprintf(stdout, "           111111 11112222 22222233 33333333 44444444 44555555 55556666 66666677 77777777 "
                    "88888888 88999999 99990000 00000011 11111111 22222222\n");
    fprintf(stdout, "01234567 89012345 67890123 45678901 23456789 01234567 89012345 67890123 45678901 23456789 "
                    "01234567 89012345 67890123 45678901 23456789 01234567\n");
    fprintf(stdout, "--------------------------------------------------------------------------------------------------"
                    "---------------------------------------------\n");
    for (int i = 0; i < 8; ++i) {
        //        print_block_header(stdout, i);
    }

    int debug_mode = 7;
    print_block_header(stdout, debug_mode);
    fprintf(stdout, "--------------------------------------------------------------------------------------------------"
                    "---------------------------------------------\n");
    std::optional<int> cap;
    cap = 6;
    std::array<uint8_t, 16> set_all, unset_all;
    std::fill_n(set_all.data(), 16, 0xFFu);
    std::fill_n(unset_all.data(), 16, 0xFFu);
    for (auto &coord : diffByMode[debug_mode]) {
        uint8_t const *blockData = (uint8_t const *)srcData + 16 * (coord.x + coord.y * blockW);
        for (int i = 0; i < 16; ++i) {
            set_all[i] &= blockData[i];
            unset_all[i] &= ~blockData[i];
        }
        print_block_bits(stdout, blockData);
        if (cap && !--*cap) {
            break;
        }
    }
    fprintf(stdout, "--------------------------------------------------------------------------------------------------"
                    "---------------------------------------------\n");
    print_block_bits(stdout, set_all.data());
    print_block_bits(stdout, unset_all.data());
}
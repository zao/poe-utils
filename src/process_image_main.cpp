#include <deque>
#include <iostream>
#include <optional>
#include <gli/gli.hpp>
#include <gli/load.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include <stb_image_write.h>

#include "lv_bptc.h"
#include "cmp_core.h"

struct Rect {
    glm::ivec2 origin;
    glm::ivec2 size;
};

std::string Usage() { return ""; }

static int IntoInt(std::string const &s) {
    std::size_t pos{};
    int ret = std::stoi(s, &pos, 10);
    if (pos != s.size()) {
        throw std::runtime_error("invalid integer");
    }
    return ret;
}

void ConvertCommand(std::deque<std::string> args) {
    Rect crop;
    std::string srcPath, dstPath;

    if (args.size() != 2 && args.size() != 6) {
        throw std::runtime_error("invalid argument count");
    }
    srcPath = args[0];
    dstPath = args[1];

    if (srcPath.size() < 4 || srcPath.substr(srcPath.size() - 4) != ".dds") {
        throw std::runtime_error("input image must be a DDS file");
    }
    if (dstPath.size() < 4 || dstPath.substr(dstPath.size() - 4) != ".png") {
        throw std::runtime_error("output image must be a PNG file");
    }

    gli::texture2d srcTex{gli::load(srcPath)};
    if (srcTex.empty()) {
        throw std::runtime_error("could not load texture");
    }

    auto fmt = srcTex.format();
    auto extent = srcTex.extent(srcTex.base_level());

    if (args.size() == 6) {
        crop.origin = glm::ivec2(IntoInt(args[2]), IntoInt(args[3]));
        crop.size = glm::ivec2(IntoInt(args[4]), IntoInt(args[5]));
    } else {
        crop.origin = glm::ivec2(0, 0);
        crop.size = extent;
    }

    if (crop.size.x == 0 || crop.size.y == 0) {
        throw std::runtime_error("crop specification is of zero size");
    }

    if (glm::ivec2 end = crop.origin + crop.size; end.x > extent.x || end.y > extent.y) {
        throw std::runtime_error("crop specification exceeds image size");
    }

    if (gli::is_compressed(fmt)) {
        auto srcSize = srcTex.size(0);
        auto srcData = srcTex.data(0, 0, 0);
        auto blockSize = gli::block_size(fmt);
        auto blockExtent = glm::ivec2(gli::block_extent(fmt));
        auto blockCount = (extent + glm::ivec2(blockExtent - 1)) / glm::ivec2(blockExtent);
        auto components = gli::component_count(fmt);
        glm::ivec2 firstBlock(crop.origin / blockExtent);
        glm::ivec2 lastBlock((crop.origin + crop.size + blockExtent - 1) / blockExtent);
        gli::format dstFormat = components == 4 ? gli::FORMAT_RGBA8_UNORM_PACK8 : gli::FORMAT_RGB8_UNORM_PACK8;
        gli::texture2d dstTex(dstFormat, crop.size);

        switch (fmt) {
        case gli::FORMAT_RGBA_BP_UNORM_BLOCK16: {
            auto dstExtent = dstTex.extent();
            auto dstStride = gli::component_count(dstFormat) * dstExtent.x;
            auto dstData = (uint8_t *)dstTex.data();
            auto dstComp = gli::component_count(dstFormat);
            auto midFormat = gli::FORMAT_RGBA8_UNORM_PACK8;
            gli::texture2d midTex(midFormat, blockExtent);
            // Se whiteboard
            for (int blockY = firstBlock.y; blockY < lastBlock.y; ++blockY) {
                for (int blockX = firstBlock.x; blockX < lastBlock.x; ++blockX) {
                    auto blockIdx = blockX + blockY * blockCount.x;
                    uint8_t const *cmpBlock = (uint8_t const *)srcData + blockSize * blockIdx;
                    DecompressBlockBC7(cmpBlock, (uint8_t *)midTex.data());
                    glm::ivec2 blockBase = glm::ivec2(blockX, blockY) * blockExtent;

                    auto blockRel = blockBase - crop.origin;
                    for (int midY = 0; midY < 4; ++midY) {
                        auto pixRelY = blockRel.y + midY;
                        if (pixRelY >= 0 && pixRelY < crop.size.y) {
                            for (int midX = 0; midX < 4; ++midX) {
                                auto pixRelX = blockRel.x + midX;
                                if (pixRelX >= 0 && pixRelX < crop.size.x) {
                                    auto texel = midTex.load<std::array<uint8_t, 4>>(gli::ivec2(midX, midY), 0);
                                    dstTex.store(gli::ivec2(pixRelX, pixRelY), 0, texel);
                                }
                            }
                        }
                    }
                }
            }
            stbi_write_png(dstPath.c_str(), crop.size.x, crop.size.y, dstComp, dstData, dstStride);
        } break;
        }
    } else {
    }

#if 0
    Rect blocksCoveringPixels = crop;

    lv_bptc_format compFormat = LV_BPTC_FORMAT_UNKNOWN;
    switch (srcTex.format()) {
    case gli::FORMAT_RGB_BP_UFLOAT_BLOCK16:
        compFormat = LV_BPTC_FORMAT_BC6H_UF16;
        break;
    case gli::FORMAT_RGB_BP_SFLOAT_BLOCK16:
        compFormat = LV_BPTC_FORMAT_BC6H_SF16;
        break;
    case gli::FORMAT_RGBA_BP_UNORM_BLOCK16:
    case gli::FORMAT_RGBA_BP_SRGB_BLOCK16:
        compFormat = LV_BPTC_FORMAT_BC7_UNORM;
        break;
    }
    auto size = srcTex.size(0);
    auto data = srcTex.data(0, 0, 0);
    std::cerr << extent.x << "x" << extent.y << std::endl;
    {
        if (compFormat == LV_BPTC_FORMAT_BC7_UNORM) {
            std::array<int, 2> const blockPixelSize{4, 4};
            int const blockChunkSize = 16;

            int const firstBlockX = cropSpec.x / blockPixelSize[0];
            int const lastBlockX = (cropSpec.x + cropSpec.w) / blockPixelSize[0];
            int const firstBlockY = cropSpec.y / blockPixelSize[1];
            int const lastBlockY = (cropSpec.y + cropSpec.h) / blockPixelSize[1];

            int const pixelStorageStride = 4 * blockPixelSize[0] * (1 + lastBlockX - firstBlockX) * blockPixelSize[1];
            int const pixelStorageSize =
                blockPixelSize[0] * (1 + lastBlockX - firstBlockX) * blockPixelSize[1] * (1 + lastBlockY - firstBlockY);
            std::vector<uint8_t> dstData(pixelStorageSize);

            std::array<uint8_t, 16 * 4> blockPixels;
            for (int blockY = firstBlockY; blockY <= lastBlockY; ++blockY) {
                for (int blockX = firstBlockX; blockX <= lastBlockX; ++blockX) {
                    uint8_t const *blockData = data + blockChunkSize * (blockX + blockY * blockW);
                    DecompressBlockBC7(blockData, blockPixels.data());
                }
            }
            int res = DecompressBlockBC7(const unsigned char cmpBlock[16], unsigned char srcBlock[64],
                                         const void *options CMP_DEFAULTNULL);

            bool success = lv_bptc_decode(compFormat, extent.x, extent.y, data, size, dstData.data(), dstData.size());
            std::cerr << "Decode " << (success ? "success" : "failure") << "\n";
            if (!success) {
                throw std::runtime_error("decode failure for file " + srcPath);
            }
            // int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
            bool has_alpha = (compFormat == LV_BPTC_FORMAT_BC7_UNORM);
            int comp = has_alpha ? 4 : 3;
            {
                size_t dstStride = comp * extent.x;
                uint8_t const *firstPixel = dstData.data() + dstStride * cropSpec->y + comp * cropSpec->x;
                stbi_write_png(dstPath.c_str(), cropSpec->w, cropSpec->h, comp, firstPixel, dstStride);
            }
        }
    }
#endif
}

void PrintUsageAndExit(char const *progName) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "%s convert SRC.dds DST.png [x y w h]\n", progName);
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        PrintUsageAndExit(argv[0]);
    }
    std::string cmd = argv[1];

    std::deque<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    try {
        if (cmd == "convert") {
            ConvertCommand(args);
        } else {
            PrintUsageAndExit(argv[0]);
        }
        return 0;
    } catch (std::exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
    }

    return 1;
}
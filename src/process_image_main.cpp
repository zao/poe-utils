#include <deque>
#include <iostream>
#include <optional>
#include <gli/gli.hpp>
#include <gli/load.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include <stb_image_write.h>

#include "lv_bptc.h"

struct CropSpec {
    int x, y;
    int w, h;
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
    std::optional<CropSpec> cropSpec;
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

    if (args.size() == 6) {
        CropSpec cs;
        cs.x = std::stoi(args[2]);
        cs.y = std::stoi(args[3]);
        cs.w = std::stoi(args[4]);
        cs.h = std::stoi(args[5]);
        cropSpec = cs;
    }

    gli::texture2d srcTex{gli::load(srcPath)};
    if (srcTex.empty()) {
        throw std::runtime_error("could not load texture");
    }

    {
        auto extent = srcTex.extent(srcTex.base_level());
        if (!cropSpec) {
            CropSpec cs;
            cs.x = 0;
            cs.y = 0;
            cs.w = extent.x;
            cs.h = extent.y;
            cropSpec = cs;
        } else if (cropSpec->x + cropSpec->w > extent.x || cropSpec->y + cropSpec->h > extent.y) {
            throw std::runtime_error("crop specification exceeds image size");
        }
    }

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
    int coarsest_level = srcTex.max_level();
    int finest_level = srcTex.base_level();
    {
        int level = finest_level;
        auto extent = srcTex.extent(level);
        auto size = srcTex.size(level);
        auto data = srcTex.data(0, 0, level);
        std::cerr << "level " << level << ": " << extent.x << "x" << extent.y << std::endl;
        if (compFormat == LV_BPTC_FORMAT_BC7_UNORM) {
            size_t outputSize = lv_bptc_output_size(compFormat, extent.x, extent.y, data, size);
            std::vector<uint8_t> dstData(outputSize);
            std::cerr << "Storage needed for decoded level: " << outputSize << " bytes\n";
            bool success = lv_bptc_decode(compFormat, extent.x, extent.y, data, size, dstData.data(), dstData.size());
            std::cerr << "Decode " << (success ? "success" : "failure") << "\n";
            std::string dstPathSub = dstPath.substr(0, dstPath.size() - 4) + "-level" + std::to_string(level) + ".png";
            // int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
            bool has_alpha = (compFormat == LV_BPTC_FORMAT_BC7_UNORM);
            int comp = has_alpha ? 4 : 3;
            {
                size_t dstStride = comp * extent.x;
                // stbi_write_png(dstPathSub.c_str(), extent.x, extent.y, comp, dstData.data(), dstStride);
                uint8_t const *firstPixel = dstData.data() + dstStride * cropSpec->y + comp * cropSpec->x;
                stbi_write_png(dstPath.c_str(), cropSpec->w, cropSpec->h, comp, firstPixel, dstStride);
            }
        }
    }
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
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct ImageRgba {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    std::string format;
};

const char* LastImageError();
bool DecodeDds(const uint8_t* data, size_t size, ImageRgba& out);
bool DecodeImage(const uint8_t* data, size_t size, const std::wstring& path, ImageRgba& out);
bool SavePngFile(const std::filesystem::path& dest, const ImageRgba& img);
HBITMAP ImageToBitmap(const ImageRgba& img);

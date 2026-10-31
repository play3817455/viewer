#include "image_io.h"

#include <cstdio>
#include <cstring>
#include <wincodec.h>

#include "DirectXTex.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

std::string g_err = "no image";

void SetErr(const char* s, long hr = 0) {
    if (hr) {
        char buf[160];
        sprintf_s(buf, "%s (hr=0x%08lX)", s, (unsigned long)hr);
        g_err = buf;
    } else {
        g_err = s;
    }
}

const char* DxgiName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return "BC1/DXT1";
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        return "BC2/DXT3";
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return "BC3/DXT5";
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return "BC4";
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        return "BC5";
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
        return "BC6H";
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return "BC7";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "R8G8B8A8";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return "B8G8R8A8";
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return "B8G8R8X8";
    case DXGI_FORMAT_B5G6R5_UNORM:
        return "B5G6R5";
    case DXGI_FORMAT_B5G5R5A1_UNORM:
        return "B5G5R5A1";
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return "B4G4R4A4";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "RGBA16F";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return "RGBA32F";
    case DXGI_FORMAT_A8_UNORM:
        return "A8 mask";
    case DXGI_FORMAT_R8_UNORM:
        return "R8";
    case DXGI_FORMAT_R8G8_UNORM:
        return "RG8";
    default:
        return "DXGI";
    }
}

std::wstring ExtOf(const std::wstring& path) {
    std::wstring e = std::filesystem::path(path).extension().wstring();
    for (auto& c : e)
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    return e;
}

bool CopyRgba(const DirectX::Image& img, ImageRgba& out) {
    if (!img.pixels || !img.width || !img.height) {
        SetErr("empty DirectXTex image");
        return false;
    }
    if (img.width > 16384 || img.height > 16384) {
        SetErr("image too large");
        return false;
    }
    out.w = (int)img.width;
    out.h = (int)img.height;
    out.rgba.assign((size_t)out.w * out.h * 4, 0);
    const size_t row = (size_t)out.w * 4;
    for (int y = 0; y < out.h; ++y) {
        const size_t n = (img.rowPitch < row) ? img.rowPitch : row;
        memcpy(out.rgba.data() + (size_t)y * row, img.pixels + (size_t)y * img.rowPitch, n);
    }
    return true;
}

bool ToRgba(DirectX::ScratchImage& src, ImageRgba& out, const char* tag) {
    using namespace DirectX;
    const Image* img = src.GetImage(0, 0, 0);
    if (!img) {
        SetErr("no mip 0");
        return false;
    }
    ScratchImage conv;
    HRESULT hr = S_OK;
    if (IsCompressed(img->format)) {
        hr = Decompress(*img, DXGI_FORMAT_R8G8B8A8_UNORM, conv);
    } else if (img->format != DXGI_FORMAT_R8G8B8A8_UNORM &&
               img->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        hr = Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT,
                     conv);
    }
    if (FAILED(hr)) {
        SetErr("DirectXTex convert/decompress failed", hr);
        return false;
    }
    const Image* rgba = conv.GetImageCount() ? conv.GetImage(0, 0, 0) : img;
    if (!rgba) {
        SetErr("convert produced no image");
        return false;
    }
    out.format = tag;
    out.format += " ";
    out.format += DxgiName(img->format);
    char dim[64];
    sprintf_s(dim, " %ux%u", (unsigned)img->width, (unsigned)img->height);
    out.format += dim;
    if (!CopyRgba(*rgba, out)) return false;

    const size_t n = (size_t)out.w * (size_t)out.h;
    int srcCh = -1;
    if (img->format == DXGI_FORMAT_A8_UNORM) srcCh = 3;
    else if (img->format == DXGI_FORMAT_R8_UNORM) srcCh = 0;
    else if (n) {
        size_t rgbSum = 0;
        uint8_t a0 = out.rgba[3];
        bool aVar = false;
        for (size_t i = 0; i < n; ++i) {
            rgbSum += (size_t)out.rgba[i * 4] + out.rgba[i * 4 + 1] + out.rgba[i * 4 + 2];
            if (out.rgba[i * 4 + 3] != a0) aVar = true;
        }
        if (rgbSum == 0 && aVar) srcCh = 3;
    }
    if (srcCh >= 0) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t v = out.rgba[i * 4 + (size_t)srcCh];
            out.rgba[i * 4 + 0] = v;
            out.rgba[i * 4 + 1] = v;
            out.rgba[i * 4 + 2] = v;
            out.rgba[i * 4 + 3] = 255;
        }
        if (out.format.find("mask") == std::string::npos) out.format += " (gray)";
    }
    return true;
}

}  // namespace

const char* LastImageError() { return g_err.c_str(); }

bool DecodeDds(const uint8_t* data, size_t size, ImageRgba& out) {
    if (!data || size < 4 || memcmp(data, "DDS ", 4) != 0) {
        SetErr("not a DDS file");
        return false;
    }
    DirectX::ScratchImage image;
    DirectX::TexMetadata meta{};
    const HRESULT hr =
        DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &meta, image);
    if (FAILED(hr)) {
        SetErr("LoadFromDDSMemory failed", hr);
        return false;
    }
    return ToRgba(image, out, "DDS");
}

bool DecodeImage(const uint8_t* data, size_t size, const std::wstring& path, ImageRgba& out) {
    out = {};
    if (!data || size == 0) {
        SetErr("empty buffer");
        return false;
    }
    const std::wstring ext = ExtOf(path);
    if (ext == L".dxg" || ext == L".txg" || ext == L".mrb" || ext == L".vra") {
        SetErr("3D mesh/anim/attr, not an image");
        return false;
    }

    if (ext == L".dds") {
        if (size < 4 || memcmp(data, "DDS ", 4) != 0) {
            SetErr("not a DDS file (stale .hed offset or missing volume)");
            return false;
        }
        return DecodeDds(data, size, out);
    }
    if (size >= 4 && memcmp(data, "DDS ", 4) == 0) {
        if (DecodeDds(data, size, out)) return true;
    }

    if (ext == L".tga" || ext == L".targa" || (size > 18 && ext.empty())) {
        DirectX::ScratchImage image;
        const HRESULT hr =
            DirectX::LoadFromTGAMemory(data, size, DirectX::TGA_FLAGS_NONE, nullptr, image);
        if (SUCCEEDED(hr) && ToRgba(image, out, "TGA")) return true;
        if (ext == L".tga") SetErr("LoadFromTGAMemory failed", hr);
    }

    if (ext == L".hdr" || (size >= 11 && memcmp(data, "#?RADIANCE", 11) == 0) ||
        (size >= 6 && memcmp(data, "#?RGBE", 6) == 0)) {
        DirectX::ScratchImage image;
        const HRESULT hr = DirectX::LoadFromHDRMemory(data, size, nullptr, image);
        if (SUCCEEDED(hr) && ToRgba(image, out, "HDR")) return true;
    }

    const bool tryWic = ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
                        ext == L".tif" || ext == L".tiff" || ext == L".gif" || ext.empty();
    if (tryWic) {
        DirectX::ScratchImage image;
        const HRESULT hr =
            DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, nullptr, image);
        if (SUCCEEDED(hr) && ToRgba(image, out, "WIC")) return true;
        if (!ext.empty()) SetErr("LoadFromWICMemory failed", hr);
    }

    if (g_err == "no image" || g_err.empty()) SetErr("unsupported image format");
    return false;
}

bool SavePngFile(const std::filesystem::path& dest, const ImageRgba& img) {
    if (img.w <= 0 || img.h <= 0 || img.rgba.size() < (size_t)img.w * img.h * 4) return false;
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    DirectX::Image dx{};
    dx.width = (size_t)img.w;
    dx.height = (size_t)img.h;
    dx.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dx.rowPitch = (size_t)img.w * 4;
    dx.slicePitch = dx.rowPitch * (size_t)img.h;
    dx.pixels = const_cast<uint8_t*>(img.rgba.data());
    const HRESULT hr = DirectX::SaveToWICFile(dx, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
                                              dest.wstring().c_str());
    if (FAILED(hr)) {
        SetErr("SaveToWICFile PNG failed", hr);
        return false;
    }
    return true;
}

HBITMAP ImageToBitmap(const ImageRgba& img) {
    if (img.w <= 0 || img.h <= 0) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = img.w;
    bi.bmiHeader.biHeight = -img.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb || !bits) return hb;
    auto* dst = (uint8_t*)bits;
    for (int i = 0; i < img.w * img.h; ++i) {
        const unsigned a = img.rgba[i * 4 + 3];
        dst[i * 4 + 0] = (uint8_t)((img.rgba[i * 4 + 2] * a) / 255);
        dst[i * 4 + 1] = (uint8_t)((img.rgba[i * 4 + 1] * a) / 255);
        dst[i * 4 + 2] = (uint8_t)((img.rgba[i * 4 + 0] * a) / 255);
        dst[i * 4 + 3] = (uint8_t)a;
    }
    return hb;
}

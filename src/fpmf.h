#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fpmf {

struct VolumeInfo {
    std::wstring format;  // e.g. ./font/%04x.dat
    uint32_t extraCount = 0;
    uint32_t extraField = 0;
};

struct FileEntry {
    std::wstring folder;
    std::wstring path;
    uint32_t volume = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint8_t fileKey[8]{};
    uint8_t hash[8]{};
};

struct Archive {
    std::filesystem::path hedPath;
    std::filesystem::path dataRoot;
    VolumeInfo volume;
    std::vector<FileEntry> files;
};

struct Catalog {
    std::filesystem::path dataRoot;
    std::vector<Archive> archives;
};

void ScrambleSub(uint8_t* data, size_t size, const uint8_t* key, size_t keyLen,
                 size_t start = 0);
void DecryptHedPayload(uint8_t* data, size_t size);
void DecryptDat(uint8_t* data, size_t size, const uint8_t fileKey[8]);

std::filesystem::path NormalizeDataRoot(const std::filesystem::path& path);
Archive LoadHed(const std::filesystem::path& hedPath);
Catalog ScanDataRoot(const std::filesystem::path& dataRoot);

std::filesystem::path VolumePath(const Archive& ar, uint32_t volume);
bool VolumeExists(const Archive& ar, uint32_t volume);
std::vector<uint8_t> ExtractFile(const Archive& ar, const FileEntry& file,
                                 const Catalog* cat = nullptr);
void ExtractFileTo(const Archive& ar, const FileEntry& file,
                   const std::filesystem::path& dest, const Catalog* cat = nullptr);

const char* GuessKind(const std::wstring& path, const uint8_t* data, size_t size);

}  // namespace fpmf

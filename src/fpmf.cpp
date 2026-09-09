#include "fpmf.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "keys.inc"

namespace fpmf {
namespace {

std::wstring ReadWstring(const uint8_t* p, size_t chars) {
    std::wstring s;
    s.resize(chars);
    for (size_t i = 0; i < chars; ++i) {
        s[i] = static_cast<wchar_t>(p[i * 2] | (p[i * 2 + 1] << 8));
    }
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

void Ensure(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    Ensure(in.good(), "open failed");
    in.seekg(0, std::ios::end);
    const auto n = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<uint8_t> buf(n);
    if (n) in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    return buf;
}

}  // namespace

void ScrambleSub(uint8_t* data, size_t size, const uint8_t* key, size_t keyLen,
                 size_t start) {
    if (!key || keyLen == 0 || !data) return;
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(data[i] - key[(start + i) % keyLen]);
    }
}

void DecryptHedPayload(uint8_t* data, size_t size) {
    ScrambleSub(data, size, kFpmfKeyHed, sizeof(kFpmfKeyHed));
}

void DecryptDat(uint8_t* data, size_t size, const uint8_t fileKey[8]) {
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(data[i] - fileKey[i % 8] - kFpmfKeyDat[i % sizeof(kFpmfKeyDat)]);
    }
}

Archive LoadHed(const std::filesystem::path& hedPath) {
    Archive ar;
    ar.hedPath = hedPath;
    ar.dataRoot = hedPath.parent_path();

    const auto raw = ReadAll(hedPath);
    Ensure(raw.size() >= 16, "hed too small");
    Ensure(raw[0] == 'F' && raw[1] == 'P' && raw[2] == 'M' && raw[3] == 'F',
           "not FPMF");

    const uint32_t ver = *reinterpret_cast<const uint32_t*>(raw.data() + 4);
    const uint32_t psz = *reinterpret_cast<const uint32_t*>(raw.data() + 8);
    const uint32_t nchunk = *reinterpret_cast<const uint32_t*>(raw.data() + 12);
    (void)ver;
    Ensure(raw.size() >= 16 + psz, "hed payload truncated");

    std::vector<uint8_t> payload(raw.begin() + 16, raw.begin() + 16 + psz);
    DecryptHedPayload(payload.data(), payload.size());

    size_t off = 0;
    for (uint32_t c = 0; c < nchunk; ++c) {
        Ensure(off + 8 <= payload.size(), "chunk header overrun");
        const uint32_t type = *reinterpret_cast<const uint32_t*>(payload.data() + off);
        const uint32_t sz = *reinterpret_cast<const uint32_t*>(payload.data() + off + 4);
        off += 8;
        Ensure(off + sz <= payload.size(), "chunk body overrun");
        const uint8_t* body = payload.data() + off;

        if (type == 1 && sz >= 1) {
            const uint8_t n = body[0];
            Ensure(static_cast<size_t>(1 + n * 2 + 8) <= sz, "type1 short");
            ar.volume.format = ReadWstring(body + 1, n);
            ar.volume.extraCount = *reinterpret_cast<const uint32_t*>(body + 1 + n * 2);
            ar.volume.extraField = *reinterpret_cast<const uint32_t*>(body + 1 + n * 2 + 4);
        } else if (type == 3 && sz >= 4) {
            const uint32_t count = *reinterpret_cast<const uint32_t*>(body);
            size_t p = 4;
            ar.files.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                Ensure(p + 1 <= sz, "entry namelen");
                const uint8_t nlen = body[p++];
                Ensure(p + nlen * 2 + 1 <= sz, "entry name");
                FileEntry e;
                e.folder = ReadWstring(body + p, nlen);
                p += nlen * 2u;
                const uint8_t plen = body[p++];
                if (plen) {
                    Ensure(p + plen * 2 <= sz, "entry path");
                    e.path = ReadWstring(body + p, plen);
                    p += plen * 2u;
                }
                Ensure(p + 4 + 8 + 4 + 4 + 8 <= sz, "entry tail");
                e.volume = *reinterpret_cast<const uint32_t*>(body + p);
                p += 4;
                memcpy(e.fileKey, body + p, 8);
                p += 8;
                e.offset = *reinterpret_cast<const uint32_t*>(body + p);
                p += 4;
                e.size = *reinterpret_cast<const uint32_t*>(body + p);
                p += 4;
                memcpy(e.hash, body + p, 8);
                p += 8;
                ar.files.push_back(std::move(e));
            }
        }
        off += sz;
    }
    return ar;
}

std::filesystem::path NormalizeDataRoot(const std::filesystem::path& path) {
    std::error_code ec;
    auto p = path;
    if (p.empty()) return p;
    const auto data = p / L"data";
    auto looks = [](const std::filesystem::path& dir) {
        return std::filesystem::exists(dir / L"interface.hed") ||
               std::filesystem::exists(dir / L"fx.hed") ||
               std::filesystem::exists(dir / L"item.hed") ||
               std::filesystem::exists(dir / L"font.hed");
    };
    if (looks(data)) return data;
    if (std::filesystem::exists(data, ec)) {
        for (auto& ent : std::filesystem::directory_iterator(data, ec)) {
            if (ent.path().extension() == L".hed") return data;
        }
    }
    return p;
}

Catalog ScanDataRoot(const std::filesystem::path& dataRoot, ScanProgressFn progress, void* ctx) {
    Catalog cat;
    cat.dataRoot = NormalizeDataRoot(dataRoot);
    if (!std::filesystem::exists(cat.dataRoot)) return cat;
    if (progress) progress(0, 0, L"", ctx);
    std::vector<std::filesystem::path> heds;
    unsigned found = 0;
    for (auto& ent : std::filesystem::recursive_directory_iterator(cat.dataRoot)) {
        if (ent.is_regular_file() && ent.path().extension() == L".hed") {
            heds.push_back(ent.path());
            ++found;
            if (progress && (found == 1 || (found % 8) == 0))
                progress(found, 0, ent.path().filename().c_str(), ctx);
        }
    }
    std::sort(heds.begin(), heds.end());
    cat.archives.reserve(heds.size());
    const unsigned total = (unsigned)heds.size();
    if (progress) progress(0, total, L"", ctx);
    unsigned i = 0;
    for (const auto& h : heds) {
        try {
            Archive ar = LoadHed(h);
            ar.dataRoot = cat.dataRoot;
            cat.archives.push_back(std::move(ar));
        } catch (...) {
        }
        ++i;
        if (progress) progress(i, total, h.filename().c_str(), ctx);
    }
    return cat;
}

std::wstring NormRel(std::wstring p) {
    for (auto& c : p) {
        if (c == L'\\') c = L'/';
        else if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    while (p.size() >= 2 && p[0] == L'.' && p[1] == L'/') p.erase(0, 2);
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    return p;
}

std::wstring EntryRel(const FileEntry& e) {
    return NormRel(e.path.empty() ? e.folder : e.path);
}

std::wstring JustName(const std::wstring& p) {
    const auto s = p.rfind(L'/');
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

bool RelMatch(const std::wstring& a, const std::wstring& b) {
    if (a == b) return true;
    if (a.size() > b.size() && a[a.size() - b.size() - 1] == L'/' &&
        a.compare(a.size() - b.size(), std::wstring::npos, b) == 0)
        return true;
    if (b.size() > a.size() && b[b.size() - a.size() - 1] == L'/' &&
        b.compare(b.size() - a.size(), std::wstring::npos, a) == 0)
        return true;
    return false;
}

std::filesystem::path FormatVolumeRel(const Archive& ar, uint32_t volume) {
    wchar_t buf[512]{};
    const std::wstring& fmt = ar.volume.format;
    if (fmt.find(L"%") != std::wstring::npos) {
        swprintf_s(buf, fmt.c_str(), volume);
    } else {
        swprintf_s(buf, L"%s", fmt.c_str());
    }
    std::wstring s = buf;
    if (s.size() >= 2 && s[0] == L'.' && (s[1] == L'/' || s[1] == L'\\')) s.erase(0, 2);
    for (auto& c : s)
        if (c == L'\\') c = L'/';
    return s;
}

std::filesystem::path VolumePath(const Archive& ar, uint32_t volume) {
    const auto rel = FormatVolumeRel(ar, volume);
    const auto name = rel.filename();
    const std::filesystem::path cands[] = {
        ar.dataRoot / rel,
        ar.dataRoot / L"data" / rel,
        ar.hedPath.parent_path() / name,
        ar.hedPath.parent_path() / ar.hedPath.stem() / name,
        ar.hedPath.parent_path() / rel,
    };
    for (const auto& c : cands) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
    }
    return ar.dataRoot / rel;
}

bool VolumeExists(const Archive& ar, uint32_t volume) {
    std::error_code ec;
    return std::filesystem::exists(VolumePath(ar, volume), ec);
}

bool RedirectToChild(const Catalog& cat, const Archive& ar, const FileEntry& file,
                     const Archive*& outAr, const FileEntry*& outFile) {
    const auto rel = EntryRel(file);
    const auto slash = rel.find(L'/');
    if (slash == std::wstring::npos) return false;
    const std::wstring first = rel.substr(0, slash);
    const std::wstring rest = rel.substr(slash + 1);
    const auto parent = ar.hedPath.parent_path();
    const auto nested = parent / ar.hedPath.stem();

    const Archive* bestAr = nullptr;
    const FileEntry* bestFile = nullptr;
    int bestScore = 0;

    for (const auto& other : cat.archives) {
        if (&other == &ar) continue;
        if (NormRel(other.hedPath.stem().wstring()) != first) continue;
        const auto op = other.hedPath.parent_path();
        if (op != parent && op != nested) continue;
        for (const auto& f2 : other.files) {
            const auto r2 = EntryRel(f2);
            int score = 0;
            if (r2 == rest) score = 3;
            else if (RelMatch(r2, rest)) score = 2;
            else if (JustName(r2) == JustName(rest) && !JustName(rest).empty()) score = 1;
            if (score > bestScore) {
                bestScore = score;
                bestAr = &other;
                bestFile = &f2;
            }
        }
    }
    if (bestScore <= 0 || !bestAr || !bestFile) return false;
    outAr = bestAr;
    outFile = bestFile;
    return true;
}

std::vector<uint8_t> ReadVolumeSlice(const Archive& ar, const FileEntry& file) {
    const auto vol = VolumePath(ar, file.volume);
    std::ifstream in(vol, std::ios::binary);
    if (!in.good()) {
        throw std::runtime_error(std::string("volume open failed: ") + vol.u8string());
    }
    in.seekg(file.offset);
    std::vector<uint8_t> buf(file.size);
    if (file.size) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file.size));
        Ensure(static_cast<uint32_t>(in.gcount()) == file.size, "short read");
    }
    DecryptDat(buf.data(), buf.size(), file.fileKey);
    return buf;
}

std::vector<uint8_t> ReadDatSpan(const std::filesystem::path& dir, uint32_t offset, uint32_t size,
                                 const uint8_t fileKey[8]) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        throw std::runtime_error("span dir missing");
    }
    std::vector<std::filesystem::path> dats;
    for (auto& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ent.is_regular_file() && ent.path().extension() == L".dat") dats.push_back(ent.path());
    }
    std::sort(dats.begin(), dats.end());
    if (dats.empty()) throw std::runtime_error("span has no dat");

    uint64_t skip = offset;
    std::vector<uint8_t> buf(size);
    uint32_t got = 0;
    for (const auto& dp : dats) {
        const auto sz = std::filesystem::file_size(dp, ec);
        if (ec) continue;
        if (skip >= sz) {
            skip -= sz;
            continue;
        }
        std::ifstream in(dp, std::ios::binary);
        if (!in.good()) throw std::runtime_error(std::string("span open failed: ") + dp.u8string());
        in.seekg(static_cast<std::streamoff>(skip));
        skip = 0;
        const uint32_t want = size - got;
        const uint32_t chunk = static_cast<uint32_t>((std::min)(static_cast<uint64_t>(want), sz - static_cast<uint64_t>(in.tellg())));
        in.read(reinterpret_cast<char*>(buf.data() + got), static_cast<std::streamsize>(chunk));
        got += static_cast<uint32_t>(in.gcount());
        if (got >= size) break;
    }
    if (got != size) throw std::runtime_error("span short read");
    DecryptDat(buf.data(), buf.size(), fileKey);
    return buf;
}

std::filesystem::path SidecarDir(const Archive& ar, const FileEntry& file) {
    const auto rel = EntryRel(file);
    const auto slash = rel.find(L'/');
    if (slash == std::wstring::npos) return {};
    const std::wstring first = rel.substr(0, slash);
    const std::filesystem::path cands[] = {
        ar.hedPath.parent_path() / first,
        ar.hedPath.parent_path() / ar.hedPath.stem() / first,
        ar.dataRoot / first,
    };
    std::error_code ec;
    for (const auto& c : cands) {
        if (std::filesystem::is_directory(c, ec) &&
            std::filesystem::exists(c / L"0000.dat", ec))
            return c;
    }
    return {};
}

std::vector<uint8_t> ExtractFile(const Archive& ar, const FileEntry& file, const Catalog* cat) {
    if (VolumeExists(ar, file.volume)) return ReadVolumeSlice(ar, file);
    if (cat) {
        const Archive* oar = nullptr;
        const FileEntry* ofile = nullptr;
        if (RedirectToChild(*cat, ar, file, oar, ofile) && oar && ofile &&
            VolumeExists(*oar, ofile->volume)) {
            return ReadVolumeSlice(*oar, *ofile);
        }
        const auto rel = EntryRel(file);
        const auto slash = rel.find(L'/');
        if (slash != std::wstring::npos) {
            const std::wstring first = rel.substr(0, slash);
            for (const auto& other : cat->archives) {
                if (&other == &ar) continue;
                if (NormRel(other.hedPath.stem().wstring()) != first) continue;
                const auto op = other.hedPath.parent_path();
                if (op != ar.hedPath.parent_path() &&
                    op != ar.hedPath.parent_path() / ar.hedPath.stem())
                    continue;
                if (!VolumeExists(other, 0) &&
                    (other.files.empty() || !VolumeExists(other, other.files[0].volume)))
                    continue;
                throw std::runtime_error(
                    std::string("stale catalog ") + ar.hedPath.filename().u8string() +
                    " - data lives in " + other.hedPath.filename().u8string() +
                    " under different names");
            }
        }
    }
    const auto side = SidecarDir(ar, file);
    if (!side.empty()) {
        return ReadDatSpan(side, file.offset, file.size, file.fileKey);
    }
    const auto vol = VolumePath(ar, file.volume);
    throw std::runtime_error(std::string("volume open failed: ") + vol.u8string());
}

void ExtractFileTo(const Archive& ar, const FileEntry& file,
                   const std::filesystem::path& dest, const Catalog* cat) {
    auto data = ExtractFile(ar, file, cat);
    std::filesystem::create_directories(dest.parent_path());
    std::ofstream out(dest, std::ios::binary);
    Ensure(out.good(), "dest open failed");
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

const char* GuessKind(const std::wstring& path, const uint8_t* data, size_t size) {
    auto lower = path;
    for (auto& ch : lower) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch + 32);
    auto has = [&](const wchar_t* ext) {
        const size_t n = wcslen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    if (size >= 4 && data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S')
        return "Ogg (FMOD / VLC)";
    if (size >= 4 && data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f')
        return "TrueType Collection";
    if (has(L".dxg") || has(L".txg")) return "3D model (mesh)";
    if (has(L".mrb")) return "3D animation";
    if (has(L".vra")) return "3D model attributes";
    if (size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
        return "DirectDraw Surface";
    if (has(L".ogg")) return "Ogg Vorbis";
    if (has(L".ogv")) return "Ogg Theora video";
    if (has(L".csv")) return "CSV text";
    if (has(L".cfg") || has(L".ini") || has(L".txt")) return "Text";
    if (has(L".xml")) return "XML";
    if (has(L".dds")) return "DDS texture";
    if (has(L".tga")) return "TGA image";
    if (has(L".ttc") || has(L".ttf")) return "Font";
    return "Binary";
}

}  // namespace fpmf

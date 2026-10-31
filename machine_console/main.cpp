#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "anim.h"
#include "image_io.h"
#include "preview3d.h"
#include "scene_vra.h"
#include "vra_text.h"

namespace fs = std::filesystem;
enum { ID_ROOT = 100, ID_BROWSE, ID_MS, ID_LOAD, ID_KIND, ID_ASSETS, ID_PROFILE, ID_SAVE, ID_VIEW, ID_SCENE, ID_SCENE_CENTER, ID_SCENE_RADIUS, ID_LOAD_SCENE, ID_ASSET_SEARCH, ID_FX_TIME,
       ID_ATTACH_POS, ID_ATTACH_ROT, ID_ATTACH_SCALE, ID_ATTACH_BONE, ID_ATTACH,
       ID_TAB_OVERVIEW = 130, ID_TAB_WEAPONS, ID_TAB_TRAITS, ID_TAB_MOTION, ID_TAB_EFFECTS, ID_TAB_SIMULATOR,
       ID_UNIT_TITLE = 150, ID_UNIT_META, ID_UNIT_CARD, ID_WEAPON_CARD, ID_SOURCE_CARD,
       ID_FIELD_LABEL_FIRST = 200, ID_FIELD_EDIT_FIRST = 240 };
struct Unit { std::wstring id, name, modelId, dir, model, wiki; };
struct Weapon { std::wstring id, slot, dir, model, attr, name, basis; };
struct Asset { std::wstring path, label; int kind = 0; };

HWND g_main = nullptr, g_rootEdit = nullptr, g_msEdit = nullptr, g_kind = nullptr, g_fxTimeline = nullptr;
HWND g_sceneEdit = nullptr, g_sceneCenterEdit = nullptr, g_sceneRadiusEdit = nullptr;
HWND g_attachPosEdit = nullptr, g_attachRotEdit = nullptr, g_attachScaleEdit = nullptr, g_attachBone = nullptr;
HWND g_assets = nullptr, g_assetSearch = nullptr, g_profile = nullptr, g_status = nullptr, g_view = nullptr, g_imageView = nullptr;
HWND g_unitTitle = nullptr, g_unitMeta = nullptr, g_unitCard = nullptr, g_weaponCard = nullptr, g_sourceCard = nullptr;
HWND g_fieldLabels[25]{}; HWND g_fieldEdits[25]{};
const wchar_t* g_fieldKeys[25] = {
    L"mass",L"height",L"walk_speed",L"boost_speed",L"boost_capacity",L"jump_power",L"shield_hp",
    L"ability1",L"ability2",L"additional1",L"additional2",
    L"slot1_id",L"slot2_id",L"slot3_id",L"slot1_battle",L"slot2_battle",L"slot3_battle",L"slot1_killlog",L"slot2_killlog",L"slot3_killlog",
    L"boost_start",L"boost_loop",L"boost_overheat",L"projectile",L"impact"
};
const wchar_t* g_fieldTitles[25] = {
    L"质量",L"碰撞/模型高度",L"步行速度",L"推进速度",L"推进容量",L"跳跃力",L"护盾耐久",
    L"机体特性 1",L"机体特性 2",L"追加特性 1",L"追加特性 2",
    L"槽位 1 武器 ID",L"槽位 2 武器 ID",L"槽位 3 武器 ID",L"槽位 1 战斗图标 ID",L"槽位 2 战斗图标 ID",L"槽位 3 战斗图标 ID",L"槽位 1 Killlog ID",L"槽位 2 Killlog ID",L"槽位 3 Killlog ID",
    L"推进启动 ID / 资源路径",L"推进持续 ID / 资源路径",L"过载 ID / 资源路径",L"弹体 ID / 资源路径",L"命中/爆炸 ID / 资源路径"
};
int g_activeTab = 0;
std::wstring g_sceneCode;
bool g_sceneLoaded = false;
bool g_fxIndexed = false;
bool g_itemIndexed = false;
bool g_fxSpritePreview = false;
std::wstring g_fxSpriteInfo;
HBRUSH g_darkBrush = nullptr, g_panelBrush = nullptr, g_editBrush = nullptr;
ImageRgba g_imageData;
HBITMAP g_imageBitmap = nullptr;
struct FxPreviewNode {
    bool particle = false;
    std::wstring imagePath;
    std::shared_ptr<ImageRgba> image;
    float plugStart = 0.0f, plugEnd = 1.0f;
    float width = 16.0f, height = 16.0f;
    float color[4]{1,1,1,1};
    float pos[3]{}, posRnd[3]{}, posVel[3]{}, posVelRnd[3]{}, posAcl[3]{}, posAclRnd[3]{};
    float move[3]{0,1,0}, moveVel = 0.0f, moveAcl = 0.0f;
    float rot[3]{}, rotRnd[3]{}, rotVel[3]{}, rotVelRnd[3]{}, rotAcl[3]{}, rotAclRnd[3]{};
    float scale[3]{1,1,1}, scaleRnd[3]{}, scaleVel[3]{}, scaleVelRnd[3]{}, scaleAcl[3]{}, scaleAclRnd[3]{};
    int count = 1;
    float cycle = 1.0f, life = 1.0f, lifeRnd = 0.0f, fadeIn = 0.0f, fadeInRnd = 0.0f, fadeOut = 0.0f, fadeOutRnd = 0.0f;
    int animType = 0;
    float animUv[2]{};
};
std::vector<FxPreviewNode> g_fxNodes;
std::vector<std::wstring> g_fxMissingTextures;
std::wstring g_fxPath;
int g_fxDuration = 250;
int g_fxCurrentFrame = 0;
fs::path g_root, g_profileDir;
std::map<std::wstring, Unit> g_units;
std::map<std::wstring, std::vector<Weapon>> g_weapons;
std::vector<Asset> g_visible;
std::vector<Asset> g_assetsForUnit;
Unit g_unit;
std::vector<uint8_t> g_modelBytes, g_attrBytes, g_motionBytes;

std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
std::wstring TrimAssetPathSegments(const std::wstring& path) {
    std::wstring normalized = path, result;
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    size_t begin = 0;
    auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000; };
    while (begin <= normalized.size()) {
        const size_t sep = normalized.find(L'/', begin);
        const size_t end = sep == std::wstring::npos ? normalized.size() : sep;
        size_t first = begin, last = end;
        while (first < last && isSpace(normalized[first])) ++first;
        while (last > first && isSpace(normalized[last - 1])) --last;
        result.append(normalized, first, last - first);
        if (sep == std::wstring::npos) break;
        result.push_back(L'/'); begin = sep + 1;
    }
    return result;
}
std::wstring FromUtf8(const std::string& s) {
    const UINT codePages[] = {CP_UTF8, 932, CP_ACP};
    for (UINT codePage : codePages) {
        const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
        const int n = MultiByteToWideChar(codePage, flags, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) continue;
        std::wstring w((size_t)n, L'\0');
        if (MultiByteToWideChar(codePage, flags, s.data(), (int)s.size(), w.data(), n) == n) return w;
    }
    return {};
}
std::string ToUtf8(const std::wstring& s) {
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string out((size_t)(n>0?n:0),'\0');
    if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr);
    return out;
}
std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
std::vector<std::wstring> Split(const std::string& line) {
    std::vector<std::wstring> out; std::string part;
    for (char c : line) { if (c == ',') { out.push_back(FromUtf8(part)); part.clear(); }
        else if (c != '\r') part.push_back(c); }
    out.push_back(FromUtf8(part)); return out;
}
std::wstring ReadEdit(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd); std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1); s.resize((size_t)n); return s;
}
bool ReadFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {}); return !out.empty();
}
fs::path AssetPath(std::wstring rel) {
    std::replace(rel.begin(), rel.end(), L'\\', L'/');
    while (!rel.empty() && rel.front() == L'/') rel.erase(rel.begin());
    const fs::path relative(rel);
    if (relative.has_root_name() || relative.has_root_directory() || relative.is_absolute()) return {};
    for (const auto& part : relative)
        if (part == L"..") return {};
    return g_root / relative;
}
std::wstring AttrStemForModel(std::wstring stem) {
    while (!stem.empty() && iswspace(stem.back())) stem.pop_back();
    const std::wstring prefix = Lower(stem.substr(0, (std::min)(size_t(4), stem.size())));
    if (prefix == L"_em_") stem.replace(1, 2, L"EZ");
    else if (prefix == L"msm_") stem.replace(0, 3, L"MSZ");
    else if (prefix == L"bsm_") stem.replace(0, 3, L"BSZ");
    else if (prefix == L"bam_") stem.replace(0, 3, L"BAZ");
    else if (prefix.rfind(L"em_", 0) == 0 || prefix.rfind(L"en_", 0) == 0) stem.replace(0, 2, L"EZ");
    else if (prefix == L"mam_") stem.replace(0, 3, L"MAZ");
    else if (prefix.rfind(L"pm_", 0) == 0) stem.replace(0, 2, L"PZ");
    else if (prefix == L"plm_") stem.replace(0, 3, L"PLZ");
    else if (prefix.rfind(L"um_", 0) == 0) stem.replace(0, 2, L"UZ");
    else if (prefix.rfind(L"om_", 0) == 0) stem.replace(0, 2, L"OZ");
    else if (prefix.rfind(L"fm_", 0) == 0) stem.replace(0, 2, L"FZ");
    else if (prefix.rfind(L"sm_", 0) == 0) stem.replace(0, 2, L"SZ");
    else if (prefix == L"opm_") stem.replace(0, 3, L"OPZ");
    else if (prefix == L"evm_") stem.replace(0, 3, L"EVZ");
    else if (prefix.rfind(L"dm_", 0) == 0) stem.replace(0, 2, L"DZ");
    return stem;
}
fs::path AttrPathForModel(const fs::path& modelPath) {
    std::wstring original = modelPath.stem().wstring();
    while (!original.empty() && iswspace(original.back())) original.pop_back();
    const fs::path dir = modelPath.parent_path().parent_path() / L"attr";
    const fs::path mapped = dir / (AttrStemForModel(original) + L".vra");
    std::error_code ec;
    if (fs::is_regular_file(mapped, ec)) return mapped;
    const fs::path sameStem = dir / (original + L".vra");
    if (fs::is_regular_file(sameStem, ec)) return sameStem;
    auto normalize = [](std::wstring value) {
        std::replace(value.begin(), value.end(), L'\\', L'/');
        while (value.rfind(L"./", 0) == 0) value.erase(0, 2);
        while (!value.empty() && value.front() == L'/') value.erase(value.begin());
        return Lower(fs::path(value).lexically_normal().generic_wstring());
    };
    const std::wstring target = normalize(modelPath.lexically_relative(g_root).generic_wstring());
    fs::path geometryMatch;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || Lower(it->path().extension().wstring()) != L".vra") continue;
        std::vector<uint8_t> bytes;
        if (!ReadFile(it->path(), bytes)) continue;
        VraDoc doc;
        if (!doc.Parse((const char*)bytes.data(), bytes.size()) || doc.geometry.empty()) continue;
        if (normalize(FromUtf8(doc.geometry)) != target) continue;
        if (!geometryMatch.empty()) return mapped;  // Do not choose between duplicate geometry declarations.
        geometryMatch = it->path();
    }
    if (!geometryMatch.empty()) return geometryMatch;
    return mapped;
}
std::wstring ModelStemForAttr(std::wstring stem) {
    const std::wstring prefix = Lower(stem.substr(0, (std::min)(size_t(4), stem.size())));
    if (prefix == L"_ez_") stem.replace(1, 2, L"EM");
    else if (prefix == L"msz_") stem.replace(0, 3, L"MSM");
    else if (prefix == L"bsz_") stem.replace(0, 3, L"BSM");
    else if (prefix == L"baz_") stem.replace(0, 3, L"BAM");
    else if (prefix.rfind(L"ez_", 0) == 0) stem.replace(0, 2, L"EM");
    else if (prefix == L"maz_") stem.replace(0, 3, L"MAM");
    else if (prefix.rfind(L"pz_", 0) == 0) stem.replace(0, 2, L"PM");
    else if (prefix == L"plz_") stem.replace(0, 3, L"PLM");
    else if (prefix.rfind(L"uz_", 0) == 0) stem.replace(0, 2, L"UM");
    else if (prefix.rfind(L"oz_", 0) == 0) stem.replace(0, 2, L"OM");
    else if (prefix.rfind(L"fz_", 0) == 0) stem.replace(0, 2, L"FM");
    else if (prefix.rfind(L"sz_", 0) == 0) stem.replace(0, 2, L"SM");
    else if (prefix == L"opz_") stem.replace(0, 3, L"OPM");
    else if (prefix == L"evz_") stem.replace(0, 3, L"EVM");
    else if (prefix.rfind(L"dz_", 0) == 0) stem.replace(0, 2, L"DM");
    else return {};
    return stem;
}
std::map<std::wstring, std::vector<fs::path>> g_assetPathsByName;
fs::path g_assetIndexRoot;
void EnsureAssetPathIndex() {
    if (g_assetIndexRoot == g_root && !g_assetPathsByName.empty()) return;
    g_assetPathsByName.clear();
    g_assetIndexRoot = g_root;
    std::error_code ec;
    fs::recursive_directory_iterator it(g_root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        g_assetPathsByName[Lower(it->path().filename().wstring())].push_back(it->path());
    }
}
fs::path ResolveTexturePath(const std::wstring& rel, std::wstring* resolution = nullptr,
                            const std::wstring& geometryContext = L"") {
    if (resolution) resolution->clear();
    const std::wstring cleaned = TrimAssetPathSegments(rel);
    const fs::path exact = AssetPath(cleaned);
    std::error_code ec;
    if (fs::is_regular_file(exact, ec)) {
        if (resolution && cleaned != rel) *resolution = L"全角/边界空格修正";
        return exact;
    }
    // Repair two deterministic filename slips seen in official FX VRA data:
    // repeated dots and a one-digit final sequence number. Only accept a
    // transformed name in the referenced directory, and only when unique.
    std::wstring filename = exact.filename().wstring();
    std::vector<std::wstring> aliases;
    std::wstring collapsed;
    for (wchar_t ch : filename) {
        if (ch == L'.' && !collapsed.empty() && collapsed.back() == L'.') continue;
        collapsed.push_back(ch);
    }
    if (collapsed != filename) aliases.push_back(collapsed);
    for (const wchar_t* ext : {L".dds", L".png", L".tga"}) {
        const size_t at = Lower(filename).find(ext);
        if (at != std::wstring::npos && at + wcslen(ext) < filename.size()) {
            aliases.push_back(filename.substr(0, at + wcslen(ext)));
            break;
        }
    }
    const size_t extAt = filename.find_last_of(L'.');
    const size_t digitAt = extAt == std::wstring::npos ? filename.size() : extAt;
    if (digitAt >= 2 && filename[digitAt - 1] >= L'0' && filename[digitAt - 1] <= L'9' &&
        filename[digitAt - 2] == L'_' &&
        (digitAt < 3 || filename[digitAt - 3] < L'0' || filename[digitAt - 3] > L'9')) {
        std::wstring padded = filename;
        padded.insert(digitAt - 1, 1, L'0');
        aliases.push_back(std::move(padded));
    }
    if (extAt != std::wstring::npos) {
        size_t trimmedEnd = extAt;
        while (trimmedEnd > 0 && (filename[trimmedEnd - 1] == L' ' || filename[trimmedEnd - 1] == L'\t' ||
                                  filename[trimmedEnd - 1] == L'\r' || filename[trimmedEnd - 1] == L'\n' ||
                                  filename[trimmedEnd - 1] == 0x3000)) --trimmedEnd;
        if (trimmedEnd != extAt)
            aliases.push_back(filename.substr(0, trimmedEnd) + filename.substr(extAt));
        for (const wchar_t* ext : {L".dds", L".png", L".tga"})
            if (Lower(filename.substr(extAt)) != ext)
                aliases.push_back(filename.substr(0, extAt) + ext);
    }
    // Bind stale item texture references to same-directory assets proven by
    // the VRA's paired EM model and available sibling files.
    std::wstring itemGeometryTextureAlias;
    fs::path mapTextureFallbackPath;
    std::wstring mapTextureFallbackResolution = L"跨目录地图近似贴图回退";
    if (!geometryContext.empty()) {
        fs::path geometryPath(geometryContext);
        geometryPath = geometryPath.lexically_normal();
        const auto first = geometryPath.begin();
        const std::wstring category = geometryPath.parent_path().parent_path().parent_path().filename().wstring();
        const std::wstring itemId = geometryPath.parent_path().parent_path().filename().wstring();
        const std::wstring modelStem = geometryPath.stem().wstring();
        const std::wstring expectedStem = L"EM_" + category + L"_" + itemId + L"_";
        const fs::path expectedTextureDir = geometryPath.parent_path().parent_path() / L"tex";
        if (first != geometryPath.end() && Lower((*first).wstring()) == L"item" &&
            category.size() == 2 && itemId.size() == 4 &&
            Lower(modelStem).rfind(Lower(expectedStem), 0) == 0 &&
            expectedTextureDir == exact.parent_path()) {
            const std::wstring variant = modelStem.substr(expectedStem.size());
            if (category == L"04" && itemId == L"8417" && Lower(filename) == L"saber_brade.tga" &&
                (variant == L"01" || variant == L"02")) {
                mapTextureFallbackPath = g_root / L"item/04/8413/tex" / (L"ET_04_8413_" + variant + L"C.dds");
                mapTextureFallbackResolution = L"兄弟 item 同材质贴图近似回退";
            }
            if (Lower(filename) == L"saber_brade.tga" && !variant.empty() && variant.size() <= 2 &&
                std::all_of(variant.begin(), variant.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
                aliases.push_back(L"ET_" + category + L"_" + itemId + L"_" + variant + L"C.dds");
            const std::wstring lowerName = Lower(filename);
            const std::wstring missingPrefix = L"et_12_0107_";
            if (category == L"12" && itemId == L"0107" &&
                lowerName.rfind(missingPrefix, 0) == 0 && lowerName.size() == missingPrefix.size() + 7 &&
                lowerName.substr(lowerName.size() - 5) == L"c.dds") {
                const std::wstring requestedVariant = filename.substr(missingPrefix.size(), 2);
                if (requestedVariant >= L"04" && requestedVariant <= L"10" &&
                    (variant == L"01" || variant == L"02")) {
                    itemGeometryTextureAlias = L"ET_12_0107_" + variant + L"C.dds";
                    aliases.push_back(itemGeometryTextureAlias);
                }
            }
        }
    }
    const std::wstring directoryId = exact.parent_path().parent_path().filename().wstring();
    if (directoryId.size() == 4 && std::all_of(directoryId.begin(), directoryId.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        for (size_t at = 0; at + 4 <= filename.size(); ++at) {
            if (filename.compare(at, 4, directoryId) == 0) continue;
            if (!std::all_of(filename.begin() + at, filename.begin() + at + 4,
                             [](wchar_t c) { return c >= L'0' && c <= L'9'; })) continue;
            std::wstring corrected = filename;
            corrected.replace(at, 4, directoryId);
            aliases.push_back(std::move(corrected));
        }
    }
    // Some official chara VRA files retain the compact legacy BST texture key
    // BST_03_3101_01C while the sibling image is keyed by its resource path,
    // BST_0331_0101_01C. Resolve this only when the directory's category,
    // unit and variant IDs reproduce those compact tokens exactly.
    const fs::path relativePath = exact.lexically_relative(g_root);
    const std::wstring category = relativePath.parent_path().parent_path().parent_path().parent_path().filename().wstring();
    const std::wstring unit = relativePath.parent_path().parent_path().parent_path().filename().wstring();
    const std::wstring variant = relativePath.parent_path().parent_path().filename().wstring();
    const std::wstring bstPrefix = L"BST_" + category + L"_";
    if (relativePath.begin() != relativePath.end() &&
        Lower((*relativePath.begin()).wstring()) == L"chara" &&
        category.size() == 2 && unit.size() == 2 && variant.size() == 4 &&
        std::all_of(category.begin(), category.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }) &&
        std::all_of(unit.begin(), unit.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }) &&
        std::all_of(variant.begin(), variant.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }) &&
        Lower(filename).rfind(Lower(bstPrefix), 0) == 0 &&
        filename.size() > bstPrefix.size() + 5 &&
        filename[bstPrefix.size() + 4] == L'_' &&
        std::all_of(filename.begin() + bstPrefix.size(), filename.begin() + bstPrefix.size() + 4,
                    [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
        const std::wstring legacyId = filename.substr(bstPrefix.size(), 4);
        if (legacyId.substr(0, 2) == unit && legacyId.substr(2, 2) == variant.substr(2, 2)) {
            const std::wstring canonical = L"BST_" + category + unit + L"_" + variant + filename.substr(bstPrefix.size() + 4);
            aliases.push_back(canonical);
        }
        // Legacy VRA keys can name another unit; only redirect to an exact
        // same-suffix BST image named for this VRA's own resource directory.
        aliases.push_back(L"BST_" + category + unit + L"_" + variant + filename.substr(bstPrefix.size() + 4));
    }
    const std::wstring assetRootKind = relativePath.begin() != relativePath.end() ? Lower((*relativePath.begin()).wstring()) : L"";
    std::wstring mapTextureAlias;
    std::wstring mapTextureFallbackAlias;
    std::wstring fxImageAlias;
    if (Lower(relativePath.parent_path().generic_wstring()) == L"fx/image" && Lower(filename) == L"mt_05_208_02.png") {
        fxImageAlias = L"PT_05_208_02.png";
        aliases.push_back(fxImageAlias);
    }
    const std::wstring textureFolder = Lower(relativePath.parent_path().generic_wstring());
    const std::wstring lowerFilename = Lower(filename);
    if (textureFolder == L"world/field/f14/tex") {
        if (lowerFilename == L"ft_09_kn_light01.dds") mapTextureAlias = L"FT_F14_kn_light01.dds";
        else if (lowerFilename == L"ft_f14_kn_mix_ice01_guid.dds") mapTextureAlias = L"FT_F14_kn_mix_ice01.dds";
    } else if (textureFolder == L"world/field/u02/tex" && lowerFilename == L"ot_09_se_colony04.dds") {
        mapTextureAlias = L"UT_U02_se_colony04.dds";
    } else if (textureFolder == L"world/field/u09/tex" && lowerFilename == L"ut_u09_wt_dross01.dds") {
        mapTextureAlias = L"UT_U09_nk_dross01.dds";
    } else if (textureFolder == L"world/field/f16/tex") {
        if (lowerFilename == L"fz_f16_as_roada.dds") mapTextureFallbackAlias = L"f16_ha_gr_B.dds";
        else if (lowerFilename == L"fz_f16_as_grassa.dds") mapTextureFallbackAlias = L"f16_ha_gr_A.dds";
        else if (lowerFilename == L"fz_f16_as_grassb.dds") mapTextureFallbackAlias = L"f16_ha_gr_mask_A.dds";
        else if (lowerFilename == L"fz_f16_as_cliffb.dds") mapTextureFallbackAlias = L"f16_ha_wall_A.dds";
        else if (lowerFilename == L"fz_f16_as_tutia.dds") mapTextureFallbackAlias = L"f16_ha_underwarter_A.dds";
    } else if (textureFolder == L"world/field/u17/tex" && lowerFilename == L"fu_grdline01.dds") {
        mapTextureFallbackAlias = L"fu_line01.dds";
    } else if ((textureFolder == L"world/field/u17/tex" || textureFolder == L"world/object/u17/tex") && lowerFilename == L"ut_u17_to_wall01.psd") {
        mapTextureFallbackAlias = L"UT_U17_wall01.dds";
    } else if (textureFolder == L"world/object/09/tex" && lowerFilename.rfind(L"ot_09_ys_", 0) == 0) {
        mapTextureFallbackPath = g_root / L"world/object/60/tex" / (L"OT_60_ys_" + filename.substr(9));
    } else if (textureFolder == L"world/object/09/tex") {
        if (lowerFilename == L"ot_09_ko_castlemount01a.dds") mapTextureFallbackAlias = L"OT_F06_to_castle001.dds";
        else if (lowerFilename == L"ot_09_ko_road01a.dds") mapTextureFallbackAlias = L"OZ_09_as_9GXroad01A.dds";
        else if (lowerFilename == L"oz_09_framea.dds") mapTextureFallbackAlias = L"OT_09_kn_frame01.dds";
        else if (lowerFilename == L"ot_09_ns_tabuild04.dds") mapTextureFallbackAlias = L"OT_09_ns_tabuild03.dds";
    } else if (textureFolder == L"world/field/u24/tex") {
        if (lowerFilename == L"t_cliff_high_01_b.tga") mapTextureFallbackAlias = L"U24_ka_rock_a.dds";
        else if (lowerFilename == L"t_cliff_high_02_b.tga") mapTextureFallbackAlias = L"U24_ka_rock_b.dds";
    } else if (textureFolder == L"world/object/22/tex" && lowerFilename == L"ut_u09_wt_dross01.dds") {
        mapTextureFallbackPath = g_root / L"world/field/U09/tex/UT_U09_nk_dross01.dds";
    } else if (textureFolder == L"world/object/22/tex" && lowerFilename == L"ot_22_er_conta01.dds") {
        mapTextureFallbackPath = g_root / L"world/object/08/tex/OT_08_er_conta01.dds";
    } else if (textureFolder == L"world/object/22/tex" && lowerFilename == L"ft_f04_ht_wall_shadow.dds") {
        mapTextureFallbackPath = g_root / L"world/object/F12/tex/FT_F04_ht_wall_shadow.dds";
    } else if (textureFolder == L"world/object/22/tex" && lowerFilename == L"ft_f04_ht_wall06_ca.dds") {
        mapTextureFallbackPath = g_root / L"world/object/F12/tex/FT_F04_ht_wall06_ca.dds";
    } else if (textureFolder == L"world/object/22/tex" && lowerFilename == L"ft_f12_th_grd_d02.dds") {
        mapTextureFallbackPath = g_root / L"world/object/F12/tex/FT_F12_th_grd_d02.dds";
    }
    if (!mapTextureAlias.empty()) aliases.push_back(mapTextureAlias);
    if (!mapTextureFallbackAlias.empty()) aliases.push_back(mapTextureFallbackAlias);
    if (assetRootKind == L"item") {
        const std::wstring itemCategory = relativePath.parent_path().parent_path().parent_path().filename().wstring();
        const std::wstring itemId = relativePath.parent_path().parent_path().filename().wstring();
        std::wstring cleanName = filename;
        const size_t cleanExt = cleanName.find_last_of(L'.');
        if (cleanExt != std::wstring::npos) {
            size_t cleanEnd = cleanExt;
            while (cleanEnd > 0 && (cleanName[cleanEnd - 1] == L' ' || cleanName[cleanEnd - 1] == L'\t' ||
                                    cleanName[cleanEnd - 1] == L'\r' || cleanName[cleanEnd - 1] == L'\n' ||
                                    cleanName[cleanEnd - 1] == 0x3000)) --cleanEnd;
            cleanName = cleanName.substr(0, cleanEnd) + cleanName.substr(cleanExt);
        }
        const std::wstring expectedPrefix = L"EM_" + itemCategory + L"_" + itemId + L"_";
        if (itemCategory.size() == 2 && itemId.size() == 4 && Lower(cleanName).rfind(Lower(expectedPrefix), 0) == 0)
            aliases.push_back(L"ET_" + cleanName.substr(3));
        const std::wstring reversePrefix = L"ET_" + itemCategory + L"_" + itemId + L"_";
        if (itemCategory.size() == 2 && itemId.size() == 4 && Lower(cleanName).rfind(Lower(reversePrefix), 0) == 0)
            aliases.push_back(L"EM_" + cleanName.substr(3));
    }
    EnsureAssetPathIndex();
    std::vector<fs::path> aliasMatches;
    for (const auto& alias : aliases) {
        const auto foundAlias = g_assetPathsByName.find(Lower(alias));
        if (foundAlias == g_assetPathsByName.end()) continue;
        for (const auto& candidate : foundAlias->second)
            if (candidate.parent_path() == exact.parent_path() &&
                std::find(aliasMatches.begin(), aliasMatches.end(), candidate) == aliasMatches.end())
                aliasMatches.push_back(candidate);
    }
    if (aliasMatches.size() == 1) {
        if (resolution) *resolution = !itemGeometryTextureAlias.empty() &&
            Lower(aliasMatches.front().filename().wstring()) == Lower(itemGeometryTextureAlias)
                ? L"item 基础几何贴图回退" :
                (!mapTextureAlias.empty() && Lower(aliasMatches.front().filename().wstring()) == Lower(mapTextureAlias)
                    ? L"VRA 地图贴图名修正" :
                    (!mapTextureFallbackAlias.empty() && Lower(aliasMatches.front().filename().wstring()) == Lower(mapTextureFallbackAlias)
                    ? L"地图近似贴图回退" :
                    (!fxImageAlias.empty() && Lower(aliasMatches.front().filename().wstring()) == Lower(fxImageAlias)
                        ? L"FX 图片前缀修正" : L"唯一目录内规范文件名")));
        return aliasMatches.front();
    }
    if (!mapTextureFallbackPath.empty() && fs::is_regular_file(mapTextureFallbackPath)) {
        if (resolution) *resolution = mapTextureFallbackResolution;
        return mapTextureFallbackPath;
    }
    const std::wstring name = Lower(fs::path(rel).filename().wstring());
    if (name.empty()) return exact;
    EnsureAssetPathIndex();
    const auto found = g_assetPathsByName.find(name);
    if (found == g_assetPathsByName.end() || found->second.empty()) {
        // The unpacked client tree can contain a converted DDS where the VRA
        // still names its source TGA (or the reverse). Search alternate image
        // extensions only when the stem maps to one file or identical bytes.
        const std::wstring ext = Lower(fs::path(name).extension().wstring());
        if (ext != L".dds" && ext != L".png" && ext != L".tga") return exact;
        const std::wstring stem = Lower(fs::path(name).stem().wstring());
        std::vector<fs::path> alternate;
        for (const wchar_t* otherExt : {L".dds", L".png", L".tga"}) {
            const auto matches = g_assetPathsByName.find(stem + otherExt);
            if (matches == g_assetPathsByName.end()) continue;
            for (const auto& candidate : matches->second)
                if (std::find(alternate.begin(), alternate.end(), candidate) == alternate.end())
                    alternate.push_back(candidate);
        }
        fs::path alternateSelected;
        std::vector<uint8_t> alternateBytes;
        for (const auto& candidate : alternate) {
            std::vector<uint8_t> bytes;
            if (!ReadFile(candidate, bytes)) return exact;
            if (alternateSelected.empty()) {
                alternateSelected = candidate;
                alternateBytes = std::move(bytes);
            } else if (bytes != alternateBytes) {
                return exact;
            }
        }
        if (!alternateSelected.empty()) {
            if (resolution) *resolution = L"唯一同名跨格式纹理";
            return alternateSelected;
        }
        return exact;
    }

    fs::path selected;
    std::vector<uint8_t> canonical;
    for (const auto& candidate : found->second) {
        std::vector<uint8_t> bytes;
        if (!ReadFile(candidate, bytes)) return exact;
        if (selected.empty()) {
            selected = candidate;
            canonical = std::move(bytes);
        } else if (bytes != canonical) {
            return exact;
        }
    }
    if (!selected.empty() && resolution) *resolution = L"唯一名称或同名内容一致";
    return selected.empty() ? exact : selected;
}
bool AssetReferenceExists(const std::wstring& rel, std::wstring* resolution = nullptr) {
    const std::wstring ext = Lower(fs::path(rel).extension().wstring());
    if (ext == L".dds" || ext == L".png" || ext == L".tga") {
        std::error_code ec;
        return fs::is_regular_file(ResolveTexturePath(rel, resolution), ec);
    }
    if (resolution) resolution->clear();
    std::error_code ec;
    return fs::exists(AssetPath(rel), ec);
}
HMENU ControlHandle(int id) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)); }
void SetStatus(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }
bool LoadVfsModel(const std::wstring& rel, bool motion);

std::string VraFieldValue(const std::string& block, const std::string& field) {
    const std::string key = "(" + field + " ";
    const size_t at = block.find(key);
    if (at == std::string::npos) return {};
    const size_t begin = at + key.size();
    const size_t end = block.find(')', begin);
    if (end == std::string::npos) return {};
    return block.substr(begin, end - begin);
}
std::wstring VraString(const std::string& block, const std::string& field);

struct FxSpriteData {
    std::wstring imagePath;
    float width = 0.0f, height = 0.0f;
    std::wstring sourceInfo;
};

bool ParseNextFxImageNode(const std::string& text, const char* nodeType, size_t from, size_t& next, FxSpriteData& out) {
    const std::string node = std::string("{") + nodeType;
    size_t spriteAt = text.find(node, from);
    while (spriteAt != std::string::npos) {
        size_t end = spriteAt + 1;
        int depth = 1;
        bool quoted = false;
        for (; end < text.size() && depth; ++end) {
            const char c = text[end];
            if (c == '"' && (end == 0 || text[end - 1] != '\\')) quoted = !quoted;
            if (quoted) continue;
            if (c == '{') ++depth;
            else if (c == '}') --depth;
        }
        if (depth != 0) return false;
        next = end;
        const std::string block = text.substr(spriteAt, end - spriteAt);
        std::wstring imagePath = VraString(block, "file");
        if (!imagePath.empty()) {
            std::replace(imagePath.begin(), imagePath.end(), L'\\', L'/');
            std::string dims = VraFieldValue(block, "dims");
            const size_t valueAt = dims.find(']');
            if (valueAt == std::string::npos || sscanf_s(dims.c_str() + valueAt + 1, " %f , %f", &out.width, &out.height) != 2 ||
                !std::isfinite(out.width) || !std::isfinite(out.height) || out.width <= 0 || out.height <= 0) {
                spriteAt = text.find(node, end); continue;
            }
            auto field = [&](const char* name) { return FromUtf8(VraFieldValue(block, name)); };
            out.imagePath = std::move(imagePath);
            out.sourceInfo = FromUtf8(nodeType) + L"  尺寸：" + field("dims") + L"  颜色：" + field("color") +
                L"\r\nbillboard=" + field("billboard") + L"  blend=" + field("blend") +
                L"  anim_type=" + field("anim_type") + L"  anim_uv=" + field("anim_uv");
            return true;
        }
        spriteAt = text.find(node, end);
    }
    return false;
}

bool ParseFirstFxImage(const std::string& text, FxSpriteData& out) {
    // PP files may contain Sprite objects or Particle emitters. Prefer Sprite,
    // then try every Particle image if those references are absent or unreadable.
    for (const char* nodeType : {"Sprite", "Particle"}) {
        size_t from = 0, next = 0;
        FxSpriteData candidate;
        while (ParseNextFxImageNode(text, nodeType, from, next, candidate)) {
            from = next;
            std::vector<uint8_t> imageBytes;
            if (ReadFile(ResolveTexturePath(candidate.imagePath), imageBytes)) {
                out = std::move(candidate);
                return true;
            }
        }
    }
    return false;
}

bool ReadVraNodeBlock(const std::string& text, const std::string& type, size_t from, size_t& begin, size_t& end) {
    const std::string marker = "{" + type;
    begin = text.find(marker, from);
    while (begin != std::string::npos && begin + marker.size() < text.size() &&
           (std::isalnum((unsigned char)text[begin + marker.size()]) || text[begin + marker.size()] == '_'))
        begin = text.find(marker, begin + marker.size());
    if (begin == std::string::npos) return false;
    int depth = 0; bool quoted = false;
    for (end = begin; end < text.size(); ++end) {
        const char c = text[end];
        if (c == '"' && (end == 0 || text[end - 1] != '\\')) quoted = !quoted;
        if (quoted) continue;
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) { ++end; return true; }
    }
    return false;
}

std::vector<float> VraNumbers(const std::string& block, const std::string& field) {
    const std::string value = VraFieldValue(block, field);
    const size_t at = value.find(']');
    std::vector<float> out;
    if (at == std::string::npos) return out;
    const char* p = value.c_str() + at + 1;
    while (*p) {
        char* end = nullptr; const float n = strtof(p, &end);
        if (end != p) { if (std::isfinite(n)) out.push_back(n); p = end; }
        else ++p;
    }
    return out;
}

float VraNumber(const std::string& block, const std::string& field, float fallback = 0.0f) {
    const auto values = VraNumbers(block, field);
    return values.empty() ? fallback : values[0];
}

void VraVec3(const std::string& block, const std::string& field, float out[3], float fallback = 0.0f) {
    const auto values = VraNumbers(block, field);
    for (int i = 0; i < 3; ++i) out[i] = values.size() > (size_t)i ? values[(size_t)i] : fallback;
}

std::wstring VraString(const std::string& block, const std::string& field) {
    const std::string value = VraFieldValue(block, field);
    const size_t q1 = value.find('"'), q2 = q1 == std::string::npos ? q1 : value.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) return L"";
    std::string raw = value.substr(q1 + 1, q2 - q1 - 1);
    const size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return L"";
    const size_t last = raw.find_last_not_of(" \t\r\n");
    raw = raw.substr(first, last - first + 1);
    if (raw.find_first_of("/\\") != std::string::npos) {
        std::string normalized; size_t begin = 0;
        while (begin <= raw.size()) {
            const size_t sep = raw.find_first_of("/\\", begin);
            const size_t end = sep == std::string::npos ? raw.size() : sep;
            std::string part = raw.substr(begin, end - begin);
            const size_t pFirst = part.find_first_not_of(" \t\r\n"), pLast = part.find_last_not_of(" \t\r\n");
            if (pFirst != std::string::npos) normalized += part.substr(pFirst, pLast - pFirst + 1);
            if (sep == std::string::npos) break;
            normalized += raw[sep]; begin = sep + 1;
        }
        raw = std::move(normalized);
    }
    return FromUtf8(raw);
}

float EnclosingPlugTime(const std::string& text, size_t particleAt, bool start) {
    float selected = start ? 0.0f : 1.0f;
    size_t scan = 0, begin = 0, candidate = std::string::npos, smallest = std::string::npos;
    while (ReadVraNodeBlock(text, "Plug", scan, begin, candidate)) {
        if (begin < particleAt && candidate > particleAt && candidate - begin < smallest) {
            const std::string block = text.substr(begin, candidate - begin);
            const float value = VraNumber(block, start ? "start" : "end", start ? 0.0f : 1.0f);
            selected = value; smallest = candidate - begin;
        }
        scan = begin + 1;
    }
    return selected;
}

bool ParseFxNodes(const std::wstring& rel, std::vector<FxPreviewNode>& nodes, int& duration, std::vector<std::wstring>& missing) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(AssetPath(rel), bytes)) return false;
    const std::string text(bytes.begin(), bytes.end());
    size_t headerAt = text.find("{Header");
    if (headerAt != std::string::npos) {
        size_t headerEnd = text.find('}', headerAt);
        if (headerEnd != std::string::npos) duration = (std::max)(1, (int)VraNumber(text.substr(headerAt, headerEnd - headerAt + 1), "frame", 250.0f));
    }
    std::map<std::wstring, std::shared_ptr<ImageRgba>> decodedImages;
    std::set<std::wstring> failedImages;
    for (const std::string type : {"Sprite", "Particle"}) {
        size_t scan = 0, begin = 0, end = 0;
        while (ReadVraNodeBlock(text, type, scan, begin, end)) {
            scan = end;
            const std::string block = text.substr(begin, end - begin);
            FxPreviewNode node; node.particle = type == "Particle";
            node.imagePath = VraString(block, "file");
            if (node.imagePath.empty()) { missing.push_back(L"(无 file 字段)"); continue; }
            std::replace(node.imagePath.begin(), node.imagePath.end(), L'\\', L'/');
            const auto dims = VraNumbers(block, "dims");
            if (dims.size() < 2 || dims[0] <= 0 || dims[1] <= 0) { missing.push_back(node.imagePath + L" [dims 无效]"); continue; }
            node.width = dims[0]; node.height = dims[1];
            const auto color = VraNumbers(block, "color");
            for (int i = 0; i < 4; ++i) if (color.size() > (size_t)i) node.color[i] = color[(size_t)i];
            node.plugStart = EnclosingPlugTime(text, begin, true);
            node.plugEnd = EnclosingPlugTime(text, begin, false);
            node.count = (std::max)(0, (std::min)(256, (int)VraNumber(block, "emit_count", 1.0f)));
            node.cycle = (std::max)(0.25f, VraNumber(block, "emit_cycle", 1.0f));
            node.life = (std::max)(0.25f, VraNumber(block, "life", 1.0f));
            node.lifeRnd = (std::max)(0.0f, VraNumber(block, "life_rnd"));
            node.fadeIn = (std::max)(0.0f, VraNumber(block, "fade_in"));
            node.fadeInRnd = (std::max)(0.0f, VraNumber(block, "fade_in_rnd"));
            node.fadeOut = (std::max)(0.0f, VraNumber(block, "fade_out"));
            node.fadeOutRnd = (std::max)(0.0f, VraNumber(block, "fade_out_rnd"));
            node.moveVel = VraNumber(block, "move_vel"); node.moveAcl = VraNumber(block, "move_acl");
            VraVec3(block, "move", node.move);
            VraVec3(block, "pos", node.pos); VraVec3(block, "pos_rnd", node.posRnd);
            VraVec3(block, "pos_vel", node.posVel); VraVec3(block, "pos_vel_rnd", node.posVelRnd);
            VraVec3(block, "pos_acl", node.posAcl); VraVec3(block, "pos_acl_rnd", node.posAclRnd);
            VraVec3(block, "rot", node.rot); VraVec3(block, "rot_rnd", node.rotRnd);
            VraVec3(block, "rot_vel", node.rotVel); VraVec3(block, "rot_vel_rnd", node.rotVelRnd);
            VraVec3(block, "rot_acl", node.rotAcl); VraVec3(block, "rot_acl_rnd", node.rotAclRnd);
            VraVec3(block, "scale", node.scale, 1.0f); VraVec3(block, "scale_rnd", node.scaleRnd);
            VraVec3(block, "scale_vel", node.scaleVel); VraVec3(block, "scale_vel_rnd", node.scaleVelRnd);
            VraVec3(block, "scale_acl", node.scaleAcl); VraVec3(block, "scale_acl_rnd", node.scaleAclRnd);
            node.animType = (int)VraNumber(block, "anim_type");
            const auto uv = VraNumbers(block, "anim_uv");
            if (uv.size() > 0) node.animUv[0] = uv[0]; if (uv.size() > 1) node.animUv[1] = uv[1];
            auto imageIt = decodedImages.find(Lower(node.imagePath));
            if (imageIt == decodedImages.end()) {
                const std::wstring imageKey = Lower(node.imagePath);
                if (failedImages.find(imageKey) != failedImages.end()) { missing.push_back(node.imagePath); continue; }
                std::vector<uint8_t> imageBytes;
                auto decoded = std::make_shared<ImageRgba>();
                if (!ReadFile(ResolveTexturePath(node.imagePath), imageBytes) ||
                    !DecodeImage(imageBytes.data(), imageBytes.size(), node.imagePath, *decoded)) { failedImages.insert(imageKey); missing.push_back(node.imagePath); continue; }
                imageIt = decodedImages.emplace(imageKey, std::move(decoded)).first;
            }
            node.image = imageIt->second;
            nodes.push_back(std::move(node));
        }
    }
    return !nodes.empty();
}

float FxRand(unsigned seed, int axis) {
    unsigned x = seed + 0x9E3779B9u * (unsigned)(axis + 1);
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return (float)(x % 10000u) / 5000.0f - 1.0f;
}

void FxStamp(ImageRgba& canvas, const FxPreviewNode& node, float x, float y, float scale, float alpha, float angleDegrees = 0.0f) {
    if (!node.image || node.image->w <= 0 || node.image->h <= 0 || node.image->rgba.size() < (size_t)node.image->w * node.image->h * 4) return;
    const float sx = (std::max)(0.05f, fabsf(scale));
    const int dw = (std::max)(1, (int)(node.width * sx * 1.1f));
    const int dh = (std::max)(1, (int)(node.height * sx * 1.1f));
    const float radians = angleDegrees * 0.017453292519943295f;
    const float cs = cosf(radians), sn = sinf(radians);
    const int boundsW = (std::max)(1, (int)ceilf(fabsf(cs) * dw + fabsf(sn) * dh));
    const int boundsH = (std::max)(1, (int)ceilf(fabsf(sn) * dw + fabsf(cs) * dh));
    const float centerX = canvas.w * 0.5f + x, centerY = canvas.h * 0.5f - y;
    const int left = (int)floorf(centerX - boundsW * 0.5f), top = (int)floorf(centerY - boundsH * 0.5f);
    for (int py = 0; py < boundsH; ++py) for (int px = 0; px < boundsW; ++px) {
        const int tx = left + px, ty = top + py;
        if (tx < 0 || ty < 0 || tx >= canvas.w || ty >= canvas.h) continue;
        const float dx = (tx + 0.5f) - centerX, dy = (ty + 0.5f) - centerY;
        const float localX = cs * dx + sn * dy, localY = -sn * dx + cs * dy;
        const float imgX = localX / dw + 0.5f, imgY = localY / dh + 0.5f;
        if (imgX < 0.0f || imgY < 0.0f || imgX >= 1.0f || imgY >= 1.0f) continue;
        const int u = (std::min)(node.image->w - 1, (int)(imgX * node.image->w));
        const int v = (std::min)(node.image->h - 1, (int)(imgY * node.image->h));
        const size_t si = ((size_t)v * node.image->w + u) * 4, di = ((size_t)ty * canvas.w + tx) * 4;
        const float a = (node.image->rgba[si + 3] / 255.0f) * node.color[3] * alpha;
        const float sr = (node.image->rgba[si] / 255.0f) * node.color[0], sg = (node.image->rgba[si + 1] / 255.0f) * node.color[1], sb = (node.image->rgba[si + 2] / 255.0f) * node.color[2];
        const float da = canvas.rgba[di + 3] / 255.0f, oa = a + da * (1.0f - a);
        if (oa <= 0.0001f) continue;
        canvas.rgba[di] = (uint8_t)(255.0f * (sr * a + canvas.rgba[di] / 255.0f * da * (1.0f - a)) / oa);
        canvas.rgba[di + 1] = (uint8_t)(255.0f * (sg * a + canvas.rgba[di + 1] / 255.0f * da * (1.0f - a)) / oa);
        canvas.rgba[di + 2] = (uint8_t)(255.0f * (sb * a + canvas.rgba[di + 2] / 255.0f * da * (1.0f - a)) / oa);
        canvas.rgba[di + 3] = (uint8_t)(oa * 255.0f);
    }
}

void RenderFxFrame() {
    if (!g_fxSpritePreview || g_fxNodes.empty()) return;
    ImageRgba canvas; canvas.w = 800; canvas.h = 600; canvas.format = "PP frame preview"; canvas.rgba.assign((size_t)canvas.w * canvas.h * 4, 0);
    const float frame = (float)g_fxCurrentFrame;
    unsigned drawn = 0;
    for (size_t n = 0; n < g_fxNodes.size(); ++n) {
        const auto& node = g_fxNodes[n];
        const float gateBegin = node.plugStart * g_fxDuration, gateEnd = node.plugEnd * g_fxDuration;
        if (frame < gateBegin) continue;
        if (!node.particle) {
            if (frame > gateEnd) continue;
            FxStamp(canvas, node, node.pos[0] * 1.2f, node.pos[1] * 1.2f,
                    (node.scale[0] + node.scale[1]) * 0.5f, 1.0f, node.rot[2]);
            continue;
        }
        const float interval = (std::max)(1.0f, node.cycle);
        const float emissionEnd = (std::max)(gateBegin, gateEnd);
        const int batches = (std::min)(256, (int)((emissionEnd - gateBegin) / interval) + 1);
        for (int b = 0; b < batches && drawn < 2500; ++b) {
            const float batchTime = gateBegin + b * interval;
            if (batchTime > frame) break;
            for (int p = 0; p < node.count && drawn < 2500; ++p) {
                const unsigned seed = (unsigned)(n * 65537 + b * 257 + p);
                const float age = frame - batchTime - (FxRand(seed, 12) * node.lifeRnd * 0.5f);
                const float life = (std::max)(0.25f, node.life + FxRand(seed, 13) * node.lifeRnd * 0.5f);
                if (age < 0.0f || age >= life) continue;
                const float t = age;
                const float px = node.pos[0] + FxRand(seed, 0) * node.posRnd[0] + (node.posVel[0] + FxRand(seed, 3) * node.posVelRnd[0] + node.move[0] * node.moveVel) * t + 0.5f * (node.posAcl[0] + FxRand(seed, 6) * node.posAclRnd[0] + node.move[0] * node.moveAcl) * t * t;
                const float py = node.pos[1] + FxRand(seed, 1) * node.posRnd[1] + (node.posVel[1] + FxRand(seed, 4) * node.posVelRnd[1] + node.move[1] * node.moveVel) * t + 0.5f * (node.posAcl[1] + FxRand(seed, 7) * node.posAclRnd[1] + node.move[1] * node.moveAcl) * t * t;
                const float sx = node.scale[0] + FxRand(seed, 9) * node.scaleRnd[0] + (node.scaleVel[0] + FxRand(seed, 10) * node.scaleVelRnd[0]) * t + 0.5f * (node.scaleAcl[0] + FxRand(seed, 11) * node.scaleAclRnd[0]) * t * t;
                const float angle = node.rot[2] + FxRand(seed, 16) * node.rotRnd[2] +
                    (node.rotVel[2] + FxRand(seed, 17) * node.rotVelRnd[2]) * t +
                    0.5f * (node.rotAcl[2] + FxRand(seed, 18) * node.rotAclRnd[2]) * t * t;
                float opacity = 1.0f;
                const float fadeIn = node.fadeIn + fabsf(FxRand(seed, 14)) * node.fadeInRnd;
                const float fadeOut = node.fadeOut + fabsf(FxRand(seed, 15)) * node.fadeOutRnd;
                if (fadeIn > 0.001f) opacity = (std::min)(opacity, (std::min)(1.0f, age / (life * fadeIn)));
                if (fadeOut > 0.001f) opacity = (std::min)(opacity, (std::min)(1.0f, (life - age) / (life * fadeOut)));
                FxStamp(canvas, node, px * 1.2f, py * 1.2f, sx, opacity, angle); ++drawn;
            }
        }
    }
    g_imageData = std::move(canvas);
    if (g_imageBitmap) DeleteObject(g_imageBitmap);
    g_imageBitmap = ImageToBitmap(g_imageData);
    InvalidateRect(g_imageView, nullptr, TRUE);
}

bool ReadFxSprite(const std::wstring& rel, FxSpriteData& sprite, std::vector<uint8_t>& imageBytes) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(AssetPath(rel), bytes)) return false;
    const std::string text(bytes.begin(), bytes.end());
    return ParseFirstFxImage(text, sprite) && ReadFile(ResolveTexturePath(sprite.imagePath), imageBytes);
}

bool FirstFxModelGeometry(const std::wstring& rel, std::wstring& geometry) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(AssetPath(rel), bytes)) return false;
    const std::string text(bytes.begin(), bytes.end());
    size_t scan = 0, begin = 0, end = 0;
    while (ReadVraNodeBlock(text, "Model", scan, begin, end)) {
        scan = end;
        const std::wstring attrPath = VraString(text.substr(begin, end - begin), "file");
        if (attrPath.empty()) continue;
        std::vector<uint8_t> attrBytes;
        if (!ReadFile(AssetPath(attrPath), attrBytes)) continue;
        VraDoc attr;
        if (attr.Parse((const char*)attrBytes.data(), attrBytes.size()) && !attr.geometry.empty()) {
            geometry = FromUtf8(attr.geometry);
            return true;
        }
    }
    return false;
}

bool LoadFxSpritePreview(const std::wstring& rel) {
    g_fxNodes.clear(); g_fxMissingTextures.clear(); g_fxDuration = 250; g_fxCurrentFrame = 0; g_fxPath = rel;
    if (!ParseFxNodes(rel, g_fxNodes, g_fxDuration, g_fxMissingTextures)) return false;
    g_fxSpriteInfo = L"PP 时间点预览  |  " + rel + L"\r\n" +
        std::to_wstring(g_fxNodes.size()) + L" 个有可读取贴图的 Sprite/Particle；时间单位使用 VRA Header.frame 帧。\r\n" +
        L"客户端依据：粒子池按 emit_count/emit_cycle/life 生成，九组三维随机曲线写入每个粒子；本视图近似模拟发射、寿命、位置/速度/加速度、缩放和淡入淡出。\r\n" +
        L"仍未复现客户端随机种子、精确 plug 事件时序、billboard/旋转、UV 动画与 blend 模式。用下方时间轴检查。";
    if (!g_fxMissingTextures.empty()) {
        g_fxSpriteInfo += L"\r\n\r\n未解析贴图 " + std::to_wstring(g_fxMissingTextures.size()) + L" 个：";
        for (size_t i = 0; i < (std::min)(size_t(6), g_fxMissingTextures.size()); ++i) g_fxSpriteInfo += L"\r\n" + g_fxMissingTextures[i];
        if (g_fxMissingTextures.size() > 6) g_fxSpriteInfo += L"\r\n…";
    }
    g_fxSpritePreview = true;
    if (g_fxTimeline) { SendMessageW(g_fxTimeline, TBM_SETRANGEMIN, FALSE, 0); SendMessageW(g_fxTimeline, TBM_SETRANGEMAX, TRUE, g_fxDuration); SendMessageW(g_fxTimeline, TBM_SETTICFREQ, (std::max)(1, g_fxDuration / 10), 0); SendMessageW(g_fxTimeline, TBM_SETPOS, TRUE, 0); ShowWindow(g_fxTimeline, SW_SHOW); }
    RenderFxFrame();
    ShowWindow(g_view, SW_HIDE);
    ShowWindow(g_imageView, SW_SHOW);
    InvalidateRect(g_imageView, nullptr, TRUE);
    SetStatus(L"已读取 PP 中全部可解析的 Sprite/Particle 贴图；拖动时间轴查看粒子发射与运动近似效果。");
    return true;
}

void RefreshBoneChoices() {
    if(!g_attachBone)return;
    SendMessageW(g_attachBone,CB_RESETCONTENT,0,0);
    SendMessageW(g_attachBone,CB_ADDSTRING,0,(LPARAM)L"场景固定");
    for(unsigned i=0;i<Preview3D_BoneCount();++i) {
        const char* name=Preview3D_BoneName(i); if(!name)continue;
        const std::wstring wide=FromUtf8(name);
        SendMessageW(g_attachBone,CB_ADDSTRING,0,(LPARAM)wide.c_str());
    }
    SendMessageW(g_attachBone,CB_SETCURSEL,0,0);
}

void LoadBindings() {
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH);
    const fs::path base=fs::path(exe).parent_path();
    auto loadCsv = [](const fs::path& p, auto fn) {
        std::ifstream f(p, std::ios::binary); std::string line;
        while (std::getline(f, line)) if (!line.empty() && line[0] != '#') fn(Split(line));
    };
    loadCsv(base/L"ms_bind.csv", [](const auto& c) {
        if (c.size() < 5) return; Unit u; u.id=c[0]; u.name=c[1]; u.modelId=c[2]; u.dir=c[3]; u.model=c[4];
        if (c.size()>8) u.wiki=c[8]; g_units[u.id]=u;
    });
    loadCsv(base/L"ms_weapons.csv", [](const auto& c) {
        if (c.size() < 7) return; Weapon w; w.id=c[1]; w.slot=c[2]; w.dir=c[3]; w.model=c[5];
        if(c.size()>6) w.attr=c[6]; if(c.size()>7) w.name=c[7]; if(c.size()>8) w.basis=c[8];
        g_weapons[c[0]].push_back(w);
    });
}

fs::path ProfilePath() { return g_profileDir / (g_unit.id + L".ini"); }
std::wstring ProfileTemplate() {
    std::wstring s = L"# Gundam machine console experiment profile\r\n"
                     L"# Values here are local experiments; source/state fields must be recorded.\r\n"
                     L"[unit]\r\n";
    s += L"id=" + g_unit.id + L"\r\nname=" + g_unit.name + L"\r\nmodel_id=" + g_unit.modelId + L"\r\n";
    s += L"[preview]\r\nmodel=" + g_unit.dir + L"/model/" + g_unit.model + L"\r\nmotion=\r\n";
    s += L"[parameters]\r\n# key=value|unit|source|state|note\r\n"
         L"mass=|||unknown|\r\nheight=|||unknown|\r\nwalk_speed=|||unknown|\r\nboost_speed=|||unknown|\r\n"
         L"boost_capacity=|||unknown|\r\njump_power=|||unknown|\r\nshield_hp=|||unknown|\r\n";
    s += L"[weapons]\r\n# Manual IDs only. Candidate CSV rows are shown as references and never mounted automatically.\r\n"
         L"slot1_id=|manual|unknown|\r\nslot2_id=|manual|unknown|\r\nslot3_id=|manual|unknown|\r\n";
    s += L"[icons]\r\n# The UI, battle HUD, and kill log have separate resource ID fields.\r\n"
         L"slot1_battle=|manual|unknown|\r\nslot2_battle=|manual|unknown|\r\nslot3_battle=|manual|unknown|\r\n"
         L"slot1_killlog=|manual|unknown|\r\nslot2_killlog=|manual|unknown|\r\nslot3_killlog=|manual|unknown|\r\n";
    s += L"[traits]\r\n# key=value|source|state|note\r\n"
         L"ability1=|wiki|unknown|\r\nability2=|wiki|unknown|\r\nadditional1=|wiki|unknown|\r\nadditional2=|wiki|unknown|\r\n";
    s += L"[effects]\r\n# role=resource_path|source|state|note\r\n"
         L"boost_start=|manual|unknown|\r\nboost_loop=|manual|unknown|\r\nboost_overheat=|manual|unknown|\r\n"
         L"projectile=|manual|unknown|\r\nimpact=|manual|unknown|\r\n";
    s += L"[mounts]\r\n# mount_N=asset_path|position_xyz|rotation_xyz|uniform_scale|manual|candidate|\r\n";
    return s;
}
void LoadProfile() {
    if (g_unit.id.empty()) return;
    std::error_code ec; fs::create_directories(g_profileDir, ec);
    std::ifstream f(ProfilePath(), std::ios::binary);
    if (!f) { SetWindowTextW(g_profile, ProfileTemplate().c_str()); return; }
    std::string bytes((std::istreambuf_iterator<char>(f)), {});
    int n=MultiByteToWideChar(CP_UTF8,0,bytes.data(),(int)bytes.size(),nullptr,0);
    std::wstring wide(n>0?(size_t)n:0,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,0,bytes.data(),(int)bytes.size(),wide.data(),n);
    SetWindowTextW(g_profile, wide.c_str());
}

std::wstring ProfileValue(const std::wstring& wantedSection,const std::wstring& wantedKey) {
    const std::wstring profile=ReadEdit(g_profile);std::wstring section;size_t start=0;
    while(start<profile.size()) {
        size_t end=profile.find_first_of(L"\r\n",start);if(end==std::wstring::npos)end=profile.size();
        const std::wstring line=Trim(profile.substr(start,end-start));start=end+1;
        if(line.empty()||line[0]==L'#')continue;
        if(line.front()==L'['&&line.back()==L']'){section=Lower(line.substr(1,line.size()-2));continue;}
        if(section!=Lower(wantedSection))continue;
        const size_t eq=line.find(L'=');if(eq==std::wstring::npos)continue;
        if(Trim(line.substr(0,eq))==wantedKey)return Trim(line.substr(eq+1));
    }
    return L"";
}

std::wstring ProfileFieldValue(const std::wstring& section, const std::wstring& key) {
    std::wstring value = ProfileValue(section, key);
    const size_t split = value.find(L'|');
    if (split != std::wstring::npos) value.resize(split);
    value = Trim(value);
    return value.empty() ? L"未确认" : value;
}

void LoadProfileFields() {
    for (int i = 0; i < 25; ++i) {
        const std::wstring section = i < 7 ? L"parameters" : i < 11 ? L"traits" : i < 14 ? L"weapons" : i < 20 ? L"icons" : L"effects";
        SetWindowTextW(g_fieldEdits[i], ProfileFieldValue(section, g_fieldKeys[i]).c_str());
    }
}

std::wstring ReplaceProfileField(std::wstring profile, const std::wstring& sectionWanted,
                                 const std::wstring& keyWanted, const std::wstring& fieldValue) {
    std::wstring section; size_t start = 0; bool replaced = false;
    while (start < profile.size()) {
        size_t end = profile.find_first_of(L"\r\n", start); if (end == std::wstring::npos) end = profile.size();
        std::wstring line = profile.substr(start, end - start);
        if (!line.empty() && line.front() == L'[' && line.back() == L']') section = Lower(line.substr(1, line.size() - 2));
        else if (Lower(section) == Lower(sectionWanted)) {
            const size_t eq = line.find(L'=');
            if (eq != std::wstring::npos && Trim(line.substr(0, eq)) == keyWanted) {
                const std::wstring oldValue = line.substr(eq + 1); const size_t pipe = oldValue.find(L'|');
                const std::wstring suffix = fieldValue.empty() ? (pipe == std::wstring::npos ? L"|manual|unknown|" : oldValue.substr(pipe)) : L"|manual|candidate|";
                profile.replace(start + eq + 1, oldValue.size(), fieldValue + suffix);
                replaced = true; break;
            }
        }
        start = end + 1;
    }
    if (!replaced) {
        const std::wstring header = L"[" + sectionWanted + L"]";
        const size_t sectionAt = Lower(profile).find(L"[" + Lower(sectionWanted) + L"]");
        if (sectionAt == std::wstring::npos) profile += L"\r\n" + header + L"\r\n" + keyWanted + L"=" + fieldValue + L"|manual|candidate|\r\n";
        else {
            size_t insertAt = profile.find_first_of(L"\r\n", sectionAt);
            if (insertAt == std::wstring::npos) profile += L"\r\n" + keyWanted + L"=" + fieldValue + L"|manual|candidate|\r\n";
            else profile.insert(insertAt + 2, keyWanted + L"=" + fieldValue + L"|manual|candidate|\r\n");
        }
    }
    return profile;
}

void SyncProfileFields() {
    std::wstring profile = ReadEdit(g_profile);
    for (int i = 0; i < 25; ++i) {
        std::wstring value = ReadEdit(g_fieldEdits[i]);
        if (value == L"未确认") value.clear();
        const std::wstring section = i < 7 ? L"parameters" : i < 11 ? L"traits" : i < 14 ? L"weapons" : i < 20 ? L"icons" : L"effects";
        profile = ReplaceProfileField(std::move(profile), section, g_fieldKeys[i], value);
    }
    SetWindowTextW(g_profile, profile.c_str());
}

void UpdateUnitCards() {
    if (g_unit.id.empty()) {
        SetWindowTextW(g_unitTitle, L"等待载入机体");
        SetWindowTextW(g_unitMeta, L"选择 unpackdat 目录并输入机体编号");
        SetWindowTextW(g_unitCard, L"机体数据\r\n模型编号：—\r\n模型资源：—\r\n数值来源：待载入");
        SetWindowTextW(g_weaponCard, L"装备槽 / 武器绑定\r\n尚无机体数据");
        SetWindowTextW(g_sourceCard, L"证据状态\r\nWiki 数值：待载入\r\n解包资源：待选择目录\r\n未确认参数不会自动补值");
        return;
    }
    SetWindowTextW(g_unitTitle, g_unit.name.empty() ? L"未命名机体" : g_unit.name.c_str());
    SetWindowTextW(g_unitMeta, (L"机体编号  " + g_unit.id + L"     模型编号  " + g_unit.modelId).c_str());
    const std::wstring modelPath = (fs::path(g_unit.dir) / L"model" / g_unit.model).wstring();
    const std::wstring values = L"机体与实验参数\r\n机体编号：" + g_unit.id + L"\r\n模型编号：" + g_unit.modelId +
        L"\r\n模型资源：" + modelPath + L"\r\n\r\n质量：" + ProfileValue(L"parameters",L"mass") +
        L"\r\n高度：" + ProfileValue(L"parameters",L"height") + L"\r\n步行速度：" + ProfileValue(L"parameters",L"walk_speed") +
        L"\r\n推进速度：" + ProfileValue(L"parameters",L"boost_speed") + L"\r\n推进容量：" + ProfileValue(L"parameters",L"boost_capacity");
    SetWindowTextW(g_unitCard, values.c_str());
    std::wstring weapons = L"武器参考记录（CSV 候选，不自动挂载）\r\n实际预览由手动武器 ID 控制\r\n\r\n";
    const auto it = g_weapons.find(g_unit.id);
    if (it == g_weapons.end() || it->second.empty()) weapons += L"无已记录绑定\r\n";
    else for (const auto& w : it->second) {
        weapons += w.slot + L"  " + (w.name.empty() ? L"未命名武器" : w.name) + L"\r\n";
        weapons += L"ID " + w.id + L"    " + w.dir + L"\r\n";
        weapons += L"模型 " + (w.model.empty() ? L"未记录" : w.model) + L"    数值来源 " + (w.basis.empty() ? L"未记录" : w.basis) + L"\r\n\r\n";
    }
    SetWindowTextW(g_weaponCard, weapons.c_str());
    SetWindowTextW(g_sourceCard, (L"证据状态\r\nWiki 行：" + (g_unit.wiki.empty() ? L"未匹配" : g_unit.wiki) +
        L"\r\n模型资源：" + modelPath + L"\r\n参数可信度：以档案中的 unknown / candidate / verified 标记为准\r\n\r\n资源关联只展示已有绑定，不根据文件名推断数值。").c_str());
}

void SaveProfile() {
    if (g_unit.id.empty()) return;
    const std::wstring contents=ReadEdit(g_profile);
    if (contents.find(L"id="+g_unit.id)==std::wstring::npos) {
        MessageBoxW(g_main,L"档案中的 [unit] id 必须与当前机体一致。",L"保存失败",MB_ICONWARNING); return;
    }
    int n=WideCharToMultiByte(CP_UTF8,0,contents.data(),(int)contents.size(),nullptr,0,nullptr,nullptr);
    std::string bytes((size_t)n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,0,contents.data(),(int)contents.size(),bytes.data(),n,nullptr,nullptr);
    std::error_code ec; fs::create_directories(g_profileDir,ec);
    std::ofstream f(ProfilePath(),std::ios::binary|std::ios::trunc); f.write(bytes.data(),(std::streamsize)bytes.size());
    SetStatus(L"实验档案已保存："+ProfilePath().wstring());
}

void AddFiles(const fs::path& dir, int kind, const wchar_t* labelPrefix) {
    std::error_code ec; if (!fs::exists(dir,ec)) return;
    for (auto it=fs::recursive_directory_iterator(dir,fs::directory_options::skip_permission_denied,ec);
         it!=fs::recursive_directory_iterator(); it.increment(ec)) {
        if(ec){ec.clear();continue;} if(!it->is_regular_file(ec)) continue;
        const auto p=it->path(); const auto ext=Lower(p.extension().wstring());
        const bool wanted = kind==0 ? (ext==L".dxg"||ext==L".txg") :
                            kind==1 ? ext==L".mrb" :
                            kind==2 ? (ext==L".dxg"||ext==L".vra"||ext==L".dds"||ext==L".png"||ext==L".tga") :
                                      (ext==L".dxg"||ext==L".vra"||ext==L".dds"||ext==L".png"||ext==L".tga");
        if (!wanted) continue;
        std::wstring rel=p.lexically_relative(g_root).wstring();
        std::replace(rel.begin(),rel.end(),L'\\',L'/');
        const auto existing=std::find_if(g_assetsForUnit.begin(),g_assetsForUnit.end(),[&](const Asset&a){return Lower(a.path)==Lower(rel);});
        if(existing!=g_assetsForUnit.end())continue;
        g_assetsForUnit.push_back({rel,std::wstring(labelPrefix)+rel,kind});
    }
}

void AddFxCatalog() {
    std::ifstream file(AssetPath(L"settings/fx.csv"), std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto columns = Split(line);
        if (columns.size() < 3 || columns[0].empty() || columns[1].empty() || columns[2].empty()) continue;
        std::wstring path = columns[2];
        std::replace(path.begin(), path.end(), L'\\', L'/');
        if (Lower(path).rfind(L"fx/", 0) != 0 || Lower(fs::path(path).extension().wstring()) != L".vra") continue;
        std::wstring note = columns.size() > 3 ? Trim(columns[3]) : L"";
        if (!note.empty() && note.front() == L'#') note = Trim(note.substr(1));
        const std::wstring catalog = L"  [fx.csv 类别=" + columns[0] + L" 唯一ID=" + columns[1] + L"]" +
                                     (note.empty() ? L"" : L"  " + note);
        auto existing = std::find_if(g_assetsForUnit.begin(), g_assetsForUnit.end(),
            [&](const Asset& asset) { return Lower(asset.path) == Lower(path); });
        if (existing == g_assetsForUnit.end()) {
            const bool found = fs::exists(AssetPath(path));
            g_assetsForUnit.push_back({path, L"fx.csv 资源  " + path + catalog + (found ? L"" : L"  [资源缺失]"), 3});
        } else {
            existing->label += catalog;
        }
    }
}

void RefreshAssets(const std::wstring& selectPath = L"") {
    g_visible.clear(); SendMessageW(g_assets,LB_RESETCONTENT,0,0);
    const int category=(int)SendMessageW(g_kind,CB_GETCURSEL,0,0);
    const std::wstring needle = Lower(Trim(ReadEdit(g_assetSearch)));
    int selectIndex = -1;
    for(const auto&a:g_assetsForUnit) if(category<4 && a.kind==category) {
        if(!needle.empty()&&Lower(a.path).find(needle)==std::wstring::npos&&Lower(a.label).find(needle)==std::wstring::npos)continue;
        if(!selectPath.empty()&&Lower(a.path)==Lower(selectPath))selectIndex=(int)g_visible.size();
        g_visible.push_back(a); SendMessageW(g_assets,LB_ADDSTRING,0,(LPARAM)a.label.c_str());
    }
    if(selectIndex>=0)SendMessageW(g_assets,LB_SETCURSEL,selectIndex,0);
}

void SetTab(int tab) {
    g_activeTab = tab;
    if (tab == 1 && !g_itemIndexed && !g_root.empty()) {
        AddFiles(g_root / L"item", 2, L"Item  ");
        g_itemIndexed = true;
    }
    if (tab == 4 && !g_fxIndexed && !g_root.empty()) {
        AddFiles(g_root / L"fx", 3, L"客户端 FX 资源  ");
        AddFxCatalog();
        g_fxIndexed = true;
    }
    const int kind = tab == 1 ? 2 : tab == 3 ? 1 : tab == 4 ? 3 : 0;
    SendMessageW(g_kind, CB_SETCURSEL, kind, 0);
    RefreshAssets();
    const bool editProfile = tab == 1 || tab == 2 || tab == 4 || tab == 5;
    ShowWindow(g_profile, SW_HIDE);
    ShowWindow(GetDlgItem(g_main, ID_SAVE), editProfile ? SW_SHOW : SW_HIDE);
    ShowWindow(g_unitCard, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_weaponCard, tab == 0 || tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_sourceCard, tab == 0 || tab == 3 || tab == 4 ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < 25; ++i) {
        const bool visible = (tab == 1 && i >= 11 && i < 20) ||
                             ((tab == 2 || tab == 5) && i < 11) ||
                             (tab == 4 && i >= 20);
        ShowWindow(g_fieldLabels[i], visible ? SW_SHOW : SW_HIDE);
        ShowWindow(g_fieldEdits[i], visible ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(g_main, nullptr, TRUE);
}

void AddProfileEffects() {
    const std::wstring profile=ReadEdit(g_profile);
    bool inEffects=false,inWeapons=false;
    size_t start=0;
    while(start<profile.size()) {
        size_t end=profile.find_first_of(L"\r\n",start); if(end==std::wstring::npos)end=profile.size();
        std::wstring line=Trim(profile.substr(start,end-start));
        start=end+1;
        if(line.empty()||line[0]==L'#')continue;
        if(line.front()==L'['&&line.back()==L']') {
            const auto section=Lower(line);
            inEffects=section==L"[effects]";inWeapons=section==L"[weapons]";continue;
        }
        if(!inEffects&&!inWeapons)continue;
        const size_t eq=line.find(L'='); if(eq==std::wstring::npos)continue;
        std::wstring value=line.substr(eq+1); size_t part=0;
        while(part<value.size()) {
            size_t sep=value.find(L'|',part);if(sep==std::wstring::npos)sep=value.size();
            std::wstring candidate=Trim(value.substr(part,sep-part));
            const bool numericId = !candidate.empty() && std::all_of(candidate.begin(), candidate.end(), [](wchar_t c){return c>=L'0'&&c<=L'9';});
            if (inEffects && numericId) {
                g_assetsForUnit.push_back({L"effect-id:"+candidate,L"手动特效 ID  "+candidate+L"  [映射/运行方式待验证]",3});
                break;
            }
            if((candidate.find(L'/')!=std::wstring::npos||candidate.find(L'\\')!=std::wstring::npos)&&
               (Lower(candidate).rfind(L"fx/",0)==0||Lower(candidate).find(L"/fx/")!=std::wstring::npos)) {
                std::replace(candidate.begin(),candidate.end(),L'\\',L'/');
                const std::wstring prefix=inEffects?L"特效档案  ":L"弹药/武器档案  ";
                const bool exists=fs::exists(AssetPath(candidate));
                g_assetsForUnit.push_back({candidate,prefix+candidate+(exists?L"":L"  [资源缺失]"),3});
                if(exists) {
                    std::vector<std::wstring> queue{candidate};std::set<std::wstring> seen{Lower(candidate)};
                    for(size_t qi=0;qi<queue.size()&&queue.size()<256;++qi) {
                        std::vector<uint8_t> bytes;if(!ReadFile(AssetPath(queue[qi]),bytes))continue;
                        std::string raw(bytes.begin(),bytes.end());
                        for(size_t pos=0;pos<raw.size();) {
                            const size_t lineEnd=raw.find('\n',pos);const size_t end=lineEnd==std::string::npos?raw.size():lineEnd;
                            const std::string line=raw.substr(pos,end-pos);pos=end+1;
                            const size_t tag=line.find("string");if(tag==std::string::npos)continue;
                            const size_t q1=line.find('"',tag),q2=q1==std::string::npos?q1:line.find('"',q1+1);
                            if(q1==std::string::npos||q2==std::string::npos)continue;
                            const std::string value=line.substr(q1+1,q2-q1-1);
                            std::wstring ref=FromUtf8(value);std::wstring low=Lower(ref);
                            if(!(low.find(L".vra")!=std::wstring::npos||low.find(L".dxg")!=std::wstring::npos||
                                 low.find(L".dds")!=std::wstring::npos||low.find(L".png")!=std::wstring::npos||
                                 low.find(L".tga")!=std::wstring::npos))continue;
                            std::replace(ref.begin(),ref.end(),L'\\',L'/');
                            if(seen.insert(Lower(ref)).second) {
                            std::wstring resolution;
                            const bool found=AssetReferenceExists(ref,&resolution);
                            g_assetsForUnit.push_back({ref,L"引用资源  "+ref+(found?(resolution.empty()?L"":L"  [同名安全恢复]"):L"  [资源缺失]"),3});
                                if(found&&Lower(fs::path(ref).extension().wstring())==L".vra"&&queue[qi].size()<280)
                                    queue.push_back(ref);
                            }
                        }
                        VraDoc doc;
                        if(doc.Parse((const char*)bytes.data(),bytes.size())) {
                            if(!doc.geometry.empty()) {
                                std::wstring geo(doc.geometry.begin(),doc.geometry.end());
                                if(seen.insert(Lower(geo)).second)g_assetsForUnit.push_back({geo,L"引用模型  "+geo,3});
                            }
                            for(const auto&mat:doc.materials) for(const auto* ref:{&mat.texture,&mat.mixTexture}) if(!ref->empty()) {
                                std::wstring tex(ref->begin(),ref->end());
                                if(seen.insert(Lower(tex)).second) {
                                    std::wstring resolution;
                                    const bool found=AssetReferenceExists(tex,&resolution);
                                    g_assetsForUnit.push_back({tex,L"引用贴图  "+tex+(found?(resolution.empty()?L"":L"  [同名安全恢复]"):L"  [资源缺失]"),3});
                                }
                            }
                        }
                    }
                }
                break;
            }
            part=sep+1;
        }
    }
}

void AddProfileWeapons() {
    for (int slot = 1; slot <= 3; ++slot) {
        const std::wstring id = ProfileFieldValue(L"weapons", L"slot" + std::to_wstring(slot) + L"_id");
        if (id == L"未确认") continue;
        const bool digits = !id.empty() && std::all_of(id.begin(), id.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        if (!digits || id.size() != 7) {
            g_assetsForUnit.push_back({id, L"槽位 " + std::to_wstring(slot) + L" 手动 ID：" + id + L"  [当前仅支持已核实的 7 位模型 ID]", 2});
            continue;
        }
        std::wstring category = id.substr(0, 1); if (category.size() < 2) category.insert(category.begin(), L'0');
        const std::wstring serial = id.substr(1, 4), variant = id.substr(5, 2);
        const std::wstring dir = L"item/" + category + L"/" + serial;
        const std::wstring modelName = L"EM_" + category + L"_" + serial + L"_" + variant + L".dxg";
        const fs::path base = AssetPath(dir);
        AddFiles(base/L"model",2,(L"槽位 "+std::to_wstring(slot)+L" 手动武器 ").c_str());
        AddFiles(base/L"attr",2,(L"槽位 "+std::to_wstring(slot)+L" 手动武器 ").c_str());
        AddFiles(base/L"tex",2,(L"槽位 "+std::to_wstring(slot)+L" 武器贴图 ").c_str());
        const fs::path modelPath = base / L"model" / modelName;
        g_assetsForUnit.push_back({modelPath.lexically_relative(g_root).wstring(), L"槽位 " + std::to_wstring(slot) + L" 手动模型 " + modelName + (fs::exists(modelPath) ? L"" : L"  [无独立模型资源]"), 2});
    }
}

void AddProfileIcons() {
    auto addIcon = [&](const std::wstring& rel, const std::wstring& label) {
        const bool exists = fs::exists(AssetPath(rel));
        g_assetsForUnit.push_back({rel, label + (exists ? L"" : L"  [资源缺失]"), 3});
    };
    auto pad8 = [](std::wstring id) { while (id.size() < 8) id.insert(id.begin(), L'0'); return id; };
    for (int slot = 1; slot <= 3; ++slot) {
        const std::wstring weaponId = ProfileFieldValue(L"weapons", L"slot" + std::to_wstring(slot) + L"_id");
        if (weaponId != L"未确认" && weaponId.size() <= 8 && std::all_of(weaponId.begin(), weaponId.end(), [](wchar_t c){return c>=L'0'&&c<=L'9';})) {
            const auto id = pad8(weaponId);
            addIcon(L"interface/icon/item/game/m/" + id + L".dds", L"槽位 " + std::to_wstring(slot) + L" UI 图标  ");
            addIcon(L"interface/icon/item/d/d" + id + L".dds", L"槽位 " + std::to_wstring(slot) + L" 详情图标  ");
            addIcon(L"interface/icon/item/s/s" + id + L".dds", L"槽位 " + std::to_wstring(slot) + L" 简易图标  ");
        }
        const std::wstring battle = ProfileFieldValue(L"icons", L"slot" + std::to_wstring(slot) + L"_battle");
        if (battle != L"未确认" && battle.size() <= 8 && std::all_of(battle.begin(), battle.end(), [](wchar_t c){return c>=L'0'&&c<=L'9';}))
            addIcon(L"interface/icon/battle/b/b" + pad8(battle) + L".dds", L"槽位 " + std::to_wstring(slot) + L" 战斗图标  ");
        const std::wstring kill = ProfileFieldValue(L"icons", L"slot" + std::to_wstring(slot) + L"_killlog");
        if (kill != L"未确认" && kill.size() <= 8 && std::all_of(kill.begin(), kill.end(), [](wchar_t c){return c>=L'0'&&c<=L'9';}))
            addIcon(L"interface/icon/battle/killlog/" + pad8(kill) + L".dds", L"槽位 " + std::to_wstring(slot) + L" Killlog 图标  ");
    }
}

bool LoadSceneObject(const SceneObject& object, const std::wstring& fieldCode,
                     float centerX, float centerY, float centerZ, size_t ordinal,
                     size_t& sceneMissingTextureRefs, size_t& sceneRecoveredTextureRefs) {
    const std::wstring category = FromUtf8(object.category);
    const std::wstring name = FromUtf8(object.name);
    const bool fieldAsset = Lower(category) == Lower(fieldCode);
    const std::wstring attrRel = fieldAsset
        ? L"world/field/" + category + L"/attr/" + name + L".vra"
        : L"world/object/" + category + L"/attr/" + name + L".vra";
    std::vector<uint8_t> attrBytes;
    VraDoc attrDoc;
    if (!ReadFile(AssetPath(attrRel), attrBytes) ||
        !attrDoc.Parse((const char*)attrBytes.data(), attrBytes.size())) return false;
    std::wstring modelRel;
    if (!attrDoc.geometry.empty()) {
        modelRel = FromUtf8(attrDoc.geometry);
    } else if (fieldAsset && name.rfind(L"FZ_", 0) == 0) {
        // Field terrain VRA stores a mesh/material table, not a Geometry path.
        // The unpacked F01 pair confirms FZ_<name>.vra -> FM_<name>.dxg.
        std::wstring modelName = name;
        modelName.replace(0, 3, L"FM_");
        modelRel = L"world/field/" + category + L"/model/" + modelName + L".dxg";
    } else {
        return false;
    }
    std::vector<uint8_t> modelBytes;
    if (!ReadFile(AssetPath(modelRel), modelBytes)) return false;

    std::vector<std::vector<uint8_t>> textureBytes;
    std::vector<Preview3DTex> textures;
    size_t missingTextureRefs = 0;
    size_t recoveredTextureRefs = 0;
    textureBytes.reserve(attrDoc.materials.size());
    textures.reserve(attrDoc.materials.size());
    for (const auto& material : attrDoc.materials) for (const auto* ref : {&material.texture, &material.mixTexture}) {
        if (ref->empty()) continue;
        std::wstring texRel = FromUtf8(*ref);
        std::replace(texRel.begin(), texRel.end(), L'\\', L'/');
        std::wstring resolution;
        textureBytes.emplace_back();
        if (!ReadFile(ResolveTexturePath(texRel, &resolution, FromUtf8(attrDoc.geometry)), textureBytes.back())) {
            textureBytes.pop_back();
            ++missingTextureRefs;
            continue;
        }
        if (!resolution.empty()) ++recoveredTextureRefs;
        textures.push_back({*ref, textureBytes.back().data(), textureBytes.back().size()});
    }
    Preview3DInput input;
    input.dxg = modelBytes.data(); input.dxgSize = modelBytes.size();
    input.vraText = (const char*)attrBytes.data(); input.vraSize = attrBytes.size(); input.textures = std::move(textures);
    Preview3DTransform transform;
    // The selected map coordinate is the machine's origin in this preview. This
    // keeps world-space placements intact while making the scene focus editable.
    transform.position[0]=object.position.x-centerX;
    transform.position[1]=object.position.y-centerY;
    transform.position[2]=object.position.z-centerZ;
    transform.rotation[0]=object.rotation.x; transform.rotation[1]=object.rotation.y; transform.rotation[2]=object.rotation.z; transform.rotation[3]=object.rotation.w;
    transform.scale[0]=object.scale.x; transform.scale[1]=object.scale.y; transform.scale[2]=object.scale.z;
    if (!Preview3D_AppendStatic(input, transform)) return false;
    sceneMissingTextureRefs += missingTextureRefs;
    sceneRecoveredTextureRefs += recoveredTextureRefs;
    g_assetsForUnit.push_back({modelRel, L"场景对象 " + std::to_wstring(ordinal) + L"  " + name, 0});
    return true;
}

bool ParseSceneCenter(float& x, float& y, float& z) {
    const std::wstring raw = ReadEdit(g_sceneCenterEdit);
    wchar_t* end = nullptr;
    x = std::wcstof(raw.c_str(), &end); if (end == raw.c_str() || !std::isfinite(x)) return false;
    while (*end == L' ' || *end == L'\t' || *end == L',') ++end;
    const wchar_t* yStart = end; y = std::wcstof(end, &end);
    if (end == yStart || !std::isfinite(y)) return false;
    while (*end == L' ' || *end == L'\t' || *end == L',') ++end;
    const wchar_t* zStart = end; z = std::wcstof(end, &end);
    if (end == zStart || !std::isfinite(z)) return false;
    while (*end == L' ' || *end == L'\t') ++end;
    return *end == L'\0';
}

bool ParseTriple(const std::wstring& raw, float& x, float& y, float& z) {
    wchar_t* end = nullptr;
    x = std::wcstof(raw.c_str(), &end); if (end == raw.c_str() || !std::isfinite(x)) return false;
    while (*end == L' ' || *end == L'\t' || *end == L',') ++end;
    const wchar_t* ys = end; y = std::wcstof(end, &end); if (end == ys || !std::isfinite(y)) return false;
    while (*end == L' ' || *end == L'\t' || *end == L',') ++end;
    const wchar_t* zs = end; z = std::wcstof(end, &end); if (end == zs || !std::isfinite(z)) return false;
    while (*end == L' ' || *end == L'\t') ++end;
    return *end == L'\0';
}

std::wstring SelectedBoneName() {
    const int selected=(int)SendMessageW(g_attachBone,CB_GETCURSEL,0,0);
    if(selected<=0)return L"";
    const int len=(int)SendMessageW(g_attachBone,CB_GETLBTEXTLEN,selected,0);
    if(len<=0)return L"";
    std::wstring value((size_t)len+1,L'\0');
    SendMessageW(g_attachBone,CB_GETLBTEXT,selected,(LPARAM)value.data()); value.resize((size_t)len); return value;
}

void RecordMount(const Asset& asset, const std::wstring& position, const std::wstring& rotation,
                 const std::wstring& scale, const std::wstring& bone);

bool AppendAssetToScene(const Asset& asset, const std::wstring& position, const std::wstring& rotation,
                        const std::wstring& scaleText, const std::wstring& bone, bool record) {
    if (!g_sceneLoaded) { SetStatus(L"先载入机体，进入机体预览空间后再挂载模型资源。"); return false; }
    if (asset.path.rfind(L"effect-id:", 0) == 0) { SetStatus(L"特效 ID 已作为人工记录保存；目前没有证据支持 ID→VRA 的映射，需先从 FX 资源库选择并验证具体文件。"); return false; }
    const fs::path selectedPath = AssetPath(asset.path);
    const std::wstring selectedExt = Lower(selectedPath.extension().wstring());
    const std::wstring lowerAssetPath = Lower(asset.path);
    if (selectedExt == L".vra" && lowerAssetPath.rfind(L"fx/", 0) == 0 && lowerAssetPath.find(L"/pp_") != std::wstring::npos) {
        FxSpriteData sprite; std::vector<uint8_t> imageBytes;
        if (!ReadFxSprite(asset.path, sprite, imageBytes)) { SetStatus(L"PP Sprite 或其图片引用无法读取：" + asset.path); return false; }
        float px=0, py=0, pz=0, rx=0, ry=0, rz=0, scale=1;
        if (!ParseTriple(position, px, py, pz) || !ParseTriple(rotation, rx, ry, rz)) {
            SetStatus(L"挂载位置/旋转请按 x,y,z 填写，例如 0,0,0。"); return false;
        }
        wchar_t* scaleEnd = nullptr; scale = std::wcstof(scaleText.c_str(), &scaleEnd);
        if (scaleEnd == scaleText.c_str() || !std::isfinite(scale) || scale <= 0) {
            SetStatus(L"挂载缩放必须是大于 0 的数值。"); return false;
        }
        constexpr float degToRad = 0.017453292519943295f;
        const float hx=rx*degToRad*0.5f, hy=ry*degToRad*0.5f, hz=rz*degToRad*0.5f;
        const float cx=cosf(hx), sx=sinf(hx), cy=cosf(hy), sy=sinf(hy), cz=cosf(hz), sz=sinf(hz);
        Preview3DTransform transform;
        transform.position[0]=px; transform.position[1]=py; transform.position[2]=pz;
        transform.rotation[0]=sx*cy*cz-cx*sy*sz; transform.rotation[1]=cx*sy*cz+sx*cy*sz;
        transform.rotation[2]=cx*cy*sz-sx*sy*cz; transform.rotation[3]=cx*cy*cz+sx*sy*sz;
        transform.scale[0]=transform.scale[1]=transform.scale[2]=scale;
        if (!bone.empty()) { transform.attachToBone=true; transform.boneName=ToUtf8(bone); }
        Preview3DTex texture{ToUtf8(sprite.imagePath), imageBytes.data(), imageBytes.size()};
        if (!Preview3D_AppendSprite(texture, sprite.width, sprite.height, transform)) {
            SetStatus(L"Sprite 图片已找到，但未能加入 3D 预览空间；检查所选骨骼和图像格式。"); return false;
        }
        if (record) RecordMount(asset, position, rotation, scaleText, bone);
        g_fxSpritePreview = false;
        ShowWindow(g_imageView, SW_HIDE);
        ShowWindow(g_view, SW_SHOW);
        SetStatus(L"已加入手工 PP Sprite 预览平面（工具 alpha 混合，不代表客户端混合模式）：" + asset.path +
                  L"  位置=" + position + L"，旋转=" + rotation + L"，缩放=" + scaleText);
        Preview3D_Render();
        return true;
    }
    fs::path modelPath, attrPath;
    if (selectedExt == L".dxg") {
        modelPath = selectedPath;
        attrPath = AttrPathForModel(modelPath);
    } else if (selectedExt == L".vra") {
        attrPath = selectedPath;
        std::vector<uint8_t> probe; VraDoc probeDoc;
        if (!ReadFile(attrPath, probe) || !probeDoc.Parse((const char*)probe.data(), probe.size())) {
            SetStatus(L"所选 VRA 无法解析；特效引用不会冒充静态模型。资源文件：" + asset.path);
            return false;
        }
        if (!probeDoc.geometry.empty()) {
            const std::wstring geometry(probeDoc.geometry.begin(), probeDoc.geometry.end());
            modelPath = AssetPath(geometry);
        } else {
            const std::wstring modelStem = ModelStemForAttr(selectedPath.stem().wstring());
            if (modelStem.empty() || probeDoc.meshes.empty()) {
                SetStatus(L"所选 VRA 没有可配对的静态模型 geometry：" + asset.path);
                return false;
            }
            modelPath = selectedPath.parent_path().parent_path() / L"model" / (modelStem + L".dxg");
        }
    } else {
        SetStatus(L"只能把 DXG 模型或带 geometry 的 VRA 配置加入当前场景。"); return false;
    }
    std::vector<uint8_t> modelBytes, attrBytes;
    if (!ReadFile(modelPath, modelBytes)) { SetStatus(L"模型文件无法读取：" + modelPath.wstring()); return false; }
    if (!ReadFile(attrPath, attrBytes)) { SetStatus(L"模型存在，但配套材质属性未找到：" + attrPath.wstring()); return false; }
    VraDoc attrDoc;
    if (!attrDoc.Parse((const char*)attrBytes.data(), attrBytes.size())) { SetStatus(L"配套 VRA 无法解析：" + attrPath.wstring()); return false; }

    std::vector<std::vector<uint8_t>> textureBytes;
    std::vector<Preview3DTex> textures;
    size_t loadedTextureRefs=0, missingTextureRefs=0, recoveredTextureRefs=0;
    textureBytes.reserve(attrDoc.materials.size()); textures.reserve(attrDoc.materials.size());
    for (const auto& material : attrDoc.materials) for (const auto* ref : {&material.texture, &material.mixTexture}) {
        if (ref->empty()) continue;
        std::wstring texturePath(ref->begin(), ref->end());
        std::replace(texturePath.begin(), texturePath.end(), L'\\', L'/');
        std::wstring resolution;
        textureBytes.emplace_back();
        if (!ReadFile(ResolveTexturePath(texturePath, &resolution, FromUtf8(attrDoc.geometry)), textureBytes.back())) {
            textureBytes.pop_back(); ++missingTextureRefs; continue;
        }
        ++loadedTextureRefs;
        if (!resolution.empty()) ++recoveredTextureRefs;
        textures.push_back({*ref, textureBytes.back().data(), textureBytes.back().size()});
    }

    float px=0, py=0, pz=0, rx=0, ry=0, rz=0, scale=1;
    if (!ParseTriple(position, px, py, pz) || !ParseTriple(rotation, rx, ry, rz)) {
        SetStatus(L"挂载位置/旋转请按 x,y,z 填写，例如 0,0,0。"); return false;
    }
    wchar_t* scaleEnd = nullptr;
    scale = std::wcstof(scaleText.c_str(), &scaleEnd);
    if (scaleEnd == scaleText.c_str() || !std::isfinite(scale) || scale <= 0) {
        SetStatus(L"挂载缩放必须是大于 0 的数值。"); return false;
    }

    constexpr float kDegToRad = 0.017453292519943295f;
    const float hx=rx*kDegToRad*0.5f, hy=ry*kDegToRad*0.5f, hz=rz*kDegToRad*0.5f;
    const float cx=cosf(hx), sx=sinf(hx), cy=cosf(hy), sy=sinf(hy), cz=cosf(hz), sz=sinf(hz);
    Preview3DTransform transform;
    transform.position[0]=px; transform.position[1]=py; transform.position[2]=pz;
    transform.rotation[0]=sx*cy*cz-cx*sy*sz;
    transform.rotation[1]=cx*sy*cz+sx*cy*sz;
    transform.rotation[2]=cx*cy*sz-sx*sy*cz;
    transform.rotation[3]=cx*cy*cz+sx*sy*sz;
    transform.scale[0]=transform.scale[1]=transform.scale[2]=scale;
    if(!bone.empty()) { transform.attachToBone=true; transform.boneName=ToUtf8(bone); }
    Preview3DInput input; input.dxg=modelBytes.data(); input.dxgSize=modelBytes.size();
    input.vraText=(const char*)attrBytes.data(); input.vraSize=attrBytes.size(); input.textures=std::move(textures);
    if (!Preview3D_AppendStatic(input, transform)) { SetStatus(L"资源解析成功，但模型未能加入当前场景：" + asset.path); return false; }
    const std::wstring textureStatus = L"；贴图已载入 " + std::to_wstring(input.textures.size()) +
        L" 张，未解析 " + std::to_wstring(missingTextureRefs) + L" 个引用，名称回退 " +
        std::to_wstring(recoveredTextureRefs) + L" 个。";
    SetStatus(L"已手动挂载 " + asset.path + (bone.empty()?L" 到场景原点。位置=":L" 到骨骼 "+bone+L"。位置=") + position + L"，旋转=" + rotation + L"，缩放=" + scaleText + textureStatus);
    if (record) {
        RecordMount(asset, position, rotation, scaleText, bone);
        SetStatus(L"已手动挂载并保存 " + asset.path + (bone.empty()?L" 到场景原点。位置=":L" 到骨骼 "+bone+L"。位置=") + position + L"，旋转=" + rotation + L"，缩放=" + scaleText + textureStatus);
    }
    Preview3D_Render();
    return true;
}

bool AppendSelectedAssetToScene() {
    const int index = (int)SendMessageW(g_assets, LB_GETCURSEL, 0, 0);
    if (index < 0 || index >= (int)g_visible.size()) { SetStatus(L"先在左侧选择要挂载的模型资源。"); return false; }
    return AppendAssetToScene(g_visible[(size_t)index], ReadEdit(g_attachPosEdit),
                              ReadEdit(g_attachRotEdit), ReadEdit(g_attachScaleEdit), SelectedBoneName(), true);
}

void RecordMount(const Asset& asset, const std::wstring& position, const std::wstring& rotation,
                 const std::wstring& scale, const std::wstring& bone) {
    std::wstring profile = ReadEdit(g_profile);
    std::wstring section; size_t start=0, next=1;
    while(start<profile.size()) {
        size_t end=profile.find_first_of(L"\r\n",start); if(end==std::wstring::npos)end=profile.size();
        const std::wstring line=Trim(profile.substr(start,end-start)); start=end+1;
        if(line.size()>2&&line.front()==L'['&&line.back()==L']'){section=Lower(line.substr(1,line.size()-2));continue;}
        if(section!=L"mounts")continue;
        const size_t eq=line.find(L'='); if(eq==std::wstring::npos)continue;
        const std::wstring key=Trim(line.substr(0,eq));
        if(key.rfind(L"mount_",0)!=0)continue;
        try { next=(std::max)(next,(size_t)std::stoul(key.substr(6))+1); } catch(...) {}
    }
    const std::wstring key=L"mount_"+std::to_wstring(next);
    const std::wstring value=asset.path+L"|"+position+L"|"+rotation+L"|"+scale+L"|"+bone;
    profile=ReplaceProfileField(std::move(profile),L"mounts",key,value);
    SetWindowTextW(g_profile,profile.c_str());
    SaveProfile();
}

void LoadSavedMounts() {
    const std::wstring profile=ReadEdit(g_profile); std::wstring section; size_t start=0; size_t loaded=0;
    while(start<profile.size()) {
        size_t end=profile.find_first_of(L"\r\n",start); if(end==std::wstring::npos)end=profile.size();
        const std::wstring line=Trim(profile.substr(start,end-start)); start=end+1;
        if(line.empty()||line.front()==L'#')continue;
        if(line.size()>2&&line.front()==L'['&&line.back()==L']'){section=Lower(line.substr(1,line.size()-2));continue;}
        if(section!=L"mounts")continue;
        const size_t eq=line.find(L'='); if(eq==std::wstring::npos)continue;
        std::vector<std::wstring> parts; size_t p=eq+1;
        while(p<=line.size()) { const size_t sep=line.find(L'|',p); if(sep==std::wstring::npos){parts.push_back(line.substr(p));break;} parts.push_back(line.substr(p,sep-p));p=sep+1; }
        if(parts.size()<4)continue;
        Asset asset{parts[0],L"saved mount",0};
        if(AppendAssetToScene(asset,parts[1],parts[2],parts[3],parts.size()>4?parts[4]:L"",false))++loaded;
    }
    if(loaded)SetStatus(L"已从机体档案恢复 "+std::to_wstring(loaded)+L" 个手动模型挂载。");
}

void LoadFieldScene(const std::wstring& rawCode) {
    g_sceneLoaded = false;
    const std::wstring code = Trim(rawCode);
    if (g_unit.id.empty()) { SetStatus(L"先载入机体，再把地图场景叠加到机体预览。武器/特效使用当前机体档案中的手动绑定。"); return; }
    if (code.empty()) { SetStatus(L"请输入解包目录中的 world/field 地图代码，例如 F01。"); return; }
    const fs::path fieldDir = g_root / L"world" / L"field" / code;
    std::error_code ec;
    if (!fs::is_directory(fieldDir, ec)) { SetStatus(L"场景目录不存在：" + fieldDir.wstring()); return; }

    std::wstring model = ProfileValue(L"preview",L"model");
    if (model.empty()) model=(fs::path(g_unit.dir)/L"model"/g_unit.model).wstring();
    std::replace(model.begin(),model.end(),L'\\',L'/');
    if (!LoadVfsModel(model,false)) { SetStatus(L"无法重新载入机体，未开始场景组装："+model); return; }
    g_sceneLoaded = false;
    const std::wstring motion=ProfileValue(L"preview",L"motion");
    if(!motion.empty()) LoadVfsModel(motion,true);
    RefreshBoneChoices();

    float cx=0,cy=0,cz=0;
    if (!ParseSceneCenter(cx,cy,cz)) { SetStatus(L"场景中心格式应为 x,y,z，例如 0,0,0。未加载场景。"); return; }
    wchar_t* end=nullptr; const std::wstring radiusText=ReadEdit(g_sceneRadiusEdit);
    float radius=std::wcstof(radiusText.c_str(),&end); if(end==radiusText.c_str()||!std::isfinite(radius)||radius<=0)radius=4500.0f;
    const float radius2=radius*radius;
    size_t recordCount=0, loaded=0, outOfRange=0, missing=0, terrainRecords=0;
    size_t missingTextureRefs=0, recoveredTextureRefs=0;
    g_sceneCode=code;
    for (const auto& entry : fs::directory_iterator(fieldDir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || Lower(entry.path().extension().wstring()) != L".vra") continue;
        const std::wstring fileName=Lower(entry.path().filename().wstring());
        const bool terrain=fileName.size()>=8 && fileName.rfind(L"_grd.vra")==fileName.size()-8;
        const bool objects=fileName.size()>=8 && fileName.rfind(L"_obj.vra")==fileName.size()-8;
        if(!terrain&&!objects)continue;
        std::vector<uint8_t> bytes; SceneVraDoc doc;
        if(!ReadFile(entry.path(),bytes)||!doc.Parse((const char*)bytes.data(),bytes.size()))continue;
        for(const auto& object:doc.objects) {
            ++recordCount; if(terrain)++terrainRecords;
            if(objects&&object.hasPosition) {
                const float dx=object.position.x-cx,dy=object.position.y-cy,dz=object.position.z-cz;
                if(dx*dx+dy*dy+dz*dz>radius2){++outOfRange;continue;}
            }
            if(LoadSceneObject(object,code,cx,cy,cz,recordCount,missingTextureRefs,recoveredTextureRefs))++loaded; else ++missing;
        }
    }
    g_sceneLoaded = true;
    LoadSavedMounts();
    std::sort(g_assetsForUnit.begin(),g_assetsForUnit.end(),[](const Asset&a,const Asset&b){return a.path<b.path;});
    RefreshAssets();
    SetStatus(L"场景 "+code+L"：地形记录 "+std::to_wstring(terrainRecords)+L"，范围内叠加 "+std::to_wstring(loaded)+
              L" 个模型；半径外 "+std::to_wstring(outOfRange)+L"，资源未解析 "+std::to_wstring(missing)+
              L"；贴图未解析 "+std::to_wstring(missingTextureRefs)+L" 个引用，同名恢复 "+
              std::to_wstring(recoveredTextureRefs)+L" 个。中心="+std::to_wstring((int)cx)+L","+
              std::to_wstring((int)cy)+L","+std::to_wstring((int)cz));
    Preview3D_SetSceneRange(radius);
    Preview3D_Render();
}

void LoadMachine(const std::wstring& rawId) {
    const std::wstring id=Trim(rawId); auto it=g_units.find(id);
    if(it==g_units.end()) {
        auto byModel=std::find_if(g_units.begin(),g_units.end(),[&](const auto& kv){return kv.second.modelId==id;});
        if(byModel==g_units.end()){SetStatus(L"绑定表中没有这个机体 ID / モデルID。未做推测匹配。");return;}
        it=byModel;
    }
    g_unit=it->second; g_visible.clear();g_assetsForUnit.clear();g_fxIndexed=false;g_itemIndexed=false;g_sceneLoaded=false;
    AddFiles(AssetPath(g_unit.dir + L"/model"),0,L"机体模型  ");
    AddFiles(AssetPath(g_unit.dir + L"/anim"),1,L"动作  ");
    std::sort(g_assetsForUnit.begin(),g_assetsForUnit.end(),[](const Asset&a,const Asset&b){return a.path<b.path;});
    SetWindowTextW(g_msEdit,g_unit.id.c_str()); LoadProfile(); LoadProfileFields();
    AddProfileWeapons(); AddProfileIcons(); AddProfileEffects(); RefreshAssets();
    UpdateUnitCards();
    SetStatus(g_unit.name+L"  |  "+g_unit.wiki+L"  |  "+std::to_wstring(g_visible.size())+L" 个机体/挂载资源");
    std::wstring model=ProfileValue(L"preview",L"model");
    if(model.empty())model=(fs::path(g_unit.dir)/L"model"/g_unit.model).wstring();
    std::replace(model.begin(),model.end(),L'\\',L'/');
    const std::wstring motion=ProfileValue(L"preview",L"motion");
    if(!LoadVfsModel(model,false))SetStatus(L"机体档案中的模型路径无法加载："+model);
    else {
        // The mech preview itself is the working space. Map loading remains an
        // optional background overlay; manual mounts must not depend on it.
        g_sceneLoaded=true;
        RefreshBoneChoices();
        if(!motion.empty()&&!LoadVfsModel(motion,true))SetStatus(L"当前模型已显示，但档案动作无法加载："+motion);
        else RefreshBoneChoices();
        LoadSavedMounts();
    }
}

bool LoadVfsModel(const std::wstring& rel, bool motion) {
    const fs::path p=AssetPath(rel); std::vector<uint8_t> mesh,attr,mrb;
    if (Lower(p.extension().wstring())==L".mrb") {
        if(!ReadFile(p,mrb)) return false;
        mesh=g_modelBytes;
        const fs::path modelPath = AssetPath(g_unit.dir + L"/model/" + g_unit.model);
        ReadFile(AttrPathForModel(modelPath),attr);
    } else {
        if(!ReadFile(p,mesh)) return false;
        ReadFile(AttrPathForModel(p),attr);
    }
    if(mesh.empty()) return false;
    Preview3D_Clear(); Preview3D_LoadMotion(nullptr,0);
    if(!Preview3D_Init(g_view)) return false;
    Preview3DInput in; in.dxg=mesh.data();in.dxgSize=mesh.size();
    if(!attr.empty()){in.vraText=(const char*)attr.data();in.vraSize=attr.size();}
    VraDoc attrDoc;
    std::vector<std::vector<uint8_t>> textureBytes;
    size_t loadedTextureRefs=0, missingTextureRefs=0, recoveredTextureRefs=0;
    if(!attr.empty()) {
        attrDoc.Parse((const char*)attr.data(),attr.size());
        textureBytes.reserve(attrDoc.materials.size());
        for(const auto& mat:attrDoc.materials) for(const auto* ref:{&mat.texture,&mat.mixTexture}) if(!ref->empty()) {
            std::wstring texPath(ref->begin(),ref->end());
            std::replace(texPath.begin(),texPath.end(),L'\\',L'/');
            std::wstring resolution;
            textureBytes.emplace_back();
            if(!ReadFile(ResolveTexturePath(texPath,&resolution,FromUtf8(attrDoc.geometry)),textureBytes.back())) {
                textureBytes.pop_back(); ++missingTextureRefs; continue;
            }
            ++loadedTextureRefs;
            if(!resolution.empty()) ++recoveredTextureRefs;
            Preview3DTex tex; tex.key=*ref;tex.data=textureBytes.back().data();tex.size=textureBytes.back().size();in.textures.push_back(tex);
        }
    }
    if(!Preview3D_Load(in)) return false;
    if(motion&&!mrb.empty()) Preview3D_LoadMotion(mrb.data(),mrb.size());
    g_modelBytes=std::move(mesh);g_attrBytes=std::move(attr);g_motionBytes=std::move(mrb);
    if (missingTextureRefs || recoveredTextureRefs) {
        SetStatus(L"模型贴图：已载入 " + std::to_wstring(loadedTextureRefs) + L" 个引用，未解析 " +
                  std::to_wstring(missingTextureRefs) + L" 个；名称回退 " + std::to_wstring(recoveredTextureRefs) + L" 个。");
    }
    Preview3D_Render();return true;
}

void IndexVraReferences(const Asset& asset) {
    std::vector<uint8_t> bytes;
    if (!ReadFile(AssetPath(asset.path), bytes)) return;
    const std::string raw(bytes.begin(), bytes.end());
    const std::set<std::wstring> allowed{L".vra",L".dxg",L".dds",L".png",L".tga",L".mrb"};
    std::set<std::wstring> added;
    for (size_t pos=0; pos<raw.size();) {
        const size_t lineEnd=raw.find('\n',pos); const size_t end=lineEnd==std::string::npos?raw.size():lineEnd;
        const std::string line=raw.substr(pos,end-pos); pos=end+1;
        if(line.find("string")==std::string::npos)continue;
        const size_t q1=line.find('"'), q2=q1==std::string::npos?q1:line.find('"',q1+1);
        if(q1==std::string::npos||q2==std::string::npos)continue;
        std::wstring ref=FromUtf8(line.substr(q1+1,q2-q1-1));
        std::replace(ref.begin(),ref.end(),L'\\',L'/');
        if(allowed.find(Lower(fs::path(ref).extension().wstring()))==allowed.end()||ref.empty())continue;
        if(!added.insert(Lower(ref)).second)continue;
        std::wstring resolution;
        const bool exists=AssetReferenceExists(ref,&resolution);
        const auto ext = Lower(fs::path(ref).extension().wstring());
        const std::wstring label = (ext==L".png"||ext==L".tga"||ext==L".dds") ? L"FX 引用图像  " : L"VRA 引用  ";
        const std::wstring recovered = resolution.empty() ? L"" : L"  [同名安全恢复]";
        auto existing=std::find_if(g_assetsForUnit.begin(),g_assetsForUnit.end(),[&](const Asset& a){return Lower(a.path)==Lower(ref);});
        if(existing==g_assetsForUnit.end()) {
            g_assetsForUnit.push_back({ref,label+ref+(exists?recovered:L"  [资源缺失]"),3});
        } else if(ext==L".png"||ext==L".tga"||ext==L".dds") {
            existing->label=L"FX 引用图像 ["+fs::path(asset.path).filename().wstring()+L"]  "+ref+
                            (exists?recovered:L"  [资源缺失]");
        }
    }
    RefreshAssets(asset.path);
}

void LoadSelectedAsset() {
    const int i=(int)SendMessageW(g_assets,LB_GETCURSEL,0,0); if(i<0||i>=(int)g_visible.size())return;
    const Asset a=g_visible[(size_t)i];
    g_fxSpritePreview = false;
    if (g_fxTimeline) ShowWindow(g_fxTimeline, SW_HIDE);
    const bool isMotion=Lower(fs::path(a.path).extension().wstring())==L".mrb";
    const std::wstring ext=Lower(fs::path(a.path).extension().wstring());
    if(ext==L".vra") {
        const std::wstring lowerPath = Lower(a.path);
        if (lowerPath.find(L"fx/") == 0 && lowerPath.find(L"/pp_") != std::wstring::npos) {
            const bool previewed = LoadFxSpritePreview(a.path);
            IndexVraReferences(a);
            if (previewed) return;
            std::wstring geometry;
            if (FirstFxModelGeometry(a.path, geometry) && LoadVfsModel(geometry, false)) {
                g_sceneLoaded = false;
                ShowWindow(g_imageView, SW_HIDE); ShowWindow(g_view, SW_SHOW);
                SetStatus(L"PP 没有可读取的 Sprite/Particle 贴图；已预览其首个 Model 引用的静态 geometry：" + geometry + L"。其他模型/VRA 引用仍列在 FX 资源中，Work 动画未播放。");
                return;
            }
            ShowWindow(g_imageView, SW_HIDE); ShowWindow(g_view, SW_SHOW);
            SetStatus(L"PP 文件中没有可读取的 Sprite/Particle 图片引用；已保留资源路径供检查：" + a.path);
        }
        std::vector<uint8_t> bytes;VraDoc doc;
        if(ReadFile(AssetPath(a.path),bytes)&&doc.Parse((const char*)bytes.data(),bytes.size())&&!doc.geometry.empty()) {
            if (g_sceneLoaded) { AppendSelectedAssetToScene(); return; }
            std::wstring geo(doc.geometry.begin(),doc.geometry.end());
            if(LoadVfsModel(geo,false)){SetStatus(a.path+L"  |  引用模型："+geo);ShowWindow(g_imageView,SW_HIDE);ShowWindow(g_view,SW_SHOW);return;}
        }
        IndexVraReferences(a);
        SetStatus(a.path+L"  |  VRA 配置/特效引用。当前显示其资源关联，不模拟粒子运行时。");return;
    }
    if(a.path.rfind(L"effect-id:",0)==0){SetStatus(L"已记录人工特效 ID；请在 FX 资源库筛选资源名，双击预览贴图，或对含 geometry 的 VRA 使用场景挂载。");return;}
    if (ext==L".dxg" && g_sceneLoaded) { AppendSelectedAssetToScene(); return; }
    if(ext==L".dds"||ext==L".png"||ext==L".tga") {
        std::vector<uint8_t> image;
        if(ReadFile(AssetPath(a.path),image)&&DecodeImage(image.data(),image.size(),a.path,g_imageData)) {
            if(g_imageBitmap)DeleteObject(g_imageBitmap);g_imageBitmap=ImageToBitmap(g_imageData);
            ShowWindow(g_view,SW_HIDE);ShowWindow(g_imageView,SW_SHOW);InvalidateRect(g_imageView,nullptr,TRUE);
            SetStatus(a.path+L"  |  图片/贴图预览");return;
        }
    }
    if(!LoadVfsModel(a.path,isMotion)) SetStatus(L"资源可定位，但当前预览器未能载入此模型/动作。");
    else {
        if (isMotion && g_sceneLoaded) LoadSavedMounts();
        SetStatus(a.path+(isMotion?L"  |  MRB 动作循环播放中":L"  |  模型预览"));
    }
    ShowWindow(g_imageView,SW_HIDE);ShowWindow(g_view,SW_SHOW);
}

void BrowseRoot() {
    BROWSEINFOW bi{}; bi.hwndOwner=g_main; bi.lpszTitle=L"选择 unpackdat 资源目录"; bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi); if(!pid)return;
    wchar_t path[MAX_PATH]{}; if(SHGetPathFromIDListW(pid,path)){g_root=path;SetWindowTextW(g_rootEdit,path);}
    CoTaskMemFree(pid);
}

void Layout(HWND h) {
    RECT r{}; GetClientRect(h, &r);
    const int width = r.right, height = r.bottom;
    const int pad = 14, searchY = 8, attachY = 44, tabY = 82, bodyY = 128, footer = 32;
    const int left = 270, right = 322, bodyH = (std::max)(180, height - bodyY - footer - pad);
    const int centerX = pad + left + pad, centerW = (std::max)(220, width - centerX - right - pad * 2);
    const int rightX = centerX + centerW + pad;

    int x = pad;
    const int rootW = (std::max)(220, width - 470);
    MoveWindow(g_rootEdit, x, searchY, rootW, 28, TRUE); x += rootW + 8;
    MoveWindow(GetDlgItem(h, ID_BROWSE), x, searchY, 106, 28, TRUE); x += 114;
    MoveWindow(g_msEdit, x, searchY, 108, 28, TRUE); x += 116;
    MoveWindow(GetDlgItem(h, ID_LOAD), x, searchY, 92, 28, TRUE); x += 100;

    const int attachWidth = (std::max)(70, (width - 470) / 3);
    MoveWindow(g_attachPosEdit, pad, attachY, attachWidth, 28, TRUE);
    MoveWindow(g_attachRotEdit, pad + attachWidth + 8, attachY, attachWidth, 28, TRUE);
    MoveWindow(g_attachScaleEdit, pad + 2 * (attachWidth + 8), attachY, 78, 28, TRUE);
    MoveWindow(g_attachBone, pad + 2 * (attachWidth + 8) + 164, attachY, 160, 150, TRUE);
    MoveWindow(GetDlgItem(h, ID_ATTACH), pad + 2 * (attachWidth + 8) + 332, attachY, (std::max)(130, width - pad * 3 - 2 * attachWidth - 340), 28, TRUE);

    const int tabW = (width - pad * 2) / 6;
    const int tabs[] = {ID_TAB_OVERVIEW, ID_TAB_WEAPONS, ID_TAB_TRAITS, ID_TAB_MOTION, ID_TAB_EFFECTS, ID_TAB_SIMULATOR};
    for (int i = 0; i < 6; ++i) MoveWindow(GetDlgItem(h, tabs[i]), pad + i * tabW, tabY, tabW - 6, 34, TRUE);

    MoveWindow(g_kind, pad + 8, bodyY + 8, left - 16, 26, TRUE);
    MoveWindow(g_assetSearch, pad + 8, bodyY + 38, left - 16, 26, TRUE);
    MoveWindow(g_assets, pad, bodyY + 68, left, bodyH - 68, TRUE);
    MoveWindow(g_view, centerX, bodyY, centerW, bodyH, TRUE);
    MoveWindow(g_imageView, centerX, bodyY, centerW, bodyH, TRUE);
    MoveWindow(g_fxTimeline, centerX + 14, bodyY + bodyH - 40, centerW - 28, 30, TRUE);
    MoveWindow(g_unitTitle, rightX, bodyY, right, 42, TRUE);
    MoveWindow(g_unitMeta, rightX, bodyY + 44, right, 36, TRUE);

    if (g_activeTab == 1 || g_activeTab == 2 || g_activeTab == 4 || g_activeTab == 5) {
        const int first = g_activeTab == 1 ? 11 : g_activeTab == 4 ? 20 : 0;
        const int last = g_activeTab == 1 ? 20 : g_activeTab == 4 ? 25 : 11;
        for (int i = first; i < last; ++i) {
            const int y = bodyY + 94 + (i - first) * 47;
            MoveWindow(g_fieldLabels[i], rightX + 8, y, 128, 34, TRUE);
            MoveWindow(g_fieldEdits[i], rightX + 140, y, right - 148, 34, TRUE);
        }
        MoveWindow(GetDlgItem(h, ID_SAVE), rightX, bodyY + bodyH - 36, right, 34, TRUE);
    } else {
        MoveWindow(g_unitCard, rightX, bodyY + 88, right, 180, TRUE);
        MoveWindow(g_weaponCard, rightX, bodyY + 278, right, (std::max)(120, bodyH - 440), TRUE);
        MoveWindow(g_sourceCard, rightX, bodyY + bodyH - 152, right, 144, TRUE);
    }
    MoveWindow(g_status, pad, height - 24, width - pad * 2, 20, TRUE);
}

LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m){
    case WM_CREATE: {
        g_main=h;
        g_darkBrush=CreateSolidBrush(RGB(12,22,34)); g_panelBrush=CreateSolidBrush(RGB(19,39,57)); g_editBrush=CreateSolidBrush(RGB(7,17,27));
        g_rootEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"F:\\9ymsgo\\9you\\机动战士敢达Online\\GundamOnline\\unpackdat",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_ROOT,nullptr,nullptr);
        const fs::path configuredRoot=L"F:\\9ymsgo\\9you\\机动战士敢达Online\\GundamOnline\\unpackdat";
        if(fs::is_directory(configuredRoot))g_root=configuredRoot;
        CreateWindowW(L"BUTTON",L"选择 unpackdat",WS_CHILD|WS_VISIBLE,(int)0,0,0,0,h,(HMENU)ID_BROWSE,nullptr,nullptr);
        g_msEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1101011",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_MS,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"载入机体",WS_CHILD|WS_VISIBLE,0,0,0,0,h,(HMENU)ID_LOAD,nullptr,nullptr);
        g_attachPosEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0,0,0",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_ATTACH_POS,nullptr,nullptr);
        g_attachRotEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"0,0,0",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_ATTACH_ROT,nullptr,nullptr);
        g_attachScaleEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"1",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_ATTACH_SCALE,nullptr,nullptr);
        g_attachBone=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,h,(HMENU)ID_ATTACH_BONE,nullptr,nullptr);
        SendMessageW(g_attachBone,CB_ADDSTRING,0,(LPARAM)L"场景固定"); SendMessageW(g_attachBone,CB_SETCURSEL,0,0);
        CreateWindowW(L"BUTTON",L"挂载左侧所选 DXG / VRA（位置 / 旋转 / 缩放 / 骨骼）",WS_CHILD|WS_VISIBLE,0,0,0,0,h,(HMENU)ID_ATTACH,nullptr,nullptr);
        g_kind=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|CBS_DROPDOWNLIST,0,0,0,0,h,(HMENU)ID_KIND,nullptr,nullptr);
        for(const wchar_t*s:{L"模型",L"动作",L"武器",L"弹药/特效"})SendMessageW(g_kind,CB_ADDSTRING,0,(LPARAM)s);SendMessageW(g_kind,CB_SETCURSEL,0,0);
        g_assetSearch=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,h,(HMENU)ID_ASSET_SEARCH,nullptr,nullptr);
        SendMessageW(g_assetSearch,EM_SETCUEBANNER,TRUE,(LPARAM)L"搜索模型 / 动作 / 武器 / 特效资源路径");
        g_assets=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,0,0,0,0,h,(HMENU)ID_ASSETS,nullptr,nullptr);
        g_view=CreateWindowExW(0,L"GundamMachineViewport",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,h,(HMENU)ID_VIEW,nullptr,nullptr);
        g_imageView=CreateWindowExW(WS_EX_CLIENTEDGE,L"GundamMachineImage",L"",WS_CHILD,0,0,0,0,h,nullptr,nullptr,nullptr);
        g_fxTimeline=CreateWindowExW(0,TRACKBAR_CLASSW,L"",WS_CHILD|TBS_HORZ|TBS_AUTOTICKS,0,0,0,0,h,(HMENU)ID_FX_TIME,nullptr,nullptr);
        g_profile=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL,0,0,0,0,h,(HMENU)ID_PROFILE,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"保存实验档案",WS_CHILD|WS_VISIBLE,0,0,0,0,h,(HMENU)ID_SAVE,nullptr,nullptr);
        for (const auto& tab : std::vector<std::pair<int,const wchar_t*>>{{ID_TAB_OVERVIEW,L"机体总览"},{ID_TAB_WEAPONS,L"武器与弹药"},{ID_TAB_TRAITS,L"特性与数值"},{ID_TAB_MOTION,L"动作库"},{ID_TAB_EFFECTS,L"特效绑定"},{ID_TAB_SIMULATOR,L"模拟器 / 档案"}})
            CreateWindowW(L"BUTTON",tab.second,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,h,ControlHandle(tab.first),nullptr,nullptr);
        g_unitTitle=CreateWindowW(L"STATIC",L"等待载入机体",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE|WS_BORDER,0,0,0,0,h,(HMENU)ID_UNIT_TITLE,nullptr,nullptr);
        g_unitMeta=CreateWindowW(L"STATIC",L"选择 unpackdat 目录并载入",WS_CHILD|WS_VISIBLE|SS_LEFT|WS_BORDER,0,0,0,0,h,(HMENU)ID_UNIT_META,nullptr,nullptr);
        g_unitCard=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|WS_BORDER,0,0,0,0,h,(HMENU)ID_UNIT_CARD,nullptr,nullptr);
        g_weaponCard=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|WS_BORDER,0,0,0,0,h,(HMENU)ID_WEAPON_CARD,nullptr,nullptr);
        g_sourceCard=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|WS_BORDER,0,0,0,0,h,(HMENU)ID_SOURCE_CARD,nullptr,nullptr);
        for (int i = 0; i < 25; ++i) {
            g_fieldLabels[i] = CreateWindowW(L"STATIC",g_fieldTitles[i],WS_CHILD|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,h,ControlHandle(ID_FIELD_LABEL_FIRST+i),nullptr,nullptr);
            g_fieldEdits[i] = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"未确认",WS_CHILD|ES_AUTOHSCROLL,0,0,0,0,h,ControlHandle(ID_FIELD_EDIT_FIRST+i),nullptr,nullptr);
        }
        g_status=CreateWindowW(STATUSCLASSNAMEW,L"选择资源目录和机体 ID。",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,nullptr,nullptr);
        UpdateUnitCards(); SetTab(0); Layout(h); return 0; }
    case WM_SIZE: Layout(h); if(Preview3D_Active())Preview3D_Resize();return 0;
    case WM_COMMAND:
        if(LOWORD(w)==ID_BROWSE){BrowseRoot();return 0;}
        if(LOWORD(w)==ID_LOAD){g_root=ReadEdit(g_rootEdit);LoadMachine(ReadEdit(g_msEdit));UpdateUnitCards();SetTab(g_activeTab);return 0;}
        if(LOWORD(w)==ID_LOAD_SCENE){g_root=ReadEdit(g_rootEdit);LoadFieldScene(ReadEdit(g_sceneEdit));return 0;}
        if(LOWORD(w)==ID_ATTACH){AppendSelectedAssetToScene();return 0;}
        if(LOWORD(w)==ID_ASSET_SEARCH&&HIWORD(w)==EN_CHANGE){RefreshAssets();return 0;}
        if(LOWORD(w)==ID_SAVE){SyncProfileFields();SaveProfile();LoadMachine(g_unit.id);UpdateUnitCards();return 0;}
        if(LOWORD(w)>=ID_TAB_OVERVIEW&&LOWORD(w)<=ID_TAB_SIMULATOR){SetTab((int)LOWORD(w)-(int)ID_TAB_OVERVIEW);Layout(h);return 0;}
        if(LOWORD(w)==ID_ASSETS&&HIWORD(w)==LBN_DBLCLK){LoadSelectedAsset();return 0;}
        if(LOWORD(w)==ID_KIND&&HIWORD(w)==CBN_SELCHANGE){RefreshAssets();return 0;}
        break;
    case WM_HSCROLL:
        if ((HWND)l == g_fxTimeline && g_fxSpritePreview) {
            g_fxCurrentFrame = (int)SendMessageW(g_fxTimeline, TBM_GETPOS, 0, 0);
            RenderFxFrame();
            SetStatus(L"PP 预览帧 " + std::to_wstring(g_fxCurrentFrame) + L" / " + std::to_wstring(g_fxDuration) + L"（随机/UV/blend 等行为为工具近似显示）");
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: {
        HDC dc=(HDC)w; HWND child=(HWND)l;
        const int id=GetDlgCtrlID(child);
        const bool edit=id==ID_ROOT||id==ID_MS||id==ID_PROFILE||id==ID_SCENE||id==ID_SCENE_CENTER||id==ID_SCENE_RADIUS||id==ID_ASSET_SEARCH||
                        id==ID_ATTACH_POS||id==ID_ATTACH_ROT||id==ID_ATTACH_SCALE||
                        (id>=ID_FIELD_EDIT_FIRST&&id<ID_FIELD_EDIT_FIRST+25);
        SetTextColor(dc,RGB(220,235,242)); SetBkColor(dc,edit?RGB(7,17,27):RGB(19,39,57));
        return (INT_PTR)(edit?g_editBrush:g_panelBrush);
    }
    case WM_ERASEBKGND: { RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_darkBrush);return 1; }
    case WM_DESTROY:Preview3D_Shutdown();if(g_darkBrush)DeleteObject(g_darkBrush);if(g_panelBrush)DeleteObject(g_panelBrush);if(g_editBrush)DeleteObject(g_editBrush);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

LRESULT CALLBACK ViewProc(HWND h,UINT m,WPARAM w,LPARAM l){return Preview3D_WndProc(h,m,w,l);}
LRESULT CALLBACK ImageProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT rc{};GetClientRect(h,&rc);
        FillRect(dc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        if(g_imageBitmap&&g_imageData.w>0&&g_imageData.h>0){HDC mem=CreateCompatibleDC(dc);auto old=SelectObject(mem,g_imageBitmap);
            const int availH = (std::max)(16, (int)rc.bottom - (g_fxSpritePreview ? 62 : 16));
            const double scale=(std::min)((double)(rc.right-16)/g_imageData.w,(double)availH/g_imageData.h);
            const int dw=(int)(g_imageData.w*scale),dh=(int)(g_imageData.h*scale),x=(rc.right-dw)/2,y=(availH-dh)/2;
            SetStretchBltMode(dc,HALFTONE);BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            AlphaBlend(dc,x,y,dw,dh,mem,0,0,g_imageData.w,g_imageData.h,bf);SelectObject(mem,old);DeleteDC(mem);}
        if(g_fxSpritePreview&&!g_fxSpriteInfo.empty()){
            RECT info{12,12,(std::max)(12,(int)rc.right-12),(std::min)((int)rc.bottom-12,228)};
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(220,238,246));
            DrawTextW(dc,g_fxSpriteInfo.c_str(),(int)g_fxSpriteInfo.size(),&info,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        }
        EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    LoadBindings(); wchar_t app[MAX_PATH]{};GetModuleFileNameW(nullptr,app,MAX_PATH);g_profileDir=fs::path(app).parent_path()/L"machine_profiles";
    WNDCLASSW vc{};vc.lpfnWndProc=ViewProc;vc.hInstance=inst;vc.lpszClassName=L"GundamMachineViewport";vc.hCursor=LoadCursor(nullptr,IDC_CROSS);RegisterClassW(&vc);
    WNDCLASSW ic{};ic.lpfnWndProc=ImageProc;ic.hInstance=inst;ic.hCursor=LoadCursor(nullptr,IDC_ARROW);ic.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);ic.lpszClassName=L"GundamMachineImage";RegisterClassW(&ic);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"GundamMachineConsole";RegisterClassW(&wc);
    HWND h=CreateWindowW(wc.lpszClassName,L"Gundam Machine Console",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,980,nullptr,nullptr,inst,nullptr);
    if(!h)return 1;ShowWindow(h,show);UpdateWindow(h);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return(int)msg.wParam;
}

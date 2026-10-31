#include "vra_text.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

bool Starts(const char* p, const char* end, const char* lit) {
    const size_t n = strlen(lit);
    return p + n <= end && memcmp(p, lit, n) == 0;
}

const char* Find(const char* p, const char* end, const char* lit) {
    const size_t n = strlen(lit);
    if (n == 0 || p + n > end) return nullptr;
    for (const char* c = p; c + n <= end; ++c)
        if (memcmp(c, lit, n) == 0) return c;
    return nullptr;
}

void TrimPathSegments(std::string& value) {
    if (value.find_first_of("/\\") == std::string::npos) return;
    auto asciiSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto sjisFullWidthSpaceAt = [&](size_t pos, size_t end) {
        return pos + 1 < end && (unsigned char)value[pos] == 0x81 && (unsigned char)value[pos + 1] == 0x40;
    };
    size_t begin = 0;
    while (begin <= value.size()) {
        const size_t end = value.find_first_of("/\\", begin);
        const size_t limit = end == std::string::npos ? value.size() : end;
        size_t first = begin, last = limit;
        while (first < last && (asciiSpace(value[first]) || sjisFullWidthSpaceAt(first, last)))
            first += sjisFullWidthSpaceAt(first, last) ? 2 : 1;
        while (last > first && asciiSpace(value[last - 1])) --last;
        while (last > first && last >= 2 && (unsigned char)value[last - 2] == 0x81 &&
               (unsigned char)value[last - 1] == 0x40) last -= 2;
        size_t effectiveLimit = limit;
        if (first > begin) {
            const size_t removed = first - begin;
            value.erase(begin, removed);
            last -= removed;
            effectiveLimit -= removed;
        }
        if (last < effectiveLimit) value.erase(last, effectiveLimit - last);
        if (end == std::string::npos) break;
        begin = value.find_first_of("/\\", begin);
        if (begin == std::string::npos) break;
        ++begin;
    }
}

bool ParseQuoted(const char* p, const char* end, std::string& out) {
    const char* q1 = Find(p, end, "\"");
    if (!q1) return false;
    const char* q2 = Find(q1 + 1, end, "\"");
    if (!q2) return false;
    out.assign(q1 + 1, q2);
    const size_t first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) out.clear();
    else {
        const size_t last = out.find_last_not_of(" \t\r\n");
        out = out.substr(first, last - first + 1);
    }
    TrimPathSegments(out);
    return true;
}

const char* MatchingBrace(const char* open, const char* end) {
    if (!open || open >= end || *open != '{') return nullptr;
    int depth = 0;
    bool quoted = false;
    for (const char* p = open; p < end; ++p) {
        if (*p == '"' && (p == open || p[-1] != '\\')) quoted = !quoted;
        if (quoted) continue;
        if (*p == '{') ++depth;
        else if (*p == '}' && --depth == 0) return p;
    }
    return nullptr;
}

const char* AfterType(const char* p, const char* end) {
    const char* br = Find(p, end, "]");
    return br ? br + 1 : p;
}

bool ParseIntAfter(const char* p, const char* end, int& out) {
    while (p < end && (*p < '0' || *p > '9') && *p != '-') ++p;
    if (p >= end) return false;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    int v = 0;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
        any = true;
    }
    if (!any) return false;
    out = v * sign;
    return true;
}

bool ParseFloatsAfter(const char* p, const char* end, float* out, size_t count) {
    p = AfterType(p, end);
    const char* lineEnd = Find(p, end, "\n");
    if (!lineEnd) lineEnd = end;
    const std::string line(p, lineEnd);
    const char* value = line.c_str();
    const char* valueEnd = value + line.size();
    for (size_t i = 0; i < count; ++i) {
        while (value < valueEnd && (*value == ' ' || *value == '\t' || *value == '\r' || *value == ',')) ++value;
        if (value >= valueEnd) return false;
        char* next = nullptr;
        const float parsed = std::strtof(value, &next);
        if (next == value || next > valueEnd || !std::isfinite(parsed)) return false;
        out[i] = parsed;
        value = next;
    }
    return true;
}

}  // namespace

bool VraDoc::Parse(const char* text, size_t len) {
    meshes.clear();
    materials.clear();
    geometry.clear();
    if (!text || !len) return false;
    const char* end = text + len;
    const char* p = text;
    enum { None, InMesh, InMat } st = None;
    VraMeshBind curM;
    VraMaterial curA;
    bool haveM = false, haveA = false;
    enum TextureSlot { NoTextureSlot, BaseTextureSlot, MixTextureSlot, GlossTextureSlot };
    TextureSlot textureSlot = NoTextureSlot;
    const char* textureBlockEnd = nullptr;
    const char* lod0BlockEnd = nullptr;

    while (p < end) {
        if (Starts(p, end, "{mesh")) {
            if (haveM) meshes.push_back(curM);
            curM = {};
            haveM = true;
            st = InMesh;
            lod0BlockEnd = nullptr;
            p += 5;
            continue;
        }
        if (Starts(p, end, "{material") && !Starts(p, end, "{material_set")) {
            if (haveA) materials.push_back(curA);
            curA = {};
            haveA = true;
            st = InMat;
            textureSlot = NoTextureSlot;
            textureBlockEnd = nullptr;
            p += 9;
            continue;
        }
        if (Starts(p, end, "(name string")) {
            std::string s;
            if (ParseQuoted(p, end, s) && st == InMesh) curM.name = s;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMesh && Starts(p, end, "{lod0")) {
            const char* close = MatchingBrace(p, end);
            lod0BlockEnd = close;
            curM.hasLod0 = close != nullptr;
            const char* brace = Find(p, end, "{");
            p = brace ? brace + 1 : p + 1;
            continue;
        }
        if (Starts(p, end, "(material_id")) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v) && st == InMesh && lod0BlockEnd && p < lod0BlockEnd)
                curM.materialId = v;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (Starts(p, end, "(no_render")) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v) && st == InMesh) curM.noRender = v != 0;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (Starts(p, end, "(id int32") && st == InMat) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v)) curA.id = v;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && (Starts(p, end, "{s_base") || Starts(p, end, "{s_mix") ||
                            Starts(p, end, "{s_gloss"))) {
            const char* close = MatchingBrace(p, end);
            textureBlockEnd = close ? close : nullptr;
            if (Starts(p, end, "{s_base")) textureSlot = BaseTextureSlot;
            else if (Starts(p, end, "{s_mix")) textureSlot = MixTextureSlot;
            else textureSlot = GlossTextureSlot;
            const char* brace = Find(p, end, "{");
            p = brace ? brace + 1 : p + 1;
            continue;
        }
        if ((Starts(p, end, "(s_base string[2]") || Starts(p, end, "(s_mix string[2]") ||
             Starts(p, end, "(s_gloss string[2]")) && st == InMat) {
            const TextureSlot directSlot = Starts(p, end, "(s_base") ? BaseTextureSlot :
                                           Starts(p, end, "(s_mix") ? MixTextureSlot : GlossTextureSlot;
            std::string key, value;
            const char* afterKey = nullptr;
            const char* q1 = Find(p, end, "\"");
            if (q1) {
                const char* q2 = Find(q1 + 1, end, "\"");
                if (q2) {
                    key.assign(q1 + 1, q2);
                    const char* q3 = Find(q2 + 1, end, "\"");
                    const char* q4 = q3 ? Find(q3 + 1, end, "\"") : nullptr;
                    if (q3 && q4) {
                        value.assign(q3 + 1, q4);
                        afterKey = q4 + 1;
                    }
                }
            }
            if (key == "texture" && !value.empty()) {
                if (directSlot == BaseTextureSlot) curA.texture = value;
                else if (directSlot == MixTextureSlot) curA.mixTexture = value;
                else curA.glossTexture = value;
            }
            p = afterKey ? afterKey : p + 1;
            continue;
        }
        if (Starts(p, end, "(texture string") && st == InMat && textureBlockEnd && p < textureBlockEnd) {
            std::string s;
            if (ParseQuoted(p, end, s)) {
                if (textureSlot == BaseTextureSlot) curA.texture = s;
                else if (textureSlot == MixTextureSlot) curA.mixTexture = s;
                else if (textureSlot == GlossTextureSlot) curA.glossTexture = s;
            }
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && Starts(p, end, "(ambient vector3")) {
            ParseFloatsAfter(p, end, curA.ambient, 3);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && Starts(p, end, "(diffuse vector3")) {
            ParseFloatsAfter(p, end, curA.diffuse, 3);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && Starts(p, end, "(emissive vector3")) {
            ParseFloatsAfter(p, end, curA.emissive, 3);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && Starts(p, end, "(uvadd_u float")) {
            ParseFloatsAfter(p, end, &curA.uvAddU, 1);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (st == InMat && Starts(p, end, "(uvadd_v float")) {
            ParseFloatsAfter(p, end, &curA.uvAddV, 1);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (textureBlockEnd && p >= textureBlockEnd) {
            textureBlockEnd = nullptr;
            textureSlot = NoTextureSlot;
        }
        if (lod0BlockEnd && p >= lod0BlockEnd) lod0BlockEnd = nullptr;
        if (Starts(p, end, "(alpha_blend_type")) {
            std::string s;
            if (ParseQuoted(p, end, s)) curA.blend = s;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (Starts(p, end, "(twoSided")) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v)) curA.twoSided = v != 0;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (Starts(p, end, "(alpha_test")) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v)) curA.alphaTest = v != 0;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        if (Starts(p, end, "(Geometry string")) {
            ParseQuoted(p, end, geometry);
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
        ++p;
    }
    if (haveM) meshes.push_back(curM);
    if (haveA) materials.push_back(curA);
    return !geometry.empty() || !meshes.empty() || !materials.empty();
}

bool VraDoc::HasMesh(const std::string& meshName) const {
    for (const auto& m : meshes)
        if (m.name == meshName && m.hasLod0 && m.materialId >= 0 && GetMat(m.materialId)) return true;
    return false;
}

int VraDoc::MaterialOf(const std::string& meshName) const {
    for (const auto& m : meshes)
        if (m.name == meshName && m.hasLod0 && m.materialId >= 0 && GetMat(m.materialId)) return m.materialId;
    return -1;
}

bool VraDoc::IsNoRender(const std::string& meshName) const {
    for (const auto& m : meshes)
        if (m.name == meshName) return m.noRender;
    return false;
}

const VraMaterial* VraDoc::GetMat(int id) const {
    for (const auto& m : materials)
        if (m.id == id) return &m;
    return nullptr;
}

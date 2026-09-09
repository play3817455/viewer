#include "vra_text.h"

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

bool ParseQuoted(const char* p, const char* end, std::string& out) {
    const char* q1 = Find(p, end, "\"");
    if (!q1) return false;
    const char* q2 = Find(q1 + 1, end, "\"");
    if (!q2) return false;
    out.assign(q1 + 1, q2);
    return true;
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

    while (p < end) {
        if (Starts(p, end, "{mesh")) {
            if (haveM) meshes.push_back(curM);
            curM = {};
            haveM = true;
            st = InMesh;
            p += 5;
            continue;
        }
        if (Starts(p, end, "{material") && !Starts(p, end, "{material_set")) {
            if (haveA) materials.push_back(curA);
            curA = {};
            haveA = true;
            st = InMat;
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
        if (Starts(p, end, "(material_id")) {
            int v = 0;
            if (ParseIntAfter(AfterType(p, end), end, v) && st == InMesh) curM.materialId = v;
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
        if (Starts(p, end, "(texture string")) {
            std::string s;
            if (ParseQuoted(p, end, s)) curA.texture = s;
            const char* q = Find(p, end, "\n");
            p = q ? q + 1 : end;
            continue;
        }
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

int VraDoc::MaterialOf(const std::string& meshName) const {
    for (const auto& m : meshes)
        if (m.name == meshName) return m.materialId;
    return 1;
}

const VraMaterial* VraDoc::GetMat(int id) const {
    for (const auto& m : materials)
        if (m.id == id) return &m;
    return materials.empty() ? nullptr : &materials[0];
}

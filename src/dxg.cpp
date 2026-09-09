#include "dxg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kTypSkinned = 26;  // cmp eax, 0x1A @ 0x86A008
constexpr int kTyp26PosExtra = 190;   // o2 + nverts*4 + 190 -> VB (GND_00 sample)

int Align4(int x) { return (x + 3) & ~3; }

bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           fabsf(x) < 1.0e6f && fabsf(y) < 1.0e6f && fabsf(z) < 1.0e6f;
}

bool NearZeroVert(const DxgVec3& p) {
    return fabsf(p.x) + fabsf(p.y) + fabsf(p.z) < 0.75f;
}

bool LooksUnitNrm(const DxgVec3& p) {
    const float L = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    return L > 0.75f && L < 1.25f &&
           fabsf(p.x) < 1.3f && fabsf(p.y) < 1.3f && fabsf(p.z) < 1.3f;
}

void ReadPos(const uint8_t* data, int off, int count, std::vector<DxgVec3>& out) {
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i)
        memcpy(&out[(size_t)i], data + off + (size_t)i * 12u, 12);
}

void ReadNrmAfterPos(const uint8_t* data, size_t len, int posOff, int nverts,
                     std::vector<DxgVec3>& nrm) {
    nrm.assign((size_t)nverts, {0, 1, 0});
    const size_t posEnd = (size_t)posOff + (size_t)nverts * 12u;
    if (posEnd + (size_t)nverts * 12u > len) return;
    int ok = 0;
    for (int i = 0; i < nverts; ++i) {
        float x, y, z;
        memcpy(&x, data + posEnd + (size_t)i * 12u, 4);
        memcpy(&y, data + posEnd + (size_t)i * 12u + 4, 4);
        memcpy(&z, data + posEnd + (size_t)i * 12u + 8, 4);
        if (!Finite3(x, y, z)) return;
        const float L = sqrtf(x * x + y * y + z * z);
        if (L > 0.5f && L < 1.5f) ++ok;
        nrm[(size_t)i] = {x, y, z};
    }
    if (ok < nverts / 2) nrm.clear();
}

void ReadUvAfterPos(const uint8_t* data, size_t len, int posOff, int nverts,
                    std::vector<DxgVec2>& uv) {
    uv.clear();
    if (nverts <= 0 || posOff < 0) return;
    const size_t uvOff = (size_t)posOff + (size_t)nverts * 12u;
    if (uvOff + (size_t)nverts * 8u > len) return;
    uv.resize((size_t)nverts);
    int ok = 0;
    for (int i = 0; i < nverts; ++i) {
        float u = 0, v = 0;
        memcpy(&u, data + uvOff + (size_t)i * 8u, 4);
        memcpy(&v, data + uvOff + (size_t)i * 8u + 4, 4);
        if (!std::isfinite(u) || !std::isfinite(v)) {
            uv.clear();
            return;
        }
        if (u >= -0.05f && u <= 8.0f && v >= -0.05f && v <= 8.0f) ++ok;
        uv[(size_t)i] = {u, v};
    }
    if (ok < nverts / 2) uv.clear();
}

void MakeBoxUv(const std::vector<DxgVec3>& pos, std::vector<DxgVec2>& uv) {
    if (pos.empty()) return;
    float mn[3] = {1.0e9f, 1.0e9f, 1.0e9f};
    float mx[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
    for (const auto& p : pos) {
        if (p.x < mn[0]) mn[0] = p.x;
        if (p.y < mn[1]) mn[1] = p.y;
        if (p.z < mn[2]) mn[2] = p.z;
        if (p.x > mx[0]) mx[0] = p.x;
        if (p.y > mx[1]) mx[1] = p.y;
        if (p.z > mx[2]) mx[2] = p.z;
    }
    const float dx = (std::max)(0.001f, mx[0] - mn[0]);
    const float dy = (std::max)(0.001f, mx[1] - mn[1]);
    uv.resize(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        uv[i].u = (pos[i].x - mn[0]) / dx;
        uv[i].v = 1.0f - (pos[i].y - mn[1]) / dy;
    }
}

void FaceNormals(DxgMesh& m) {
    m.nrm.assign(m.pos.size(), {});
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        const uint32_t ia = m.idx[i], ib = m.idx[i + 1], ic = m.idx[i + 2];
        if (ia >= m.pos.size() || ib >= m.pos.size() || ic >= m.pos.size()) continue;
        const DxgVec3& a = m.pos[ia];
        const DxgVec3& b = m.pos[ib];
        const DxgVec3& c = m.pos[ic];
        DxgVec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        DxgVec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        DxgVec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        m.nrm[ia].x += n.x;
        m.nrm[ia].y += n.y;
        m.nrm[ia].z += n.z;
        m.nrm[ib].x += n.x;
        m.nrm[ib].y += n.y;
        m.nrm[ib].z += n.z;
        m.nrm[ic].x += n.x;
        m.nrm[ic].y += n.y;
        m.nrm[ic].z += n.z;
    }
    for (auto& n : m.nrm) {
        const float L = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (L > 1.0e-8f) {
            n.x /= L;
            n.y /= L;
            n.z /= L;
        } else {
            n.y = 1.0f;
        }
    }
}

bool LooksLod1(const std::string& name, uint16_t nFaces) {
    if (name.find("_GND_01") != std::string::npos) return true;
    if (name.find("_LOD1") != std::string::npos || name.find("_lod1") != std::string::npos) return true;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, "_01") == 0 &&
        name.find("GND") != std::string::npos && nFaces <= 16)
        return true;
    return false;
}

int PosOffset(uint32_t typ, int o2, int headerVerts) {
    if (typ == kTypSkinned)
        return Align4(o2 + headerVerts * 4 + kTyp26PosExtra);
    return Align4(o2);
}

// Game loader (GundamOnline_unpacked.exe): scan [o1,o2) in 2-byte steps for the first
// contiguous nidx x u16 run with no 0xFFFF separator. VB vertex count = max(IB)+1.
bool FindGameIndexBuffer(const uint8_t* data, int o1, int o2, int nidx, int& ibOff,
                         int& maxIdx) {
    if (o1 < 0 || o2 <= o1 || nidx < 3) return false;
    ibOff = -1;
    maxIdx = -1;
    for (int st = o1; st + nidx * 2 <= o2; st += 2) {
        int mx = 0;
        bool ok = true;
        for (int k = 0; k < nidx; ++k) {
            uint16_t v = 0;
            memcpy(&v, data + st + k * 2, 2);
            if (v == 0xFFFF) {
                ok = false;
                break;
            }
            if ((int)v > mx) mx = (int)v;
        }
        if (!ok) continue;
        ibOff = st;
        maxIdx = mx;
        return true;
    }
    return false;
}

void CompactMesh(DxgMesh& m) {
    if (m.idx.empty() || m.pos.empty()) return;
    std::vector<uint32_t> used;
    used.reserve(m.idx.size());
    for (uint32_t i : m.idx) {
        if (i < m.pos.size()) used.push_back(i);
    }
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    if (used.size() == m.pos.size()) return;
    std::vector<DxgVec3> pos(used.size());
    std::vector<DxgVec3> nrm;
    std::vector<DxgVec2> uv;
    if (m.nrm.size() == m.pos.size()) nrm.resize(used.size());
    if (m.uv.size() == m.pos.size()) uv.resize(used.size());
    std::vector<uint32_t> remap(m.pos.size(), UINT32_MAX);
    for (size_t i = 0; i < used.size(); ++i) {
        const uint32_t old = used[i];
        remap[old] = (uint32_t)i;
        pos[i] = m.pos[old];
        if (!nrm.empty()) nrm[i] = m.nrm[old];
        if (!uv.empty()) uv[i] = m.uv[old];
    }
    for (uint32_t& idx : m.idx) {
        if (idx < remap.size()) idx = remap[idx];
    }
    m.pos = std::move(pos);
    if (!nrm.empty()) m.nrm = std::move(nrm);
    if (!uv.empty()) m.uv = std::move(uv);
}

void BuildTriangles(const uint16_t* idx, int nidx, int nv, const std::vector<DxgVec3>& pos,
                    std::vector<uint32_t>& out) {
    out.clear();
    out.reserve((size_t)nidx);
    for (int i = 0; i + 2 < nidx; i += 3) {
        const uint32_t a = idx[i];
        const uint32_t b = idx[i + 1];
        const uint32_t c = idx[i + 2];
        if (a >= (uint32_t)nv || b >= (uint32_t)nv || c >= (uint32_t)nv) continue;
        if (a == b || b == c || a == c) continue;
        if (!pos.empty()) {
            const DxgVec3& pa = pos[a];
            const DxgVec3& pb = pos[b];
            const DxgVec3& pc = pos[c];
            if (!Finite3(pa.x, pa.y, pa.z) || !Finite3(pb.x, pb.y, pb.z) || !Finite3(pc.x, pc.y, pc.z))
                continue;
            if (NearZeroVert(pa) || NearZeroVert(pb) || NearZeroVert(pc)) continue;
            DxgVec3 e1{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
            DxgVec3 e2{pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
            DxgVec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            const float area2 = n.x * n.x + n.y * n.y + n.z * n.z;
            if (area2 < 1.0e-12f) continue;
        }
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    }
}

bool TryPackedMesh(const uint8_t* data, size_t dlen, int o2, int nFaces, DxgMesh& m) {
    if (nFaces <= 0 || o2 < 0) return false;
    const int packed = nFaces * 3;
    int posOff = o2;
    if ((size_t)posOff + 16u + (size_t)packed * 12u <= dlen) {
        bool hdrZero = true;
        for (int k = 0; k < 16; k += 4) {
            float f = 0;
            memcpy(&f, data + posOff + k, 4);
            if (fabsf(f) > 1.0e-4f) hdrZero = false;
        }
        if (hdrZero) posOff += 16;
    }
    if ((size_t)posOff + (size_t)packed * 12u > dlen) return false;
    for (int i = 0; i < packed; ++i) {
        float x, y, z;
        memcpy(&x, data + posOff + (size_t)i * 12u, 4);
        memcpy(&y, data + posOff + (size_t)i * 12u + 4, 4);
        memcpy(&z, data + posOff + (size_t)i * 12u + 8, 4);
        if (!Finite3(x, y, z)) return false;
    }
    ReadPos(data, posOff, packed, m.pos);
    m.idx.resize((size_t)packed);
    for (int i = 0; i < packed; ++i) m.idx[(size_t)i] = (uint32_t)i;
    return !m.pos.empty();
}

void ParseMeshPayload(const uint8_t* pl, size_t sz, const std::string& name, DxgMesh& m,
                      std::vector<std::pair<int, DxgVec3>>* skinAcc) {
    if (sz < 28) return;
    uint32_t flags = 0, nidx = 0;
    uint16_t nFaces = 0, nVerts = 0;
    memcpy(&flags, pl + 4, 4);
    memcpy(&nFaces, pl + 12, 2);
    memcpy(&nVerts, pl + 14, 2);
    memcpy(&nidx, pl + 24, 4);
    const uint8_t* data = pl + 28;
    const size_t dlen = sz - 28;
    m.name = name;
    m.flags = flags;
    m.nFaces = nFaces;
    m.nVerts = nVerts;
    m.lod1 = LooksLod1(name, nFaces);
    if (dlen < 16 || nFaces == 0) return;
    if (name.find("decal") != std::string::npos && nVerts <= 3) return;

    uint16_t a = 0, b = 0;
    uint32_t typ = 0, o1 = 0, o2 = 0;
    memcpy(&a, data, 2);
    memcpy(&b, data + 2, 2);
    memcpy(&typ, data + 4, 4);
    memcpy(&o1, data + 8, 4);
    memcpy(&o2, data + 12, 4);
    (void)a;
    (void)b;

    if (nidx < 3) {
        if (TryPackedMesh(data, dlen, (int)o2, (int)nFaces, m)) {
            FaceNormals(m);
            MakeBoxUv(m.pos, m.uv);
        }
        return;
    }

    int ibOff = -1, maxIdx = -1;
    if (!FindGameIndexBuffer(data, (int)o1, (int)o2, (int)nidx, ibOff, maxIdx)) {
        if (TryPackedMesh(data, dlen, (int)o2, (int)nFaces, m)) {
            FaceNormals(m);
            MakeBoxUv(m.pos, m.uv);
        }
        return;
    }

    const int nv = maxIdx + 1;
    const int posOff = PosOffset(typ, (int)o2, (int)nVerts);
    if (posOff < 0 || nv <= 0 || (size_t)posOff + (size_t)nv * 12u > dlen) return;

    ReadPos(data, posOff, nv, m.pos);
    const uint16_t* ib = (const uint16_t*)(data + ibOff);
    BuildTriangles(ib, (int)nidx, nv, m.pos, m.idx);
    if (m.idx.empty()) return;

    // typ=26: pos block is followed by float2 UV, then skin weights — not normals.
    FaceNormals(m);
    ReadUvAfterPos(data, dlen, posOff, nv, m.uv);
    if (m.uv.size() != m.pos.size())
        MakeBoxUv(m.pos, m.uv);
    CompactMesh(m);

    if (skinAcc && !m.pos.empty() && o1 > 0 && ibOff > o1) {
        const auto* skin = (const uint16_t*)(data + o1);
        const int nSkin = (ibOff - (int)o1) / 2;
        std::vector<uint16_t> g;
        for (int i = 0; i < nSkin; ++i) {
            const uint16_t v = skin[i];
            if (v == 0xFFFF) {
                if (g.size() >= 4 && g[0] == 0 && g[2] == 0) {
                    const uint16_t bone = g[1];
                    const uint16_t vert = g[3];
                    if (vert < m.pos.size())
                        skinAcc->push_back({(int)bone, m.pos[vert]});
                }
                g.clear();
            } else {
                g.push_back(v);
                if (g.size() > 8) g.clear();
            }
        }
    }
}

void ParseSkeleton(const uint8_t* pl, size_t sz, std::vector<DxgBone>& bones) {
    if (sz < 8) return;
    uint32_t nlen = 0;
    memcpy(&nlen, pl, 4);
    if (nlen > sz - 4) return;
    std::vector<std::string> names;
    size_t i = 4;
    std::string cur;
    while (i < 4 + nlen && i < sz) {
        if (pl[i] == 0) {
            if (!cur.empty()) names.push_back(cur);
            cur.clear();
        } else if (pl[i] >= 32 && pl[i] < 127) {
            cur.push_back((char)pl[i]);
        }
        ++i;
    }
    if (!cur.empty()) names.push_back(cur);
    if (names.empty()) return;
    bones.resize(names.size());
    for (size_t bi = 0; bi < names.size(); ++bi) {
        bones[bi].name = names[bi];
        bones[bi].parent = -1;
    }
    const uint8_t* rest = pl + 4 + nlen;
    const size_t rlen = sz - (4 + nlen);
    const int n = (int)bones.size();
    for (size_t k = 0; k + 1 < rlen; ++k) {
        const int a = rest[k];
        const int p = rest[k + 1];
        if (a > 0 && a < n && p >= 0 && p < n && p < a && bones[(size_t)a].parent < 0)
            bones[(size_t)a].parent = p;
    }
}

void LayoutBones(std::vector<DxgBone>& bones, const std::vector<DxgMesh>& meshes) {
    int placed = 0;
    for (const auto& b : bones)
        if (fabsf(b.pos.x) + fabsf(b.pos.y) + fabsf(b.pos.z) > 1.0f) ++placed;
    if (bones.empty() || placed >= (int)bones.size() / 4) return;
    float mn[3] = {1.0e9f, 1.0e9f, 1.0e9f};
    float mx[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
    bool any = false;
    for (const auto& mesh : meshes) {
        if (mesh.lod1) continue;
        for (const auto& p : mesh.pos) {
            if (p.x < mn[0]) mn[0] = p.x;
            if (p.y < mn[1]) mn[1] = p.y;
            if (p.z < mn[2]) mn[2] = p.z;
            if (p.x > mx[0]) mx[0] = p.x;
            if (p.y > mx[1]) mx[1] = p.y;
            if (p.z > mx[2]) mx[2] = p.z;
            any = true;
        }
    }
    if (!any) return;
    const float cx = (mn[0] + mx[0]) * 0.5f;
    const float cy0 = mn[1], cy1 = mx[1];
    const float cz = (mn[2] + mx[2]) * 0.5f;
    const float sx = (mx[0] - mn[0]) * 0.28f;
    auto Y = [&](float t) { return cy0 + (cy1 - cy0) * t; };
    for (auto& bone : bones) {
        const std::string& n = bone.name;
        if (n.find("head") != std::string::npos || n.find("Head") != std::string::npos)
            bone.pos = {cx, Y(0.95f), cz};
        else if (n.find("Foot") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.35f, Y(0.02f), cz};
        else if (n.find("Calf") != std::string::npos || n.find("Knee") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.3f,
                        Y(n.find("Knee") != std::string::npos ? 0.28f : 0.14f), cz};
        else if (n.find("Thigh") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.25f, Y(0.42f), cz};
        else if (n.find("Hand") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx), Y(0.58f), cz};
        else if (n.find("Forearm") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.85f, Y(0.62f), cz};
        else if (n.find("UpperArm") != std::string::npos || n.find("Clavicle") != std::string::npos)
            bone.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.55f, Y(0.78f), cz};
        else if (n.find("Pevis") != std::string::npos || n.find("Skirt") != std::string::npos)
            bone.pos = {cx, Y(0.48f), cz};
        else if (n.find("Spine01") != std::string::npos)
            bone.pos = {cx, Y(0.72f), cz};
        else if (n.find("Spine") != std::string::npos)
            bone.pos = {cx, Y(0.60f), cz};
        else if (n.find("Root") != std::string::npos)
            bone.pos = {cx, Y(0.50f), cz};
        else
            bone.pos = {cx, Y(0.55f), cz};
    }
}

}  // namespace

bool DxgModel::Parse(const uint8_t* data, size_t size) {
    names.clear();
    meshes.clear();
    bones.clear();
    if (!data || size < 28) return false;
    if (memcmp(data, "DXG ", 4) != 0) return false;
    uint32_t nameBytes = 0;
    memcpy(&nameBytes, data + 20, 4);
    if (nameBytes > size - 24) return false;
    {
        std::string cur;
        for (uint32_t i = 0; i < nameBytes; ++i) {
            const char c = (char)data[24 + i];
            if (c == 0) {
                if (!cur.empty()) names.push_back(cur);
                cur.clear();
            } else if ((unsigned char)c >= 32) {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) names.push_back(cur);
    }

    size_t off = 24 + nameBytes;
    size_t nameI = 0;
    std::vector<std::pair<int, DxgVec3>> skinAcc;
    while (off + 8 <= size) {
        uint32_t typ = 0, csz = 0;
        memcpy(&typ, data + off, 4);
        memcpy(&csz, data + off + 4, 4);
        if (off + 8 + csz > size) break;
        const uint8_t* pl = data + off + 8;
        if (typ == 1) {
            DxgMesh mesh;
            std::string nm = nameI < names.size() ? names[nameI] : ("mesh" + std::to_string(nameI));
            ++nameI;
            ParseMeshPayload(pl, csz, nm, mesh, &skinAcc);
            if (!mesh.pos.empty() && !mesh.idx.empty()) meshes.push_back(std::move(mesh));
        } else if (typ == 36) {
            ParseSkeleton(pl, csz, bones);
        }
        off += 8 + csz;
        if (typ == 0 && csz == 0) break;
    }

    if (!bones.empty() && !skinAcc.empty()) {
        std::vector<DxgVec3> acc(bones.size());
        std::vector<int> cnt(bones.size(), 0);
        for (const auto& s : skinAcc) {
            if (s.first < 0 || s.first >= (int)bones.size()) continue;
            acc[(size_t)s.first].x += s.second.x;
            acc[(size_t)s.first].y += s.second.y;
            acc[(size_t)s.first].z += s.second.z;
            cnt[(size_t)s.first]++;
        }
        for (size_t i = 0; i < bones.size(); ++i) {
            if (cnt[i] <= 0) continue;
            const float x = acc[i].x / (float)cnt[i];
            const float y = acc[i].y / (float)cnt[i];
            const float z = acc[i].z / (float)cnt[i];
            if (!Finite3(x, y, z) || fabsf(x) + fabsf(y) + fabsf(z) > 800.0f) continue;
            bones[i].pos = {x, y, z};
        }
        for (size_t i = 0; i < bones.size(); ++i) {
            if (cnt[i] > 0) continue;
            int p = bones[i].parent;
            while (p >= 0 && cnt[(size_t)p] <= 0) p = bones[(size_t)p].parent;
            if (p >= 0) bones[i].pos = bones[(size_t)p].pos;
        }
    }
    LayoutBones(bones, meshes);
    return !meshes.empty();
}

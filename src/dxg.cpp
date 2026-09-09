#include "dxg.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           fabsf(x) < 1.0e6f && fabsf(y) < 1.0e6f && fabsf(z) < 1.0e6f;
}

int FindFloat3Run(const uint8_t* data, size_t len, int count, float maxAbs) {
    if (count < 3) return -1;
    const size_t need = (size_t)count * 12u;
    if (len < need) return -1;
    int best = -1;
    float bestScore = -1.0f;
    const size_t step = count > 200 ? 4u : 4u;
    for (size_t off = 0; off + need <= len; off += step) {
        float mn[3] = {1.0e9f, 1.0e9f, 1.0e9f};
        float mx[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
        int zero = 0;
        bool fail = false;
        for (int i = 0; i < count; ++i) {
            float x, y, z;
            memcpy(&x, data + off + (size_t)i * 12u, 4);
            memcpy(&y, data + off + (size_t)i * 12u + 4, 4);
            memcpy(&z, data + off + (size_t)i * 12u + 8, 4);
            if (!Finite3(x, y, z) || fabsf(x) > maxAbs || fabsf(y) > maxAbs || fabsf(z) > maxAbs) {
                fail = true;
                break;
            }
            if (fabsf(x) + fabsf(y) + fabsf(z) < 1.0e-5f) ++zero;
            if (x < mn[0]) mn[0] = x;
            if (y < mn[1]) mn[1] = y;
            if (z < mn[2]) mn[2] = z;
            if (x > mx[0]) mx[0] = x;
            if (y > mx[1]) mx[1] = y;
            if (z > mx[2]) mx[2] = z;
        }
        if (fail) continue;
        const float dx = mx[0] - mn[0];
        const float dy = mx[1] - mn[1];
        const float dz = mx[2] - mn[2];
        if (dx < 1.0f && dy < 1.0f && dz < 1.0f) continue;
        if (zero > count * 3 / 4) continue;
        const float score = dx + dy + dz;
        if (score > bestScore) {
            bestScore = score;
            best = (int)off;
            if (count > 400 && dx > 20.0f && dy > 40.0f) return best;
        }
    }
    return best;
}

void ReadPos(const uint8_t* data, int off, int count, std::vector<DxgVec3>& out) {
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        memcpy(&out[(size_t)i], data + off + (size_t)i * 12u, 12);
    }
}

void ReadUvAfter(const uint8_t* data, size_t len, int posOff, int nverts, std::vector<DxgVec2>& uv) {
    uv.assign((size_t)nverts, {});
    const size_t posEnd = (size_t)posOff + (size_t)nverts * 12u;
    if (posEnd + (size_t)nverts * 8u > len) return;
    int good = 0;
    for (int i = 0; i < nverts; ++i) {
        float u, v;
        memcpy(&u, data + posEnd + (size_t)i * 8u, 4);
        memcpy(&v, data + posEnd + (size_t)i * 8u + 4, 4);
        if (!std::isfinite(u) || !std::isfinite(v) || fabsf(u) > 8.0f || fabsf(v) > 8.0f) {
            good = 0;
            break;
        }
        if (u >= -0.05f && u <= 1.05f && v >= -0.05f && v <= 1.05f) ++good;
        uv[(size_t)i].u = u;
        uv[(size_t)i].v = v;
    }
    if (good < nverts * 6 / 10) uv.assign((size_t)nverts, {});
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

void AddStripTris(const uint16_t* idx, int n, int nVerts, int maxFaces, std::vector<uint32_t>& out) {
    uint16_t s[8];
    int sl = 0;
    for (int i = 0; i < n && (int)out.size() / 3 < maxFaces; ++i) {
        const uint16_t v = idx[i];
        if (v == 0xFFFF || v >= (uint16_t)nVerts) {
            sl = 0;
            continue;
        }
        if (sl < 8) s[sl++] = v;
        else {
            s[0] = s[1];
            s[1] = s[2];
            s[2] = v;
            sl = 3;
        }
        if (sl >= 3) {
            uint16_t a, b, c;
            if (sl % 2 == 1) {
                a = s[sl - 3];
                b = s[sl - 2];
                c = s[sl - 1];
            } else {
                a = s[sl - 2];
                b = s[sl - 3];
                c = s[sl - 1];
            }
            if (a != b && b != c && a != c) {
                out.push_back(a);
                out.push_back(b);
                out.push_back(c);
            }
        }
    }
}

void AddTupleTris(const uint8_t* data, size_t len, int nVerts, int maxFaces, std::vector<uint32_t>& out) {
    const int n = (int)(len / 2);
    if (n < 4) return;
    const auto* idx = (const uint16_t*)data;
    std::vector<uint16_t> g;
    g.reserve(8);
    for (int i = 0; i < n && (int)out.size() / 3 < maxFaces; ++i) {
        const uint16_t v = idx[i];
        if (v == 0xFFFF) {
            if (g.size() == 4 && g[0] == 0) {
                const uint16_t a = g[1], b = g[2], c = g[3];
                if (a < nVerts && b < nVerts && c < nVerts && a != b && b != c && a != c) {
                    out.push_back(a);
                    out.push_back(b);
                    out.push_back(c);
                }
            }
            g.clear();
            continue;
        }
        g.push_back(v);
        if (g.size() > 8) g.clear();
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

    uint16_t a = 0, b = 0;
    uint32_t typ = 0, o1 = 0, o2 = 0;
    memcpy(&a, data, 2);
    memcpy(&b, data + 2, 2);
    memcpy(&typ, data + 4, 4);
    memcpy(&o1, data + 8, 4);
    memcpy(&o2, data + 12, 4);

    const int lodFaces = nFaces;
    const int packedCount = (int)nFaces * 3;

    if (typ == 1 && o2 + 12 <= dlen) {
        const int npos = packedCount;
        if (o2 + (size_t)npos * 12u <= dlen) {
            ReadPos(data, (int)o2, npos, m.pos);
            m.idx.resize((size_t)npos);
            for (int i = 0; i < npos; ++i) m.idx[(size_t)i] = (uint32_t)i;
            MakeBoxUv(m.pos, m.uv);
            FaceNormals(m);
            return;
        }
    }

    int vcount = nVerts > 1 ? (int)nVerts : packedCount;
    int posOff = -1;
    if (o2 < dlen) posOff = FindFloat3Run(data + o2, dlen - o2, vcount, 4000.0f);
    if (posOff >= 0) posOff += (int)o2;
    if (posOff < 0) posOff = FindFloat3Run(data, dlen, vcount, 4000.0f);
    if (posOff < 0 && packedCount != vcount) {
        posOff = FindFloat3Run(data, dlen, packedCount, 4000.0f);
        if (posOff >= 0) vcount = packedCount;
    }
    if (posOff < 0) return;

    if (vcount == packedCount && nVerts <= 2) {
        ReadPos(data, posOff, packedCount, m.pos);
        m.idx.resize((size_t)packedCount);
        for (int i = 0; i < packedCount; ++i) m.idx[(size_t)i] = (uint32_t)i;
    } else {
        ReadPos(data, posOff, vcount, m.pos);
        if (o1 + 6 <= dlen) {
            const int n16 = (int)((dlen - o1) / 2);
            AddStripTris((const uint16_t*)(data + o1), n16, vcount, lodFaces, m.idx);
        }
        if ((int)m.idx.size() / 3 < lodFaces / 2) {
            m.idx.clear();
            AddTupleTris(data, (o1 > 16 && o1 < dlen) ? o1 : dlen, vcount, lodFaces, m.idx);
        }
        if (m.idx.empty()) {
            const int n = (std::min)(packedCount, vcount);
            m.idx.resize((size_t)n);
            for (int i = 0; i < n; ++i) m.idx[(size_t)i] = (uint32_t)i;
        }
        if ((int)m.idx.size() / 3 > lodFaces && lodFaces > 0)
            m.idx.resize((size_t)lodFaces * 3u);
    }

    ReadUvAfter(data, dlen, posOff, (int)m.pos.size(), m.uv);
    bool uvOk = false;
    if (m.uv.size() == m.pos.size()) {
        float ru = 0, rv = 0;
        float umn = 1.0e9f, umx = -1.0e9f, vmn = 1.0e9f, vmx = -1.0e9f;
        for (const auto& t : m.uv) {
            if (t.u < umn) umn = t.u;
            if (t.u > umx) umx = t.u;
            if (t.v < vmn) vmn = t.v;
            if (t.v > vmx) vmx = t.v;
        }
        ru = umx - umn;
        rv = vmx - vmn;
        uvOk = ru > 0.15f && rv > 0.15f && ru < 4.0f && rv < 4.0f;
    }
    if (!uvOk) MakeBoxUv(m.pos, m.uv);
    FaceNormals(m);

    if (skinAcc && nVerts > 1) {
        const int n16 = (int)(dlen / 2);
        const auto* idx = (const uint16_t*)data;
        std::vector<uint16_t> g;
        for (int i = 0; i < n16; ++i) {
            const uint16_t v = idx[i];
            if (v == 0xFFFF) {
                if (g.size() == 4 && g[0] == 0 && g[1] == g[3] && g[1] < m.pos.size()) {
                    skinAcc->push_back({(int)g[2], m.pos[g[1]]});
                }
                g.clear();
            } else {
                g.push_back(v);
                if (g.size() > 6) g.clear();
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
    for (size_t b = 0; b < names.size(); ++b) {
        bones[b].name = names[b];
        bones[b].parent = -1;
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

}  // namespace

bool DxgModel::Parse(const uint8_t* data, size_t size) {
    names.clear();
    meshes.clear();
    bones.clear();
    if (!data || size < 28) return false;
    if (memcmp(data, "DXG ", 4) != 0) return false;
    uint32_t meshCount = 0, nameBytes = 0;
    memcpy(&meshCount, data + 12, 4);
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
        uint32_t typ = 0, sz = 0;
        memcpy(&typ, data + off, 4);
        memcpy(&sz, data + off + 4, 4);
        if (off + 8 + sz > size) break;
        const uint8_t* pl = data + off + 8;
        if (typ == 1) {
            DxgMesh m;
            std::string nm = nameI < names.size() ? names[nameI] : ("mesh" + std::to_string(nameI));
            ++nameI;
            ParseMeshPayload(pl, sz, nm, m, &skinAcc);
            if (!m.pos.empty() && !m.idx.empty()) meshes.push_back(std::move(m));
        } else if (typ == 36) {
            ParseSkeleton(pl, sz, bones);
        }
        off += 8 + sz;
        if (typ == 0 && sz == 0) break;
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
            if (cnt[i] > 0) {
                bones[i].pos.x = acc[i].x / (float)cnt[i];
                bones[i].pos.y = acc[i].y / (float)cnt[i];
                bones[i].pos.z = acc[i].z / (float)cnt[i];
            }
        }
        for (size_t i = 0; i < bones.size(); ++i) {
            if (cnt[i] > 0) continue;
            int p = bones[i].parent;
            while (p >= 0 && cnt[(size_t)p] <= 0) p = bones[(size_t)p].parent;
            if (p >= 0) bones[i].pos = bones[(size_t)p].pos;
        }
    }

    int placed = 0;
    for (const auto& b : bones)
        if (fabsf(b.pos.x) + fabsf(b.pos.y) + fabsf(b.pos.z) > 1.0f) ++placed;
    if (!bones.empty() && placed < (int)bones.size() / 3) {
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
        if (any) {
            const float cx = (mn[0] + mx[0]) * 0.5f;
            const float cy0 = mn[1], cy1 = mx[1];
            const float cz = (mn[2] + mx[2]) * 0.5f;
            const float sx = (mx[0] - mn[0]) * 0.28f;
            auto Y = [&](float t) { return cy0 + (cy1 - cy0) * t; };
            for (auto& b : bones) {
                const std::string& n = b.name;
                if (n.find("head") != std::string::npos || n.find("Head") != std::string::npos)
                    b.pos = {cx, Y(0.95f), cz};
                else if (n.find("Foot") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.35f, Y(0.02f), cz};
                else if (n.find("Calf") != std::string::npos || n.find("Knee") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.3f,
                             Y(n.find("Knee") != std::string::npos ? 0.28f : 0.14f), cz};
                else if (n.find("Thigh") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.25f, Y(0.42f), cz};
                else if (n.find("Hand") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx), Y(0.58f), cz};
                else if (n.find("Forearm") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.85f, Y(0.62f), cz};
                else if (n.find("UpperArm") != std::string::npos || n.find("Clavicle") != std::string::npos)
                    b.pos = {cx + (n.find("_L_") != std::string::npos ? -sx : sx) * 0.55f, Y(0.78f), cz};
                else if (n.find("Pevis") != std::string::npos || n.find("Skirt") != std::string::npos)
                    b.pos = {cx, Y(0.48f), cz};
                else if (n.find("Spine01") != std::string::npos)
                    b.pos = {cx, Y(0.72f), cz};
                else if (n.find("Spine") != std::string::npos)
                    b.pos = {cx, Y(0.60f), cz};
                else if (n.find("Root") != std::string::npos)
                    b.pos = {cx, Y(0.50f), cz};
                else
                    b.pos = {cx, Y(0.55f), cz};
            }
        }
    }
    return !meshes.empty();
}

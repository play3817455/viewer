#include "dxg.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// GundamOnline .dxg model decoder.
//
// DXG byte layout used by this decoder. Do not treat old non-v15 function names
// as evidence for the current client; the current v15 IDB confirms the VRA-side
// geometry/mesh/material relationship in sub_C4E780 and sub_C19320.
//
// Mesh chunk (type 1 or world-field type 0x201) payload:
//   +4  u8   shape count (geometry slices belonging to this named mesh part)
//   +8  u32  block size   (next mesh block:  p + size + 28)
//   +12 u16  nPos
//   +14 u16  nNrm
//   +16 u16  nUv0
//   +18 u16  nUv1
//   +20 u16  nExtra
//   +24 u32  nWeights (float count)
//   +28 shape[0..nShapes)
//        shape header (16 bytes):
//          +0  u16 nVerts
//          +2  u16 nFaces
//          +8  u32 (extra data size)
//          +12 u32 block size    (next shape: s + size + 16)
//          +16 vertex records: nVerts * u16[5] = (pos, nrm, uv0, uv1, extra),
//                              0xFFFF = unused slot
//          after vertex records: face indices, nFaces * u16[3], then optional
//          per-vertex bone palette data (extra-data size at shape +8)
//   after the last shape:
//        pos[nPos] (float3) | nrm[nNrm] (float3) | uv0[nUv0] (float2)
//        | uv1[nUv1] (float2) | extra[nExtra] (u32) | weights[nWeights] (float)
// ---------------------------------------------------------------------------

namespace {

constexpr uint16_t kUnused = 0xFFFF;

uint16_t Rd16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

uint32_t Rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

float Rdf(const uint8_t* p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}

bool Finite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           fabsf(x) < 1.0e6f && fabsf(y) < 1.0e6f && fabsf(z) < 1.0e6f;
}

void FaceNormals(DxgMesh& m) {
    m.nrm.assign(m.pos.size(), DxgVec3{});
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

// Flatten one shape into a renderable mesh (LOD0 only).
bool BuildShape(const uint8_t* shape, size_t avail, const uint8_t* posArr, uint16_t nPos,
                const uint8_t* nrmArr, uint16_t nNrm, const uint8_t* uvArr, uint16_t nUv,
                const uint8_t* uv1Arr, uint16_t nUv1,
                const std::string& name, const std::vector<DxgBone>& skel,
                DxgMesh& out) {
    if (avail < 28) return false;
    const uint16_t nVerts = Rd16(shape);
    const uint16_t nFaces = Rd16(shape + 2);
    if (nVerts == 0 || nFaces == 0) return false;
    const size_t need = 16u + (size_t)nVerts * 10u + (size_t)nFaces * 6u;
    if (need > avail) return false;

    const uint8_t* rec = shape + 16;
    const uint8_t* ib = rec + (size_t)nVerts * 10u;

    out.name = name;
    out.nVerts = nVerts;
    out.nFaces = nFaces;
    // LOD selection is controlled by the paired VRA records. Mesh names such as
    // *_GND_01 are valid render meshes in some VRA files, so don't infer LOD from
    // the DXG name.
    out.lod1 = false;
    out.pos.assign(nVerts, DxgVec3{});
    out.nrm.assign(nVerts, DxgVec3{});
    out.uv.assign(nVerts, DxgVec2{});
    out.uv1.assign(nVerts, DxgVec2{});

    bool anyNrm = false;
    for (uint16_t i = 0; i < nVerts; ++i) {
        const uint8_t* r = rec + (size_t)i * 10u;
        const uint16_t ip = Rd16(r);
        const uint16_t in = Rd16(r + 2);
        const uint16_t iu = Rd16(r + 4);
        const uint16_t iu1 = Rd16(r + 6);

        if (ip != kUnused && ip < nPos) {
            const uint8_t* p = posArr + (size_t)ip * 12u;
            DxgVec3 v{Rdf(p), Rdf(p + 4), Rdf(p + 8)};
            if (Finite3(v.x, v.y, v.z)) out.pos[i] = v;
        }
        if (in != kUnused && in < nNrm) {
            const uint8_t* p = nrmArr + (size_t)in * 12u;
            DxgVec3 v{Rdf(p), Rdf(p + 4), Rdf(p + 8)};
            if (Finite3(v.x, v.y, v.z)) {
                out.nrm[i] = v;
                anyNrm = true;
            }
        }
        if (iu != kUnused && iu < nUv) {
            const uint8_t* p = uvArr + (size_t)iu * 8u;
            out.uv[i] = DxgVec2{Rdf(p), Rdf(p + 4)};
        }
        if (iu1 != kUnused && iu1 < nUv1) {
            const uint8_t* p = uv1Arr + (size_t)iu1 * 8u;
            out.uv1[i] = DxgVec2{Rdf(p), Rdf(p + 4)};
        }
    }

    out.idx.clear();
    out.idx.reserve((size_t)nFaces * 3u);
    for (uint32_t f = 0; f < (uint32_t)nFaces * 3u; ++f) {
        const uint16_t v = Rd16(ib + (size_t)f * 2u);
        if (v >= nVerts) continue;
        out.idx.push_back(v);
    }
    out.idx.resize(out.idx.size() - (out.idx.size() % 3));
    if (out.idx.empty()) return false;

    // ---- skinning data -----------------------------------------------------
    // Per-shape rigid bone palette and per-vertex palette slot:
    //   [shape+16 ] vertex records  10 * nVerts
    //   [..       ] index buffer     6 * nFaces
    //   [..       ] u32 paletteSize
    //   [..       ] paletteSize bytes of zero separated bone names (shape palette)
    //   [..       ] 4 bytes per vertex, size at shape+8 (must be nVerts * 4).
    //               byte 0 of each record is treated as the palette slot.
    out.boneNames.clear();
    out.vertBone.clear();
    const uint8_t nc = shape[4];
    if (nc > 0 && nc <= 26) {
        const size_t ibRel = 16u + (size_t)nVerts * 10u + (size_t)nFaces * 6u;
        if (ibRel + 4 <= avail) {
            const uint32_t tblSize = Rd32(shape + ibRel);
            const size_t tblOff = ibRel + 4;
            if (tblSize > 0 && tblOff + tblSize <= avail) {
                size_t p = tblOff;
                const size_t end = tblOff + tblSize;
                std::vector<std::string> cand;
                bool ok = true;
                for (int i = 0; i < nc; ++i) {
                    if (p >= end) { ok = false; break; }
                    const size_t s = p;
                    while (p < end && shape[p] != 0) ++p;
                    if (p == s) { ok = false; break; }
                    cand.push_back(std::string((const char*)shape + s, p - s));
                    ++p;
                }
                if (ok) {
                    // keep it only when the names really are skeleton bones
                    bool all = true;
                    for (const auto& n : cand) {
                        bool found = false;
                        for (const auto& b : skel) {
                            if (b.name == n) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            all = false;
                            break;
                        }
                    }
                    if (all) out.boneNames = cand;
                }
            }
            if (!out.boneNames.empty()) {
                const size_t blkOff = tblOff + tblSize;
                const uint32_t blkSize = Rd32(shape + 8);
                if (blkSize == (uint32_t)nVerts * 4u && blkOff + blkSize <= avail) {
                    // 4 bytes per vertex; byte 0 is the palette slot.
                    out.vertBone.resize(nVerts);
                    for (uint16_t i = 0; i < nVerts; ++i) {
                        const uint8_t slot = shape[blkOff + 4u * (size_t)i];
                        out.vertBone[i] = (slot < out.boneNames.size()) ? slot : 0;
                    }
                }
            }
            if (out.vertBone.empty()) out.vertBone.assign(nVerts, 0);
        }
    }

    if (!anyNrm) FaceNormals(out);
    return true;
}

// Skeleton chunk (type 36):
//   +0        u32  nameBytes
//   +4        char[nameBytes]  zero separated bone names
//   +4+nlen   per bone: u8 idx, u8 parent, u8 child, u8 sibling   (0xFF = none)
//   then      per bone: float[16] bind pose matrix (model space)
void ParseSkeleton(const uint8_t* pl, size_t sz, std::vector<DxgBone>& bones) {
    if (sz < 8) return;
    const uint32_t nlen = Rd32(pl);
    if (nlen > sz - 4) return;
    std::vector<std::string> names;
    std::string cur;
    for (uint32_t i = 0; i < nlen; ++i) {
        const uint8_t c = pl[4 + i];
        if (c == 0) {
            if (!cur.empty()) names.push_back(cur);
            cur.clear();
        } else if (c >= 32 && c < 127) {
            cur.push_back((char)c);
        }
    }
    if (!cur.empty()) names.push_back(cur);
    if (names.empty()) return;

    const size_t n = names.size();
    const uint8_t* hier = pl + 4 + nlen;
    const size_t left = sz - (4 + nlen);
    if (left < n * 4) return;

    bones.clear();
    bones.resize(n);
    for (size_t i = 0; i < n; ++i) {
        bones[i].name = names[i];
        const uint8_t* h = hier + 4 * i;
        auto idx = [](uint8_t v) { return v == 0xFF ? -1 : (int)v; };
        bones[i].parent = idx(h[1]);
        bones[i].child = idx(h[2]);
        bones[i].sibling = idx(h[3]);
    }

    const uint8_t* mats = hier + n * 4;
    if (left >= n * 4 + n * 64) {
        for (size_t i = 0; i < n; ++i) {
            memcpy(bones[i].m, mats + 64 * i, 64);
            bones[i].pos = DxgVec3{bones[i].m[12], bones[i].m[13], bones[i].m[14]};
        }
    } else {
        // fall back: keep hierarchy only
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

bool DxgModel::Parse(const uint8_t* d, size_t size) {
    names.clear();
    meshes.clear();
    bones.clear();
    if (!d || size < 32 || memcmp(d, "DXG ", 4) != 0) return false;
    const uint32_t nameBytes = Rd32(d + 20);
    if (nameBytes > size - 24) return false;
    {
        std::string cur;
        for (uint32_t i = 0; i < nameBytes; ++i) {
            const char c = (char)d[24 + i];
            if (c == 0) {
                if (!cur.empty()) names.push_back(cur);
                cur.clear();
            } else if ((unsigned char)c >= 32) {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) names.push_back(cur);
    }

    // Skeleton first: BuildShape validates every shape's bone palette against the
    // bone list, so the bones must be known before the meshes are parsed.
    {
        size_t so = 24 + nameBytes;
        while (so + 8 <= size) {
            const uint32_t csz = Rd32(d + so + 4);
            if (so + 8 + csz > size) break;
            if (Rd32(d + so) == 36) ParseSkeleton(d + so + 8, csz, bones);
            so += 8 + (size_t)csz;
        }
    }

    size_t off = 24 + nameBytes;
    size_t nameI = 0;
    while (off + 8 <= size) {
        const uint32_t typ = Rd32(d + off);
        const uint32_t csz = Rd32(d + off + 4);
        if (off + 8 + csz > size) break;
        const uint8_t* pl = d + off + 8;
        // World-field DXGs use chunk type 0x201 for the same mesh-block
        // body that other DXGs store under type 1.  The active scene exporter
        // already normalizes 0x201 to 1 before using this decoder; accept the
        // verified equivalent directly so the desktop viewer can preview the
        // original file and retain its VRA mesh/material names.
        if ((typ == 1 || typ == 0x201) && csz >= 28) {
            const uint8_t* p = pl;
            const uint8_t* pend = pl + csz;
            while (p + 28 <= pend) {
                const uint8_t nShapes = p[4];
                const uint32_t blockSize = Rd32(p + 8);
                const size_t remaining = (size_t)(pend - p);
                // Some DXG files end a type-1 chunk with a short auxiliary
                // record. Do not interpret it as a mesh header or step beyond
                // the enclosing chunk when its size is not a valid mesh block.
                if (nShapes == 0 || blockSize == 0 || blockSize > remaining - 28u)
                    break;
                const uint8_t* blockEnd = p + 28u + blockSize;
                const uint16_t nPos = Rd16(p + 12);
                const uint16_t nNrm = Rd16(p + 14);
                const uint16_t nUv0 = Rd16(p + 16);
                const uint16_t nUv1 = Rd16(p + 18);
                const uint32_t nW = Rd32(p + 24);

                // Walk the shape chain; vertex attribute arrays follow the last shape.
                const uint8_t* sh = p + 28;
                std::vector<const uint8_t*> shapes;
                bool shapeChainValid = true;
                for (uint32_t si = 0; si < nShapes; ++si) {
                    if (sh > blockEnd || (size_t)(blockEnd - sh) < 16u) {
                        shapeChainValid = false;
                        break;
                    }
                    const uint32_t sb = Rd32(sh + 12);
                    if (sb > (size_t)(blockEnd - sh) - 16u) {
                        shapeChainValid = false;
                        break;
                    }
                    shapes.push_back(sh);
                    sh += (size_t)sb + 16u;
                }
                const size_t posBytes = (size_t)nPos * 12u;
                const size_t nrmBytes = (size_t)nNrm * 12u;
                const size_t arrayBytes = posBytes + nrmBytes + (size_t)nUv0 * 8u +
                                          (size_t)Rd16(p + 18) * 8u +
                                          (size_t)Rd16(p + 20) * 4u + (size_t)nW * 4u;
                const uint8_t* posArr = sh;
                const uint8_t* nrmArr = sh;
                const uint8_t* uvArr = sh;
                const uint8_t* uv1Arr = sh;
                const bool arraysValid = sh <= blockEnd && arrayBytes <= (size_t)(blockEnd - sh);
                if (arraysValid) {
                    nrmArr = posArr + posBytes;
                    uvArr = nrmArr + nrmBytes;
                    uv1Arr = uvArr + (size_t)nUv0 * 8u;
                }

                // Every shape is a sub-mesh of the part (e.g. GND_00 keeps the body in
                // shape 0 and the right leg in shape 1); they are NOT LOD levels.
                if (shapeChainValid && shapes.size() == nShapes && arraysValid) {
                    std::string nm = nameI < names.size() ? names[nameI]
                                                          : ("mesh" + std::to_string(nameI));
                    for (const uint8_t* shape : shapes) {
                        DxgMesh m;
                        const size_t avail = (size_t)(blockEnd - shape);
                        if (BuildShape(shape, avail, posArr, nPos, nrmArr, nNrm, uvArr, nUv0,
                                       uv1Arr, nUv1,
                                       nm, bones, m))
                            meshes.push_back(std::move(m));
                    }
                }
                ++nameI;
                p = blockEnd;
            }
        } else if (typ == 36) {
            ParseSkeleton(pl, csz, bones);
        }
        off += 8 + csz;
        if (typ == 0 && csz == 0) break;
    }

    bool anyMatrix = false;
    for (const auto& b : bones) {
        if (fabsf(b.pos.x) + fabsf(b.pos.y) + fabsf(b.pos.z) > 0.001f) {
            anyMatrix = true;
            break;
        }
    }
    if (!anyMatrix) LayoutBones(bones, meshes);  // fallback if no bind poses
    return !meshes.empty();
}

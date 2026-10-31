// Offline binding verification: includes the REAL dxg.cpp / anim.cpp and checks
// the skinning pipeline on actual game data (no GUI needed).
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <limits>

#include "dxg.h"
#include "anim.h"

static std::vector<uint8_t> readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return {}; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d((size_t)n);
    fread(d.data(), 1, (size_t)n, f);
    fclose(f);
    return d;
}

static inline float getf(const float* m, int r, int c) { return m[c * 4 + r]; }

// column-major M = a*b
static void mul4(const float* a, const float* b, float* o) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

static void xform(const float* M, const DxgVec3& p, DxgVec3& q) {
    q.x = M[0] * p.x + M[4] * p.y + M[8] * p.z + M[12];
    q.y = M[1] * p.x + M[5] * p.y + M[9] * p.z + M[13];
    q.z = M[2] * p.x + M[6] * p.y + M[10] * p.z + M[14];
}

#include <filesystem>
namespace fs = std::filesystem;

static void scanDir(const char* dir) {
    for (auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.path().extension() == ".dxg") {
            std::vector<uint8_t> d = readFile(e.path().string().c_str());
            DxgModel m;
            int skin = 0;
            if (m.Parse(d.data(), d.size()))
                for (auto& mm : m.meshes) if (!mm.boneNames.empty() && !mm.vertBone.empty()) ++skin;
            printf("%-50s bones=%zu meshes=%zu skinned=%d\n",
                   e.path().filename().string().c_str(), m.bones.size(), m.meshes.size(), skin);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: _bindtest <model.dxg> [motion.mrb] | <dir>\n"); return 1; }
    std::error_code ec;
    if (fs::is_directory(argv[1], ec)) { scanDir(argv[1]); return 0; }
    auto d = readFile(argv[1]);
    if (d.empty()) return 1;
    DxgModel model;
    if (!model.Parse(d.data(), d.size())) { printf("parse failed\n"); return 1; }
    printf("bones=%zu meshes=%zu\n", model.bones.size(), model.meshes.size());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<DxgVec3> lt;          // empty -> bind pose translation
    std::vector<float> lr;            // empty -> bind pose rotation
    std::vector<float> world;
    BuildBoneWorld(model.bones, lt, lr, world);

    // bind-pose identity check: skinned vertex should equal original vertex
    int skinnedMeshes = 0, skinVerts = 0;
    float maxBindErr = 0.0f;
    for (auto& m : model.meshes) {
        if (m.boneNames.empty() || m.vertBone.empty() || m.pos.empty()) continue;
        // palette slot -> skeleton bone index
        std::vector<int> pal(m.boneNames.size(), -1);
        for (size_t s = 0; s < m.boneNames.size(); ++s)
            for (size_t bi = 0; bi < model.bones.size(); ++bi)
                if (model.bones[bi].name == m.boneNames[s]) { pal[s] = (int)bi; break; }
        ++skinnedMeshes;
        for (size_t i = 0; i < m.pos.size(); ++i) {
            int slot = m.vertBone[i];
            if (slot >= (int)pal.size()) continue;
            int bi = pal[slot];
            if (bi < 0) continue;
            const float* W = &world[(size_t)bi * 16];
            const float* Ib = model.bones[bi].m;
            float M[16];
            mul4(W, Ib, M);
            DxgVec3 q;
            xform(M, m.pos[i], q);
            float e = fabsf(q.x - m.pos[i].x) + fabsf(q.y - m.pos[i].y) + fabsf(q.z - m.pos[i].z);
            if (e > maxBindErr) maxBindErr = e;
            ++skinVerts;
        }
    }
    printf("skinnedMeshes=%d skinVerts=%d maxBindErr=%.4f\n", skinnedMeshes, skinVerts, maxBindErr);

    // report bone order: are all parents before children?
    int orderViolations = 0;
    for (size_t i = 0; i < model.bones.size(); ++i) {
        int p = model.bones[i].parent;
        if (p >= 0 && (size_t)p >= i) ++orderViolations;  // parent index >= child index => not parent-first
    }
    printf("orderViolations(parentIndex>=childIndex)=%d\n", orderViolations);

    // ---- optional motion ----
    if (argc >= 3) {
        auto md = readFile(argv[2]);
        if (!md.empty()) {
            MrbMotion mot;
            if (mot.Parse(md.data(), md.size())) {
                mot.DumpDebug();
                printf("motion bones=%u nKeys=%u dur=%u matchedNames=%d\n",
                       mot.nBones, mot.nKeys, mot.Duration(), (int)mot.boneNames.size());
                int matched = 0;
                std::vector<int> skelIdx(mot.boneNames.size(), -1);
                for (size_t b = 0; b < mot.boneNames.size(); ++b) {
                    for (size_t bi = 0; bi < model.bones.size(); ++bi)
                        if (model.bones[bi].name == mot.boneNames[b]) { skelIdx[b] = (int)bi; ++matched; break; }
                }
                printf("  motion->skeleton name matches: %d/%zu\n", matched, mot.boneNames.size());
                const uint32_t dur = mot.Duration();
                // dump root quats at two times to see if frame 0 is rest and frames vary
                for (float tt : {0.0f, dur * 0.5f}) {
                    std::vector<DxgVec3> oT;
                    std::vector<float> oR;
                    mot.Sample(tt, oT, oR);
                    printf("  quat@t=%.0f bone0=(%.3f,%.3f,%.3f,%.3f) bone1=(%.3f,%.3f,%.3f,%.3f)\n",
                           tt, oR[0], oR[1], oR[2], oR[3], oR[4], oR[5], oR[6], oR[7]);
                }
                // bind-pose skinned bbox
                auto skinBBox = [&](const std::vector<float>& wmat) {
                    DxgVec3 mn{1e9f,1e9f,1e9f}, mx{-1e9f,-1e9f,-1e9f};
                    for (auto& m : model.meshes) {
                        if (m.boneNames.empty() || m.vertBone.empty() || m.pos.empty()) continue;
                        std::vector<int> pal(m.boneNames.size(), -1);
                        for (size_t s = 0; s < m.boneNames.size(); ++s)
                            for (size_t bi = 0; bi < model.bones.size(); ++bi)
                                if (model.bones[bi].name == m.boneNames[s]) { pal[s] = (int)bi; break; }
                        for (size_t i = 0; i < m.pos.size(); ++i) {
                            int slot = m.vertBone[i];
                            if (slot >= (int)pal.size()) continue;
                            int bi = pal[slot];
                            if (bi < 0) continue;
                            const float* W = &wmat[(size_t)bi * 16];
                            const float* Ib = model.bones[bi].m;
                            float M[16];
                            mul4(W, Ib, M);
                            DxgVec3 q;
                            xform(M, m.pos[i], q);
                            if (q.x<mn.x)mn.x=q.x; if(q.y<mn.y)mn.y=q.y; if(q.z<mn.z)mn.z=q.z;
                            if (q.x>mx.x)mx.x=q.x; if(q.y>mx.y)mx.y=q.y; if(q.z>mx.z)mx.z=q.z;
                        }
                    }
                    return std::make_pair(mn, mx);
                };
                auto bb0 = skinBBox(world);
                printf("  bindBBox min(%.1f,%.1f,%.1f) max(%.1f,%.1f,%.1f)\n",
                       bb0.first.x,bb0.first.y,bb0.first.z,bb0.second.x,bb0.second.y,bb0.second.z);
                const float testT[] = {0.0f, dur * 0.25f, dur * 0.5f, dur * 0.75f};
                for (float t : testT) {
                    std::vector<DxgVec3> outT;
                    std::vector<float> outR;
                    mot.Sample(t, outT, outR);
                    std::vector<DxgVec3> lt2(model.bones.size(), {nan, nan, nan});
                    std::vector<float> lr2((size_t)model.bones.size() * 4, nan);
                    for (size_t b = 0; b < mot.boneNames.size(); ++b) {
                        int si = skelIdx[b];
                        if (si < 0) continue;
                        if (outR.size() > b * 4 + 3) {
                            lr2[(size_t)si * 4 + 0] = outR[b * 4 + 0];
                            lr2[(size_t)si * 4 + 1] = outR[b * 4 + 1];
                            lr2[(size_t)si * 4 + 2] = outR[b * 4 + 2];
                            lr2[(size_t)si * 4 + 3] = outR[b * 4 + 3];
                        }
                    }
                    std::vector<float> w2;
                    BuildBoneWorld(model.bones, lt2, lr2, w2);
                    float maxJoint = 0.0f, maxVert = 0.0f;
                    int nanW = 0;
                    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
                        const float* a = &world[bi * 16];
                        const float* c = &w2[bi * 16];
                        float dm = fabsf(c[12] - a[12]) + fabsf(c[13] - a[13]) + fabsf(c[14] - a[14]);
                        if (dm > maxJoint) maxJoint = dm;
                        if (!std::isfinite(c[12]) || !std::isfinite(c[13]) || !std::isfinite(c[14])) ++nanW;
                    }
                    auto bb = skinBBox(w2);
                    printf("  t=%.0fms maxJointMove=%.2f maxVertMove=%.2f nanW=%d bbox(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
                           t, maxJoint, maxVert, nanW,
                           bb.first.x,bb.first.y,bb.first.z,bb.second.x,bb.second.y,bb.second.z);
                }
            } else printf("motion parse failed\n");
        }
    }
    return 0;
}

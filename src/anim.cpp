#include "anim.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {

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

uint16_t Rd16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

// quaternion -> row-major 4x4 (column-major storage like the rest of the code)
void QuatToMat(const float* q, float* o) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    o[0] = 1.0f - (yy + zz); o[4] = xy - wz;       o[8]  = xz + wy;       o[12] = 0.0f;
    o[1] = xy + wz;          o[5] = 1.0f - (xx + zz); o[9] = yz - wx;      o[13] = 0.0f;
    o[2] = xz - wy;          o[6] = yz + wx;       o[10] = 1.0f - (xx + yy); o[14] = 0.0f;
    o[3] = 0.0f;             o[7] = 0.0f;          o[11] = 0.0f;          o[15] = 1.0f;
}

// world rotation (row-major 3x3) and world translation from an inverse bind matrix
void DecomposeInv(const float* m, float R[9], float t[3]) {
    // m is column-major: A[row][col] = m[col*4+row], and A = R^T.
    // Therefore R[row][col] = A[col][row] = m[row*4+col].
    R[0] = m[0]; R[1] = m[1]; R[2] = m[2];
    R[3] = m[4]; R[4] = m[5]; R[5] = m[6];
    R[6] = m[8]; R[7] = m[9]; R[8] = m[10];
    const float b[3] = {m[12], m[13], m[14]};
    t[0] = -(R[0] * b[0] + R[1] * b[1] + R[2] * b[2]);
    t[1] = -(R[3] * b[0] + R[4] * b[1] + R[5] * b[2]);
    t[2] = -(R[6] * b[0] + R[7] * b[1] + R[8] * b[2]);
}

void Mul4(const float* a, const float* b, float* o) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

}  // namespace

// Layout of the data section (confirmed against sub_B3FDB0, the rhyme motion
// loader, which reads three key channels and one index per (key, bone, channel)):
//   +0   u32 nKeys
//   +4   u32 4
//   +8   u32 0
//   +12  u32 nBones
//   +16  u32[nKeys]          key time in ms
//   ..   u32[8]              block descriptor (stride 12 for the vec3 channel)
//   ..   float[]             translation keys   (posCount * 3)
//   ..   float[]             rotation keys      (rotCount * 4, unit quaternions)
//   ..   u16[nKeys*nBones*3] (scale, translation, rotation) key index per key/bone
bool MrbMotion::Parse(const uint8_t* d, size_t size) {
    boneNames.clear();
    times.clear();
    pos.clear();
    posKeys.clear();
    rotKeys.clear();
    idx.clear();
    hasRot = false;
    nKeys = 0;
    nBones = 0;
    if (!d || size < 72 || memcmp(d, "MRB\0", 4) != 0) return false;
    const uint32_t nameBytes = Rd32(d + 60);
    if (nameBytes == 0 || 64 + nameBytes > size) return false;
    {
        std::string cur;
        for (uint32_t i = 0; i < nameBytes; ++i) {
            const char c = (char)d[64 + i];
            if (c == 0) {
                if (!cur.empty()) boneNames.push_back(cur);
                cur.clear();
            } else if ((unsigned char)c >= 32) {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) boneNames.push_back(cur);
    }
    if (boneNames.empty()) return false;

    // the data section starts on a 4 byte boundary: the name block is padded
    // (e.g. nameBytes 86 -> 64+86 = 150 -> data at 152)
    const size_t ds = (64 + nameBytes + 3) & ~(size_t)3;
    if (ds + 16 > size) return false;
    nKeys = Rd32(d + ds);
    if (nKeys == 0 || nKeys > 100000) return false;
    // The index table has one row per name of the name table: the loader builds it
    // from that list (sub_B71290 / sub_B72E80 walk the names). So the row count is
    // the NAME count, not the bone count at ds+12 - that one is smaller (e.g. 17 vs
    // 20) and using it misaligns every row, which showed up as heavy per frame
    // jitter. Verified: with rowCount = names the rotation index of bone 0 walks
    // 1,2,3..22, bone 1 walks 23..44, bone 2 walks 45..67 (key frame compression).
    nBones = (uint32_t)boneNames.size();

    const size_t timeOff = ds + 16;
    if (timeOff + 4 * (size_t)nKeys + 32 > size) return false;
    times.resize(nKeys);
    for (uint32_t i = 0; i < nKeys; ++i) times[i] = Rd32(d + timeOff + 4 * (size_t)i);

    const size_t dataOff = timeOff + 4 * (size_t)nKeys + 32;

    // Index table = trailing block, with an 8 byte footer of spare u16 at the very
    // end. Its row count is the NAME count (the loader builds it from the name
    // table), NOT the ds+12 bone count. Locate it from the file tail.
    const size_t idxNeed = (size_t)nKeys * nBones * 3;
    if (idxNeed == 0 || size < 8 + idxNeed * 2) return false;
    const size_t idxOff = size - 8 - idxNeed * 2;
    if (idxOff < dataOff) return false;
    {
        bool ok = true;
        for (size_t k = 0; k < idxNeed; ++k)
            if (Rd16(d + idxOff + 2 * k) >= 60000) { ok = false; break; }
        if (!ok) return false;
    }
    idx.resize(idxNeed);
    for (size_t k = 0; k < idxNeed; ++k) idx[k] = Rd16(d + idxOff + 2 * k);

    // Per-row layout of the index is (translation, rotation, scale) as 3 u16 at
    // columns 0,1,2. Derive each channel's pool size from the column maxima; the
    // on-disk "descriptor" words are not the pool counts (verified: col0 max 87 =>
    // 88 translation vec3 at dataOff; col1 max 396 => ~395 rotation quats in the
    // unit-quaternion region that follows; col2 all-zero => no scale).
    uint16_t c0max = 0, c1max = 0, c2max = 0;
    for (size_t i = 0; i < idxNeed; ++i) {
        const uint16_t v = idx[i];
        const int col = (int)(i % 3);
        if (col == 0 && v > c0max) c0max = v;
        else if (col == 1 && v > c1max) c1max = v;
        else if (col == 2 && v > c2max) c2max = v;
    }
    const uint32_t transCount = (uint32_t)c0max + 1;   // col0 = translation (vec3)
    const uint32_t rotCountMax = (uint32_t)c1max + 1; // col1 = rotation  (quat)
    const uint32_t scaleCount = (uint32_t)c2max + 1;  // col2 = scale     (vec3, ~0)

    // translation pool sits right at dataOff (vec3 per key)
    const size_t transOff = dataOff;
    if (transOff + (size_t)transCount * 12 > idxOff) return false;
    posKeys.resize((size_t)transCount * 3);
    for (size_t i = 0; i < (size_t)transCount * 3; ++i) posKeys[i] = Rdf(d + transOff + 4 * i);

    // rotation pool = the run of unit quaternions immediately after the translation
    // pool. Locate its start by scanning for a long consecutive unit-quat run.
    auto isUQ = [&](size_t o) {
        float v[4];
        for (int kk = 0; kk < 4; ++kk) memcpy(&v[kk], d + o + 4 * kk, 4);
        const float L = sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);
        return L > 0.985f && L < 1.015f;
    };
    size_t rotOff = 0;
    {
        size_t s = transOff + (size_t)transCount * 12;
        while (s + 16 <= idxOff) {
            size_t c = 0;
            while (s + 16 * (c + 1) <= idxOff && isUQ(s + 16 * c)) ++c;
            if (c >= 30) { rotOff = s; break; }
            s += 16 * (c > 0 ? c : 1);
        }
    }
    if (!rotOff) return false;
    uint32_t rotCount = rotCountMax;
    while (rotOff + (size_t)rotCount * 16 > idxOff) --rotCount;
    if (rotCount == 0) return false;
    rotKeys.resize((size_t)rotCount * 4);
    for (size_t i = 0; i < (size_t)rotCount * 4; ++i) rotKeys[i] = Rdf(d + rotOff + 4 * i);
    hasRot = true;
    (void)scaleCount;

    // Key frame compression makes each (bone, channel) walk forward through the key
    // array, so the indices it references form a dense run (measured: bone 0 walks
    // 1..22, bone 1 23..44, bone 2 45..67, ... covering the whole array). A handful
    // of entries are isolated - a single value with no neighbour in that run - and
    // would pop for exactly one frame. Hold the previous frame's index for those.
    for (uint32_t b = 0; b < nBones; ++b) {
        for (int s = 0; s < 3; ++s) {
            std::vector<uint16_t> vals(nKeys);
            for (uint32_t k = 0; k < nKeys; ++k)
                vals[k] = idx[((size_t)b * nKeys + k) * 3 + s];
            uint16_t last = vals[0];
            for (uint32_t k = 1; k < nKeys; ++k) {
                bool dense = false;
                for (uint32_t j = 0; j < nKeys && !dense; ++j) {
                    const int dv = (int)vals[j] - (int)vals[k];
                    if (dv == 1 || dv == -1) dense = true;
                }
                if (dense)
                    last = vals[k];
                else
                    idx[((size_t)b * nKeys + k) * 3 + s] = last;
            }
        }
    }

    // Frame 0 is the odd slice out: for several bones its index lands outside the
    // dense run that frames 1..nKeys-1 form (C_Spine: 138 against its own run
    // 1..22, T_Backpack: 303 against a constant 0), which produced a single hard
    // snap at the start of the loop. Frames 1.. are consistent, so restart from
    // frame 1 whenever frame 0 disagrees with that run.
    if (nKeys >= 3) {
        for (uint32_t b = 0; b < nBones; ++b) {
            for (int s = 0; s < 3; ++s) {
                const size_t base = (size_t)b * nKeys * 3 + s;
                uint16_t lo = 0xFFFF, hi = 0;
                for (uint32_t k = 1; k < nKeys; ++k) {
                    const uint16_t v = idx[base + (size_t)k * 3];
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                const uint16_t v0 = idx[base];
                if (v0 < lo || v0 > hi) idx[base] = idx[base + 3];
            }
        }
    }
    return true;
}

bool MrbMotion::Sample(float timeMs, std::vector<DxgVec3>& outT,
                       std::vector<float>& outR) const {
    if (!Valid()) return false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    outT.assign(nBones, DxgVec3{nan, nan, nan});
    outR.assign((size_t)nBones * 4, nan);
    const float dur = (float)Duration();
    if (dur > 0.0f) {
        while (timeMs > dur) timeMs -= dur;
        if (timeMs < 0.0f) timeMs = 0.0f;
    }
    uint32_t k1 = 0;
    for (uint32_t i = 0; i + 1 < nKeys; ++i) {
        if ((float)times[i] <= timeMs && timeMs <= (float)times[i + 1]) {
            k1 = i;
            break;
        }
        if ((float)times[i + 1] < timeMs) k1 = i + 1;
    }
    const uint32_t k0 = k1;
    const uint32_t k2 = (k1 + 1 < nKeys) ? k1 + 1 : k1;
    const float t0 = (float)times[k0];
    const float t1 = (float)times[k2];
    float f = 0.0f;
    if (t1 > t0) f = (timeMs - t0) / (t1 - t0);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    // Index table is bone-major: for every bone it holds nKeys triples of
    // (translation, rotation, scale) key indices at columns 0,1,2.
    for (uint32_t b = 0; b < nBones; ++b) {
        const size_t i0 = ((size_t)b * nKeys + k0) * 3;
        const size_t i1 = ((size_t)b * nKeys + k2) * 3;
        if (i1 + 2 >= idx.size()) break;
        const size_t p0 = idx[i0 + 0], p1 = idx[i1 + 0];   // col0 = translation
        if (useTrans && (p0 + 1) * 3 <= posKeys.size() && (p1 + 1) * 3 <= posKeys.size()) {
            const float* a = &posKeys[p0 * 3];
            const float* c = &posKeys[p1 * 3];
            outT[b].x = a[0] + (c[0] - a[0]) * f;
            outT[b].y = a[1] + (c[1] - a[1]) * f;
            outT[b].z = a[2] + (c[2] - a[2]) * f;
        }
        if (!hasRot) continue;
        // col1 = rotation key index (the unit-quaternion pool). The rotation pool
        // follows the translation pool; reading rotation from the wrong column
        // (col0/col2) froze every bone onto rotKeys[0] and stopped all animation.
        const size_t r0 = idx[i0 + 1], r1 = idx[i1 + 1];
        if ((r0 + 1) * 4 <= rotKeys.size() && (r1 + 1) * 4 <= rotKeys.size()) {
            const float* a = &rotKeys[r0 * 4];
            const float* c = &rotKeys[r1 * 4];
            const float dp = a[0] * c[0] + a[1] * c[1] + a[2] * c[2] + a[3] * c[3];
            const float sg = dp < 0.0f ? -1.0f : 1.0f;
            float q[4];
            for (int k = 0; k < 4; ++k) q[k] = a[k] + (sg * c[k] - a[k]) * f;
            float L = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (!(L > 1.0e-6f)) L = 1.0f;
            for (int k = 0; k < 4; ++k) outR[(size_t)b * 4 + k] = q[k] / L;
        }
    }
    return true;
}

void MrbMotion::DumpDebug() const {
    printf("[DUMP] nBones=%u nKeys=%u dur=%u hasRot=%d rotKeys=%zu idx=%zu\n",
           nBones, nKeys, Duration(), (int)hasRot, rotKeys.size(), idx.size());
    printf("[DUMP] times:");
    for (uint32_t i = 0; i < nKeys && i < 12; ++i) printf(" %u", times[i]);
    printf(" ... last=%u\n", times[nKeys - 1]);
    printf("[DUMP] rotKeys[0..5]:");
    for (size_t i = 0; i < rotKeys.size() / 4 && i < 6; ++i)
        printf(" (%.3f,%.3f,%.3f,%.3f)", rotKeys[i*4], rotKeys[i*4+1], rotKeys[i*4+2], rotKeys[i*4+3]);
    printf("\n");
    for (uint32_t k : {(uint32_t)0, (uint32_t)1, nKeys / 2, nKeys - 1}) {
        uint32_t ri = idx[((size_t)0 * nKeys + k) * 3 + 1];
        printf("[DUMP] b0 key%u ridx=%u quat=(%.3f,%.3f,%.3f,%.3f)\n", k, ri,
               rotKeys[ri*4], rotKeys[ri*4+1], rotKeys[ri*4+2], rotKeys[ri*4+3]);
    }
    // dump bone 0's three idx columns at several keys
    printf("[DUMP] b0 idx cols (bone-major) k: [c0,c1,c2]\n");
    uint32_t ks[] = {0,1,5,10,30,60};
    for (uint32_t k : ks) {
        uint16_t c0 = idx[((size_t)0 * nKeys + k) * 3 + 0];
        uint16_t c1 = idx[((size_t)0 * nKeys + k) * 3 + 1];
        uint16_t c2 = idx[((size_t)0 * nKeys + k) * 3 + 2];
        printf("   k%u: [%u,%u,%u]\n", k, c0, c1, c2);
    }
    // which column has the varying (1..22) run? check per-column range over all bones/keys
    for (int col = 0; col < 3; ++col) {
        uint16_t lo = 0xFFFF, hi = 0; uint32_t nz = 0;
        for (uint32_t b = 0; b < nBones; ++b)
            for (uint32_t k = 0; k < nKeys; ++k) {
                uint16_t v = idx[((size_t)b * nKeys + k) * 3 + col];
                if (v) ++nz;
                if (v < lo) lo = v; if (v > hi) hi = v;
            }
        printf("[DUMP] col%d range [%u..%u] nonzero=%u\n", col, lo, hi, nz);
    }
}

void BuildBoneWorld(const std::vector<DxgBone>& bones,
                    const std::vector<DxgVec3>& localTrans,
                    const std::vector<float>& localRot,
                    std::vector<float>& worldOut) {
    const size_t n = bones.size();
    worldOut.assign(n * 16, 0.0f);
    std::vector<float> bindWorld(n * 16, 0.0f);
    std::vector<float> bindLocal(n * 16, 0.0f);

    // bind pose world matrices from the stored inverse bind matrices
    for (size_t i = 0; i < n; ++i) {
        float R[9], t[3];
        DecomposeInv(bones[i].m, R, t);
        float* W = &bindWorld[i * 16];
        W[0] = R[0]; W[4] = R[1]; W[8] = R[2];  W[12] = t[0];
        W[1] = R[3]; W[5] = R[4]; W[9] = R[5];  W[13] = t[1];
        W[2] = R[6]; W[6] = R[7]; W[10] = R[8]; W[14] = t[2];
        W[3] = 0;    W[7] = 0;    W[11] = 0;    W[15] = 1.0f;
    }
    // local = parentWorld^-1 * world  (rotation part only, translation recomputed)
    for (size_t i = 0; i < n; ++i) {
        const float* W = &bindWorld[i * 16];
        float* L = &bindLocal[i * 16];
        const int p = bones[i].parent;
        if (p >= 0 && (size_t)p < n) {
            const float* PW = &bindWorld[(size_t)p * 16];
            // L = PW^T(rotation) * W  -> rotation = R_p^T * R_i, translation = R_p^T*(t_i - t_p)
            // Rt = R_parent^T ;  R_p[row][col] = PW[col*4+row]  =>  Rt[row][col] = PW[row*4+col]
            float Rt[9];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) Rt[r * 3 + c] = PW[r * 4 + c];
            float d[3] = {W[12] - PW[12], W[13] - PW[13], W[14] - PW[14]};
            float lt[3] = {Rt[0] * d[0] + Rt[1] * d[1] + Rt[2] * d[2],
                           Rt[3] * d[0] + Rt[4] * d[1] + Rt[5] * d[2],
                           Rt[6] * d[0] + Rt[7] * d[1] + Rt[8] * d[2]};
            float lr[9];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    lr[r * 3 + c] = Rt[r * 3 + 0] * W[c * 4 + 0] +
                                    Rt[r * 3 + 1] * W[c * 4 + 1] +
                                    Rt[r * 3 + 2] * W[c * 4 + 2];
            L[0] = lr[0]; L[4] = lr[1]; L[8] = lr[2];  L[12] = lt[0];
            L[1] = lr[3]; L[5] = lr[4]; L[9] = lr[5];  L[13] = lt[1];
            L[2] = lr[6]; L[6] = lr[7]; L[10] = lr[8]; L[14] = lt[2];
            L[3] = 0;     L[7] = 0;     L[11] = 0;     L[15] = 1.0f;
        } else {
            memcpy(L, W, 16 * sizeof(float));
        }
    }

    // animated: local rotation comes from the motion (bind rotation when absent),
    // local translation is replaced by the motion data
    for (size_t i = 0; i < n; ++i) {
        float L[16];
        memcpy(L, &bindLocal[i * 16], 16 * sizeof(float));
        if (i * 4 + 3 < localRot.size()) {
            const float* q = &localRot[i * 4];
            if (std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) &&
                std::isfinite(q[3])) {
                // copy ONLY the 3x3: QuatToMat writes all 16 floats and would zero
                // L[12..14]. With the translation channel off nothing puts the bind
                // local offset back, which collapsed every animated bone onto the
                // origin and balled the parts up.
                float R[16];
                QuatToMat(q, R);
                L[0] = R[0];  L[1] = R[1];  L[2] = R[2];
                L[4] = R[4];  L[5] = R[5];  L[6] = R[6];
                L[8] = R[8];  L[9] = R[9];  L[10] = R[10];
            }
        }
        if (i < localTrans.size()) {
            const DxgVec3& v = localTrans[i];
            if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) {
                L[12] = v.x;
                L[13] = v.y;
                L[14] = v.z;
            }
        }
        float* W = &worldOut[i * 16];
        const int p = bones[i].parent;
        if (p >= 0 && (size_t)p < n) {
            float tmp[16];
            Mul4(&worldOut[(size_t)p * 16], L, tmp);
            memcpy(W, tmp, sizeof(tmp));
        } else {
            memcpy(W, L, sizeof(L));
        }
    }
}

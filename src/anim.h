#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dxg.h"

// Minimal .mrb (rhyme motion) reader.
//
// Header (64 bytes): "MRB\0", +4 version, +8 ?, +12 ?, +48 = filesize-12,
//                    +52 = 499, +56 = 1, +60 = nameBytes
// +64            : zero separated bone names (only bones that are animated)
// data section   : u32 nKeys
//                  u32 4
//                  u32[nKeys]  key time in milliseconds (16.67ms steps => 60fps)
//                  u32[8]     block descriptor (NOT the channel pool counts)
//                  float[]     translation keys (vec3) - the col0 pool, at dataOff
//                  float[]     rotation keys (quat)    - the col1 pool, follows it
//                  u16[]      index table (nKeys * nBones * 3) at file tail
//
// Channel layout verified by reverse-engineering the on-disk data (not the loader):
// col0 = translation (vec3 pool right at dataOff), col1 = rotation (unit-quaternion
// pool that follows), col2 = scale (all-zero in these files). The index is bone-major.
struct MrbMotion {
    std::vector<std::string> boneNames;
    uint32_t nKeys = 0;
    uint32_t nBones = 0;          // animated bone count from the file
    std::vector<uint32_t> times;  // milliseconds

    // Key-frame compressed layout: three independent key channels (translation /
    // rotation / scale) plus one u16 index per (key, bone, channel). The index is
    // bone-major; per (bone, key) the triple is (translation, rotation, scale) at
    // columns 0, 1, 2.
    std::vector<float> posKeys;   // translation keys, count * 3 (vec3)
    std::vector<float> rotKeys;   // rotation keys,    count * 4 (quat x,y,z,w)
    std::vector<uint16_t> idx;    // nKeys * nBones * 3 -> (translation, rotation, scale)
    bool hasRot = false;

    std::vector<float> pos;       // legacy flat view (key * nBones + bone) * 3

    // The translation channel is the local offset of each bone (verified: its pool
    // sits at dataOff and the values are per-bone position offsets). Applying it by
    // REPLACING the bind local offset tears the mesh (the motion frame-0 translation
    // differs from the chunk-36 bind local offset, so the skeleton is double-placed
    // and limbs stretch 4..11x). The correct handling is additive (motion translation
    // += bind local translation), which is not yet wired up, so it defaults OFF and
    // the viewer can toggle it with T for comparison. Rotation (col1) is always applied
    // and drives the animation.
    bool useTrans = false;

    bool Parse(const uint8_t* data, size_t size);
    bool Valid() const { return nKeys > 0 && nBones > 0 && !posKeys.empty(); }
    // The time array can end with a small loop marker pair (1, 4) that is not a
    // real key time, so the clip length is the largest time, not the last one.
    uint32_t Duration() const {
        uint32_t m = 0;
        for (uint32_t t : times) {
            if (t > m) m = t;
        }
        return m;
    }
    void DumpDebug() const;

    // Interpolated local translation / rotation for every animated bone.
    // outT[b] is xyz, outR[b] is a xyzw quaternion (4 floats).
    bool Sample(float timeMs, std::vector<DxgVec3>& outT,
                std::vector<float>& outR) const;
};

// Build the animated world matrix for each bone.
// bones        : skeleton (names + inverse bind matrices + hierarchy)
// localTrans   : per-bone local translation for this frame (size = bones.size();
//                NaN entries fall back to the bind pose local offset)
// localRot     : per-bone local rotation as xyzw quaternions (4 per bone);
//                NaN entries fall back to the bind pose local rotation
void BuildBoneWorld(const std::vector<DxgBone>& bones,
                    const std::vector<DxgVec3>& localTrans,
                    const std::vector<float>& localRot,
                    std::vector<float>& worldOut);  // bones.size() * 16

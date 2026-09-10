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
//                  8 x u32     block descriptor
//                  float[]     per (key, bone): local translation xyz
//
// The translation layout was confirmed against the skeleton bind pose: decoding
// DXG chunk 36 matrices as INVERSE bind matrices and taking the local offset
// reproduces the first key of the motion (avg error 0.89 units).
struct MrbMotion {
    std::vector<std::string> boneNames;
    uint32_t nKeys = 0;
    uint32_t nBones = 0;          // animated bone count from the file
    std::vector<uint32_t> times;  // milliseconds

    // Key-frame compressed layout, confirmed against sub_B3FDB0 (rhyme motion
    // loader): the file stores three independent key channels (scale / rotation /
    // translation) plus one u16 index per (key, bone, channel).
    std::vector<float> posKeys;   // translation keys, count * 3
    std::vector<float> rotKeys;   // rotation keys,    count * 4 (quat x,y,z,w)
    std::vector<uint16_t> idx;    // nKeys * nBones * 3 -> (scale, trans, rot)
    bool hasRot = false;

    std::vector<float> pos;       // legacy flat view (key * nBones + bone) * 3

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

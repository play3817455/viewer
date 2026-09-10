#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DxgVec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct DxgVec2 {
    float u = 0;
    float v = 0;
};

struct DxgMesh {
    std::string name;
    uint32_t flags = 0;
    uint16_t nFaces = 0;
    uint16_t nVerts = 0;
    bool lod1 = false;
    std::vector<DxgVec3> pos;
    std::vector<DxgVec3> nrm;
    std::vector<DxgVec2> uv;
    std::vector<uint32_t> idx;

    // Skinning (confirmed in sub_B71290): every shape carries its own bone name
    // table (shape+4 = count, <= 26) and a u16 bone index per vertex that points
    // into it. The game does not use smooth weights - each vertex follows exactly
    // one bone.
    std::vector<std::string> boneNames;   // shape bone palette
    std::vector<uint16_t> vertBone;       // per vertex index into boneNames
};

// Skeleton entry.
// The game stores, per bone: a hierarchy record (idx,parent,child,sibling) and a
// 4x4 bind-pose matrix in model space (see DXG chunk type 36).
struct DxgBone {
    std::string name;
    int parent = -1;
    int child = -1;
    int sibling = -1;
    float m[16]{};  // bind pose, row-major (m[12..14] = translation)
    DxgVec3 pos{};  // translation part of m
};

struct DxgModel {
    std::vector<std::string> names;
    std::vector<DxgMesh> meshes;
    std::vector<DxgBone> bones;
    bool Parse(const uint8_t* data, size_t size);
};

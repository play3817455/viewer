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
};

struct DxgBone {
    std::string name;
    int parent = -1;
    DxgVec3 pos{};
};

struct DxgModel {
    std::vector<std::string> names;
    std::vector<DxgMesh> meshes;
    std::vector<DxgBone> bones;
    bool Parse(const uint8_t* data, size_t size);
};

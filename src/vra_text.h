#pragma once

#include <string>
#include <vector>

struct VraMeshBind {
    std::string name;
    int materialId = -1;
    bool noRender = false;
    bool hasLod0 = false;
};

struct VraMaterial {
    int id = 0;
    std::string texture;
    std::string mixTexture;
    std::string glossTexture;
    std::string blend;
    bool twoSided = false;
    bool alphaTest = false;
    float ambient[3] = {0.5f, 0.5f, 0.5f};
    float diffuse[3] = {1.0f, 1.0f, 1.0f};
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    float uvAddU = 0.0f;
    float uvAddV = 0.0f;
};

struct VraDoc {
    std::vector<VraMeshBind> meshes;
    std::vector<VraMaterial> materials;
    std::string geometry;
    bool Parse(const char* text, size_t len);
    bool HasMesh(const std::string& meshName) const;
    int MaterialOf(const std::string& meshName) const;
    bool IsNoRender(const std::string& meshName) const;
    const VraMaterial* GetMat(int id) const;
};

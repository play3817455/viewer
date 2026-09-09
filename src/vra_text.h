#pragma once

#include <string>
#include <vector>

struct VraMeshBind {
    std::string name;
    int materialId = 0;
};

struct VraMaterial {
    int id = 0;
    std::string texture;
    std::string blend;
    bool twoSided = false;
    bool alphaTest = false;
};

struct VraDoc {
    std::vector<VraMeshBind> meshes;
    std::vector<VraMaterial> materials;
    std::string geometry;
    bool Parse(const char* text, size_t len);
    int MaterialOf(const std::string& meshName) const;
    const VraMaterial* GetMat(int id) const;
};

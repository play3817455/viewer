#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SceneV3 { float x = 0, y = 0, z = 0; };
struct SceneV4 { float x = 0, y = 0, z = 0, w = 1; };

// A placement record from a field *_grd.vra / *_obj.vra file.
// The asset resolver must still use the category/name pair and verify every file.
struct SceneObject {
    std::string name;
    std::string category;
    int renderId = 0;
    SceneV3 position;
    SceneV4 rotation;
    SceneV3 scale{1, 1, 1};
    bool physics = false;
    bool shadow = false;
    bool castShadow = false;
    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;
};

struct SceneVraDoc {
    std::vector<SceneObject> objects;
    bool Parse(const char* text, size_t size);
};

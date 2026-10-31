#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct Preview3DTex {
    std::string key;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct Preview3DInput {
    const uint8_t* dxg = nullptr;
    size_t dxgSize = 0;
    const char* vraText = nullptr;
    size_t vraSize = 0;
    std::vector<Preview3DTex> textures;
};

bool Preview3D_Init(HWND hwnd);
void Preview3D_Shutdown();
void Preview3D_Clear();
bool Preview3D_Load(const Preview3DInput& in);
// Feed a .mrb motion; the skeleton (already loaded) is driven by it.
bool Preview3D_LoadMotion(const uint8_t* data, size_t size);
bool Preview3D_HasMotion();
unsigned Preview3D_MotionDuration();   // milliseconds, 0 without a motion
unsigned Preview3D_MotionBones();      // animated bone count, 0 without a motion
unsigned Preview3D_BoundParts();       // parts attached to a bone
unsigned Preview3D_PartCount();        // total parts
bool Preview3D_Active();
void Preview3D_Resize();
void Preview3D_Render();
void Preview3D_ToggleSkeleton();
unsigned Preview3D_MeshCount();
unsigned Preview3D_BoneCount();
unsigned Preview3D_TriCount();
unsigned Preview3D_TextureCount();
unsigned Preview3D_TexturedMeshCount();
unsigned Preview3D_MissingTextureCount();
unsigned Preview3D_MissingMaterialCount();
unsigned Preview3D_NoBaseTextureCount();
LRESULT Preview3D_WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

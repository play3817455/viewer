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

struct Preview3DTransform {
    float position[3] = {0, 0, 0};
    float rotation[4] = {0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
    bool attachToBone = false;
    std::string boneName;
};

bool Preview3D_Init(HWND hwnd);
void Preview3D_Shutdown();
void Preview3D_Clear();
bool Preview3D_Load(const Preview3DInput& in);
// Append a placed model without replacing the active machine. A non-empty
// attachToBone/boneName applies animation deltas from an explicitly chosen bone.
bool Preview3D_AppendStatic(const Preview3DInput& in, const Preview3DTransform& transform);
// Diagnostic quad for a manually selected PP Sprite image. This is a preview
// plane, not a reproduction of the client's particle animation or blend mode.
bool Preview3D_AppendSprite(const Preview3DTex& texture, float width, float height,
                           const Preview3DTransform& transform);
// Expand the zoom/far-clip range when the preview contains a map around origin.
void Preview3D_SetSceneRange(float radius);
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
const char* Preview3D_BoneName(unsigned index);
unsigned Preview3D_TriCount();
LRESULT Preview3D_WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

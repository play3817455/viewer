#include "preview3d.h"

#include "dxg.h"
#include "anim.h"
#include "image_io.h"
#include "vra_text.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "d3d9.lib")

namespace {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

constexpr UINT kVertexStride = 32;

struct Mat4 {
    float m[16]{};
};

struct DrawMesh {
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DTexture9* tex = nullptr;
    unsigned nidx = 0;
    unsigned nverts = 0;
    bool twoSided = false;
    bool alphaTest = false;
    bool additive = false;
    bool translucent = false;
    std::string name;
    bool weapon = false;
    int bone = -1;    // skeleton bone this part rides on (-1 = static)
    float cx = 0.0f;  // part centre, used to pick the bone
    float cy = 0.0f;
    float cz = 0.0f;

    // per vertex skinning (sub_B71290 layout: one bone per vertex)
    std::vector<DxgVec3> basePos;     // bind pose positions
    std::vector<DxgVec3> baseNrm;     // bind pose normals
    std::vector<std::string> shapeBones;  // shape bone palette
    std::vector<uint16_t> vertBone;   // per vertex palette slot
    std::vector<int> vbone;           // per vertex global bone index (-1 = static)
    bool skinned = false;
};

HWND g_hwnd = nullptr;
IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_dev = nullptr;
D3DPRESENT_PARAMETERS g_pp{};
bool g_active = false;
bool g_showSkel = false;
bool g_drag = false;
POINT g_last{};
float g_yaw = 0.35f;
float g_pitch = 0.15f;
float g_dist = 400.0f;
float g_cx = 0, g_cy = 80.0f, g_cz = 0;
float g_radius = 120.0f;
unsigned g_meshCount = 0, g_boneCount = 0, g_triCount = 0;
std::vector<DrawMesh> g_draws;
std::vector<IDirect3DTexture9*> g_texOwned;
IDirect3DVertexBuffer9* g_skelVb = nullptr;
unsigned g_skelVerts = 0;
unsigned g_skelBytes = 0;
DWORD g_fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

// --- interactive control state (WASD move / 1-4 weapon / LMB attack) ---
bool g_keys[256] = {};
int g_weaponMode = 0;          // 0 = both, 1 = right only, 2 = left only, 3 = none
float g_attack = 0.0f;         // attack animation time remaining (seconds)
const float kAttackDur = 0.55f;
float g_walkPhase = 0.0f;
bool g_useBones = true;        // B toggles: false = every part stays in model space
bool g_useSkin = true;         // M toggles per vertex skinning (geometric bone assign)
float g_moveX = 0.0f, g_moveZ = 0.0f;
float g_facing = 0.0f;
float g_bob = 0.0f;
DWORD g_lastTick = 0;
POINT g_down{};

// --- skeleton / motion ---
std::vector<DxgBone> g_bones;
std::vector<float> g_boneWorld;   // animated world matrices (16 per bone)
MrbMotion g_motion;
float g_animTime = 0.0f;          // milliseconds

void DecomposeInv(const float* m, float R[9], float t[3]) {
    // column-major: A[row][col] = m[col*4+row], A = R^T  =>  R[row][col] = m[row*4+col]
    R[0] = m[0]; R[1] = m[1]; R[2] = m[2];
    R[3] = m[4]; R[4] = m[5]; R[5] = m[6];
    R[6] = m[8]; R[7] = m[9]; R[8] = m[10];
    const float b[3] = {m[12], m[13], m[14]};
    t[0] = -(R[0] * b[0] + R[1] * b[1] + R[2] * b[2]);
    t[1] = -(R[3] * b[0] + R[4] * b[1] + R[5] * b[2]);
    t[2] = -(R[6] * b[0] + R[7] * b[1] + R[8] * b[2]);
}

void BindWorld(std::vector<float>& out) {
    const size_t n = g_bones.size();
    out.assign(n * 16, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float R[9], t[3];
        DecomposeInv(g_bones[i].m, R, t);
        float* W = &out[i * 16];
        W[0] = R[0]; W[4] = R[1]; W[8] = R[2];  W[12] = t[0];
        W[1] = R[3]; W[5] = R[4]; W[9] = R[5];  W[13] = t[1];
        W[2] = R[6]; W[6] = R[7]; W[10] = R[8]; W[14] = t[2];
        W[3] = 0;    W[7] = 0;    W[11] = 0;    W[15] = 1.0f;
    }
}

// Distance from a point to the bone segment (bone origin -> child origin); for a
// leaf bone it degrades to the distance to its origin. Used to decide which bone
// of a shape palette owns a vertex.
float BoneSegDist(const DxgVec3& p, const std::vector<float>& bw,
                  const std::vector<DxgBone>& bones, int b) {
    if (b < 0 || (size_t)b >= bones.size() || bw.size() < ((size_t)b + 1) * 16)
        return 1.0e18f;
    const float* a = &bw[(size_t)b * 16];
    const float ax = a[12], ay = a[13], az = a[14];
    float best = sqrtf((p.x - ax) * (p.x - ax) + (p.y - ay) * (p.y - ay) +
                       (p.z - az) * (p.z - az));
    for (size_t c = 0; c < bones.size(); ++c) {
        if (bones[c].parent != b) continue;
        const float* q = &bw[c * 16];
        const float dx = q[12] - ax, dy = q[13] - ay, dz = q[14] - az;
        const float L2 = dx * dx + dy * dy + dz * dz;
        float t = 0.0f;
        if (L2 > 1.0e-9f) {
            t = ((p.x - ax) * dx + (p.y - ay) * dy + (p.z - az) * dz) / L2;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
        }
        const float cx = ax + t * dx, cy = ay + t * dy, cz = az + t * dz;
        const float d = sqrtf((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) +
                              (p.z - cz) * (p.z - cz));
        if (d < best) best = d;
    }
    return best;
}

// Part -> bone binding.
// Strict name correspondence only (no geometry guessing):
//   part "Hand_01"  -> head "Hand"
//   bone "T_L_Hand" -> tail "Hand"
// Only when they are equal is the part bound to the bone. A part with no
// matching bone (body shells, decals, ...) stays in model space.
int FindBoneForPart(const std::string& part, float cx, float cy, float cz) {
    if (g_bones.empty()) return -1;
    auto lower = [](const std::string& s) {
        std::string o;
        for (char c : s) o.push_back((char)(c >= 'A' && c <= 'Z' ? c + 32 : c));
        return o;
    };
    auto head = [&](const std::string& s) {
        const auto p = s.find('_');
        return lower(p == std::string::npos ? s : s.substr(0, p));
    };
    auto tail = [&](const std::string& s) {
        const auto p = s.rfind('_');
        return lower(p == std::string::npos ? s : s.substr(p + 1));
    };
    std::string key = head(part);
    if (key == "saber" || key == "gun" || key == "rifle" || key == "shield") key = "hand";
    if (key.empty()) return -1;

    std::vector<int> hits;
    for (size_t i = 0; i < g_bones.size(); ++i) {
        if (tail(g_bones[i].name) == key) hits.push_back((int)i);
    }
    if (hits.empty()) return -1;
    std::vector<float> bw;
    BindWorld(bw);
    int best = hits[0];
    if (hits.size() > 1) {
        // left/right: take the side matching the part's X
        float bestD = 1.0e9f;
        for (int h : hits) {
            const float d = fabsf(bw[(size_t)h * 16 + 12] - cx);
            if (d < bestD) {
                bestD = d;
                best = h;
            }
        }
    }
    // safety: the part must actually sit at that bone in bind pose, otherwise the
    // name match is a coincidence - keep it in model space instead of dragging it
    const float* W = &bw[(size_t)best * 16];
    const float dx = W[12] - cx, dy = W[13] - cy, dz = W[14] - cz;
    const float lim = (g_radius > 1.0f ? g_radius : 100.0f) * 0.75f;
    if (dx * dx + dy * dy + dz * dz > lim * lim) return -1;
    return best;
}

// Rebuild the skeleton line list from the current world matrices.
void UpdateSkeletonVb() {
    if (!g_dev || g_bones.empty()) return;
    std::vector<float> lines;
    for (size_t i = 0; i < g_bones.size(); ++i) {
        const int p = g_bones[i].parent;
        if (p < 0 || (size_t)p >= g_bones.size()) continue;
        if ((size_t)i * 16 + 14 >= g_boneWorld.size()) break;
        const float* a = &g_boneWorld[(size_t)p * 16];
        const float* b = &g_boneWorld[i * 16];
        lines.push_back(a[12]); lines.push_back(a[13]); lines.push_back(a[14]);
        lines.push_back(b[12]); lines.push_back(b[13]); lines.push_back(b[14]);
    }
    if (lines.empty()) return;
    const UINT bytes = (UINT)(lines.size() * sizeof(float));
    if (!g_skelVb || bytes > g_skelBytes) {
        if (g_skelVb) g_skelVb->Release();
        g_skelVb = nullptr;
        g_skelBytes = bytes;
        if (FAILED(g_dev->CreateVertexBuffer(bytes, D3DUSAGE_DYNAMIC, D3DFVF_XYZ,
                                            D3DPOOL_DEFAULT, &g_skelVb, nullptr))) {
            g_skelVb = nullptr;
            return;
        }
    }
    void* p = nullptr;
    if (SUCCEEDED(g_skelVb->Lock(0, bytes, &p, D3DLOCK_DISCARD))) {
        memcpy(p, lines.data(), bytes);
        g_skelVb->Unlock();
    }
    g_skelVerts = (unsigned)(lines.size() / 3);
}

// Per vertex skinning: rebuild the vertex buffers from the animated bone
// matrices. A vertex follows exactly one bone (that is what sub_B71290 stores),
// so v' = (boneWorld[bone] * inverseBind[bone]) * v. DXG chunk 36 stores the
// inverse bind matrices, so no separate bind data is needed.
void UpdateSkinnedMeshes() {
    if (g_bones.empty()) return;
    for (auto& d : g_draws) {
        if (!d.skinned || !d.vb || d.basePos.empty()) continue;
        if (d.vbone.size() != d.basePos.size()) continue;
        void* vp = nullptr;
        if (FAILED(d.vb->Lock(0, 0, &vp, 0))) continue;
        auto* base = (uint8_t*)vp;
        for (size_t i = 0; i < d.basePos.size(); ++i) {
            const DxgVec3 p = d.basePos[i];
            DxgVec3 q = p;
            DxgVec3 n = (i < d.baseNrm.size()) ? d.baseNrm[i] : DxgVec3{0.0f, 1.0f, 0.0f};
            const int b = g_useSkin ? d.vbone[i] : -1;
            if (b >= 0 && (size_t)b < g_bones.size() &&
                g_boneWorld.size() >= ((size_t)b + 1) * 16) {
                const float* aw = &g_boneWorld[(size_t)b * 16];
                const float* ib = g_bones[(size_t)b].m;
                float M[16];
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        M[c * 4 + r] = aw[0 * 4 + r] * ib[c * 4 + 0] +
                                       aw[1 * 4 + r] * ib[c * 4 + 1] +
                                       aw[2 * 4 + r] * ib[c * 4 + 2] +
                                       aw[3 * 4 + r] * ib[c * 4 + 3];
                    }
                }
                q.x = M[0] * p.x + M[4] * p.y + M[8] * p.z + M[12];
                q.y = M[1] * p.x + M[5] * p.y + M[9] * p.z + M[13];
                q.z = M[2] * p.x + M[6] * p.y + M[10] * p.z + M[14];
                const float nx0 = n.x, ny0 = n.y, nz0 = n.z;
                n.x = M[0] * nx0 + M[4] * ny0 + M[8] * nz0;
                n.y = M[1] * nx0 + M[5] * ny0 + M[9] * nz0;
                n.z = M[2] * nx0 + M[6] * ny0 + M[10] * nz0;
                const float L = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                if (L > 1.0e-6f) {
                    n.x /= L;
                    n.y /= L;
                    n.z /= L;
                } else {
                    n = DxgVec3{0.0f, 1.0f, 0.0f};
                }
                // safety: a skinned vertex must stay near where it was bound. If a
                // bad bone index or matrix sends it far away, keep the bind vertex
                // so the mesh cannot explode into flying triangles.
                const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                const float lim = (g_radius > 1.0f ? g_radius : 100.0f) * 2.0f;
                if (dx * dx + dy * dy + dz * dz > lim * lim) {
                    q = p;
                    n = (i < d.baseNrm.size()) ? d.baseNrm[i] : DxgVec3{0.0f, 1.0f, 0.0f};
                }
            }
            auto* v = (Vertex*)(base + i * kVertexStride);
            v->x = q.x;
            v->y = q.y;
            v->z = q.z;
            v->nx = n.x;
            v->ny = n.y;
            v->nz = n.z;
        }
        d.vb->Unlock();
    }
}

void ReleaseDraws() {
    for (auto& d : g_draws) {
        if (d.vb) d.vb->Release();
        if (d.ib) d.ib->Release();
        d.vb = nullptr;
        d.ib = nullptr;
        d.tex = nullptr;
    }
    g_draws.clear();
    for (auto* t : g_texOwned)
        if (t) t->Release();
    g_texOwned.clear();
    if (g_skelVb) {
        g_skelVb->Release();
        g_skelVb = nullptr;
    }
    g_skelVerts = 0;
    g_active = false;
    g_meshCount = g_boneCount = g_triCount = 0;
}

void Ident(Mat4& o) {
    memset(o.m, 0, sizeof(o.m));
    o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
}

void Mul(const Mat4& a, const Mat4& b, Mat4& o) {
    Mat4 t{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t.m[c * 4 + r] = a.m[0 * 4 + r] * b.m[c * 4 + 0] + a.m[1 * 4 + r] * b.m[c * 4 + 1] +
                             a.m[2 * 4 + r] * b.m[c * 4 + 2] + a.m[3 * 4 + r] * b.m[c * 4 + 3];
    o = t;
}

void Perspective(float fov, float aspect, float zn, float zf, Mat4& o) {
    Ident(o);
    const float y = 1.0f / tanf(fov * 0.5f);
    const float x = y / aspect;
    o.m[0] = x;
    o.m[5] = y;
    o.m[10] = zf / (zf - zn);
    o.m[11] = 1.0f;
    o.m[14] = -zn * zf / (zf - zn);
    o.m[15] = 0.0f;
}

void RotX(float a, Mat4& o) {
    Ident(o);
    const float c = cosf(a), s = sinf(a);
    o.m[5] = c;
    o.m[6] = s;
    o.m[9] = -s;
    o.m[10] = c;
}

void RotY(float a, Mat4& o) {
    Ident(o);
    const float c = cosf(a), s = sinf(a);
    o.m[0] = c;
    o.m[2] = -s;
    o.m[8] = s;
    o.m[10] = c;
}

void Translate(float x, float y, float z, Mat4& o) {
    Ident(o);
    o.m[12] = x;
    o.m[13] = y;
    o.m[14] = z;
}

// Update movement / attack timers. Returns delta time in seconds.
float TickControl() {
    const DWORD now = GetTickCount();
    float dt = 0.016f;
    if (g_lastTick) dt = (float)(now - g_lastTick) / 1000.0f;
    g_lastTick = now;
    if (dt > 0.1f) dt = 0.1f;

    const float speed = g_radius * 1.6f;
    float fwd = 0.0f, side = 0.0f;
    if (g_keys['W']) fwd += 1.0f;
    if (g_keys['S']) fwd -= 1.0f;
    if (g_keys['A']) side -= 1.0f;
    if (g_keys['D']) side += 1.0f;
    if (fwd || side) {
        const float len = sqrtf(fwd * fwd + side * side);
        fwd /= len;
        side /= len;
        const float cf = cosf(g_facing), sf = sinf(g_facing);
        g_moveX += (sf * fwd + cf * side) * speed * dt;
        g_moveZ += (cf * fwd - sf * side) * speed * dt;
        g_walkPhase += dt * 6.0f;
        g_bob = sinf(g_walkPhase) * g_radius * 0.015f;
    } else {
        g_walkPhase = 0.0f;
        g_bob *= 0.85f;
    }
    if (g_keys['Q']) g_facing -= 2.0f * dt;
    if (g_keys['E']) g_facing += 2.0f * dt;
    if (g_attack > 0.0f) {
        g_attack -= dt;
        if (g_attack < 0.0f) g_attack = 0.0f;
    }
    return dt;
}

// True when the given part should be drawn with the current weapon mode.
bool WeaponVisible(const DrawMesh& d) {
    if (!d.weapon) return true;
    std::string low;
    for (char c : d.name) low.push_back((char)(c >= 'A' && c <= 'Z' ? c + 32 : c));
    const bool left = low.find("_02") != std::string::npos ||
                      low.find("_04") != std::string::npos;
    switch (g_weaponMode) {
    case 0:
        return true;
    case 1:
        return !left;
    case 2:
        return left;
    default:
        return false;
    }
}

void LookAt(float ex, float ey, float ez, float cx, float cy, float cz, Mat4& o) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float L = sqrtf(fx * fx + fy * fy + fz * fz);
    if (L < 1.0e-8f) L = 1;
    fx /= L;
    fy /= L;
    fz /= L;
    float ux = 0, uy = 1, uz = 0;
    float sx = uy * fz - uz * fy;
    float sy = uz * fx - ux * fz;
    float sz = ux * fy - uy * fx;
    L = sqrtf(sx * sx + sy * sy + sz * sz);
    if (L < 1.0e-8f) L = 1;
    sx /= L;
    sy /= L;
    sz /= L;
    ux = fy * sz - fz * sy;
    uy = fz * sx - fx * sz;
    uz = fx * sy - fy * sx;
    Ident(o);
    o.m[0] = sx;
    o.m[4] = sy;
    o.m[8] = sz;
    o.m[1] = ux;
    o.m[5] = uy;
    o.m[9] = uz;
    o.m[2] = fx;
    o.m[6] = fy;
    o.m[10] = fz;
    o.m[12] = -(sx * ex + sy * ey + sz * ez);
    o.m[13] = -(ux * ex + uy * ey + uz * ez);
    o.m[14] = -(fx * ex + fy * ey + fz * ez);
}

std::string Lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}

std::string FileName(std::string p) {
    for (auto& c : p)
        if (c == '\\') c = '/';
    const auto sl = p.find_last_of('/');
    return sl == std::string::npos ? p : p.substr(sl + 1);
}

IDirect3DTexture9* CreateTexRgba(const ImageRgba& img) {
    if (!g_dev || img.w <= 0 || img.h <= 0 || img.rgba.empty()) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(g_dev->CreateTexture((UINT)img.w, (UINT)img.h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                    &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    for (int y = 0; y < img.h; ++y) {
        auto* dst = (uint8_t*)lr.pBits + y * lr.Pitch;
        const uint8_t* src = img.rgba.data() + (size_t)y * (size_t)img.w * 4u;
        for (int x = 0; x < img.w; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    tex->UnlockRect(0);
    return tex;
}

IDirect3DTexture9* CreateTexDds(const uint8_t* data, size_t size) {
    if (!g_dev || !data || size < 128 || memcmp(data, "DDS ", 4) != 0) return nullptr;
    uint32_t h = 0, w = 0, flags = 0, pitch = 0, mips = 0, four = 0;
    memcpy(&h, data + 12, 4);
    memcpy(&w, data + 16, 4);
    memcpy(&pitch, data + 20, 4);
    memcpy(&mips, data + 28, 4);
    memcpy(&flags, data + 80, 4);
    memcpy(&four, data + 84, 4);
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    unsigned block = 0;
    if (four == 0x31545844) {
        fmt = D3DFMT_DXT1;
        block = 8;
    } else if (four == 0x33545844) {
        fmt = D3DFMT_DXT3;
        block = 16;
    } else if (four == 0x35545844) {
        fmt = D3DFMT_DXT5;
        block = 16;
    }
    if (fmt == D3DFMT_UNKNOWN || w == 0 || h == 0) return nullptr;
    const size_t header = 128;
    const unsigned bw = (std::max)(1u, (w + 3) / 4);
    const unsigned bh = (std::max)(1u, (h + 3) / 4);
    const size_t bytes = (size_t)bw * bh * block;
    if (header + bytes > size) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(g_dev->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED, &tex, nullptr))) return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    const uint8_t* src = data + header;
    auto* dst = (uint8_t*)lr.pBits;
    const size_t row = (size_t)bw * block;
    for (unsigned y = 0; y < bh; ++y) {
        memcpy(dst + (size_t)y * (size_t)lr.Pitch, src + (size_t)y * row, row);
    }
    tex->UnlockRect(0);
    (void)flags;
    (void)pitch;
    (void)mips;
    return tex;
}

IDirect3DTexture9* LoadTex(const Preview3DTex& src) {
    if (!src.data || !src.size) return nullptr;
    if (auto* t = CreateTexDds(src.data, src.size)) return t;
    ImageRgba img;
    std::wstring name(src.key.begin(), src.key.end());
    if (DecodeImage(src.data, src.size, name, img)) return CreateTexRgba(img);
    return nullptr;
}

bool ResetDevice() {
    if (!g_dev || !g_hwnd) return false;
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    g_pp.BackBufferWidth = (UINT)(std::max)(1, (int)rc.right);
    g_pp.BackBufferHeight = (UINT)(std::max)(1, (int)rc.bottom);
    g_pp.BackBufferCount = 1;
    const HRESULT hr = g_dev->Reset(&g_pp);
    return SUCCEEDED(hr);
}

void SetupStates() {
    if (!g_dev) return;
    g_dev->SetFVF(g_fvf);
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    g_dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(70, 70, 78));
    g_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    D3DLIGHT9 L{};
    L.Type = D3DLIGHT_DIRECTIONAL;
    L.Diffuse.r = L.Diffuse.g = L.Diffuse.b = 0.95f;
    L.Diffuse.a = 1;
    L.Ambient.r = L.Ambient.g = L.Ambient.b = 0.25f;
    L.Direction.x = -0.35f;
    L.Direction.y = -0.65f;
    L.Direction.z = 0.65f;
    g_dev->SetLight(0, &L);
    g_dev->LightEnable(0, TRUE);
    D3DMATERIAL9 mat{};
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1;
    mat.Ambient = mat.Diffuse;
    g_dev->SetMaterial(&mat);
}

bool CreateDevice() {
    if (g_dev) return true;
    if (!g_hwnd) return false;
    if (!g_d3d) g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return false;
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    memset(&g_pp, 0, sizeof(g_pp));
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_pp.BackBufferWidth = (UINT)(std::max)(1, (int)rc.right);
    g_pp.BackBufferHeight = (UINT)(std::max)(1, (int)rc.bottom);
    g_pp.EnableAutoDepthStencil = TRUE;
    g_pp.AutoDepthStencilFormat = D3DFMT_D16;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    g_pp.hDeviceWindow = g_hwnd;
    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_pp, &g_dev);
    if (FAILED(hr))
        hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_pp, &g_dev);
    return SUCCEEDED(hr) && g_dev;
}

bool UploadMesh(const DxgMesh& src, IDirect3DTexture9* tex, const VraMaterial* mat) {
    if (!g_dev || src.pos.empty() || src.idx.size() < 9) return false;
    if (src.name.find("decal") != std::string::npos) return false;
    DrawMesh d;
    d.nidx = (unsigned)src.idx.size();
    d.nverts = (unsigned)src.pos.size();
    d.tex = tex;
    d.name = src.name;
    {
        float mn[3] = {1.0e9f, 1.0e9f, 1.0e9f};
        float mx[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
        for (const auto& p : src.pos) {
            for (int k = 0; k < 3; ++k) {
                const float v = k == 0 ? p.x : (k == 1 ? p.y : p.z);
                if (v < mn[k]) mn[k] = v;
                if (v > mx[k]) mx[k] = v;
            }
        }
        d.cx = (mn[0] + mx[0]) * 0.5f;
        d.cy = (mn[1] + mx[1]) * 0.5f;
        d.cz = (mn[2] + mx[2]) * 0.5f;
    }
    {
        std::string low;
        for (char c : src.name) low.push_back((char)(c >= 'A' && c <= 'Z' ? c + 32 : c));
        d.weapon = low.find("saber") != std::string::npos ||
                   low.find("gun") != std::string::npos ||
                   low.find("rifle") != std::string::npos ||
                   low.find("baz") != std::string::npos ||
                   low.find("shield") != std::string::npos;
    }
    if (mat) {
        d.twoSided = mat->twoSided;
        d.alphaTest = mat->alphaTest || mat->blend == "translucent";
        d.additive = mat->blend == "add";
        d.translucent = mat->blend == "translucent";
    }
    // keep a bind pose copy so the vertices can be re-skinned every frame
    d.basePos = src.pos;
    d.baseNrm = src.nrm;
    d.shapeBones = src.boneNames;
    d.vertBone = src.vertBone;

    const UINT vbytes = (UINT)(src.pos.size() * kVertexStride);
    if (FAILED(g_dev->CreateVertexBuffer(vbytes, 0, g_fvf, D3DPOOL_MANAGED, &d.vb, nullptr)))
        return false;
    void* vp = nullptr;
    if (FAILED(d.vb->Lock(0, vbytes, &vp, 0))) {
        d.vb->Release();
        return false;
    }
    auto* base = (uint8_t*)vp;
    for (size_t i = 0; i < src.pos.size(); ++i) {
        auto* v = (Vertex*)(base + i * kVertexStride);
        v->x = src.pos[i].x;
        v->y = src.pos[i].y;
        v->z = src.pos[i].z;
        if (i < src.nrm.size()) {
            v->nx = src.nrm[i].x;
            v->ny = src.nrm[i].y;
            v->nz = src.nrm[i].z;
        } else {
            v->nx = 0;
            v->ny = 1;
            v->nz = 0;
        }
        if (i < src.uv.size()) {
            v->u = src.uv[i].u;
            v->v = src.uv[i].v;
        } else {
            v->u = 0;
            v->v = 0;
        }
        memset((uint8_t*)v + sizeof(Vertex), 0, kVertexStride - sizeof(Vertex));
    }
    d.vb->Unlock();
    const UINT ibytes = (UINT)(src.idx.size() * sizeof(uint16_t));
    if (FAILED(g_dev->CreateIndexBuffer(ibytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &d.ib, nullptr))) {
        d.vb->Release();
        return false;
    }
    void* ip = nullptr;
    if (FAILED(d.ib->Lock(0, ibytes, &ip, 0))) {
        d.vb->Release();
        d.ib->Release();
        return false;
    }
    auto* dstIdx = (uint16_t*)ip;
    for (size_t i = 0; i < src.idx.size(); ++i)
        dstIdx[i] = (uint16_t)src.idx[i];
    d.ib->Unlock();
    g_triCount += d.nidx / 3;
    g_draws.push_back(d);
    return true;
}

void FitCamera(const DxgModel& model) {
    float mn[3] = {1.0e9f, 1.0e9f, 1.0e9f};
    float mx[3] = {-1.0e9f, -1.0e9f, -1.0e9f};
    bool any = false;
    for (const auto& m : model.meshes) {
        if (m.lod1) continue;
        for (const auto& p : m.pos) {
            if (p.x < mn[0]) mn[0] = p.x;
            if (p.y < mn[1]) mn[1] = p.y;
            if (p.z < mn[2]) mn[2] = p.z;
            if (p.x > mx[0]) mx[0] = p.x;
            if (p.y > mx[1]) mx[1] = p.y;
            if (p.z > mx[2]) mx[2] = p.z;
            any = true;
        }
    }
    if (!any) return;
    g_cx = (mn[0] + mx[0]) * 0.5f;
    g_cy = (mn[1] + mx[1]) * 0.5f;
    g_cz = (mn[2] + mx[2]) * 0.5f;
    const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    g_radius = sqrtf(dx * dx + dy * dy + dz * dz) * 0.5f;
    if (g_radius < 8.0f) g_radius = 8.0f;
    const float tall = (std::max)(dy, (std::max)(dx, dz));
    g_dist = (std::max)(g_radius * 2.05f, tall * 1.35f);
    g_yaw = 0.70f;
    g_pitch = 0.12f;
}

}  // namespace

bool Preview3D_Init(HWND hwnd) {
    g_hwnd = hwnd;
    if (!CreateDevice()) return false;
    SetTimer(hwnd, 1, 16, nullptr);
    return true;
}

void Preview3D_Shutdown() {
    ReleaseDraws();
    if (g_dev) {
        g_dev->Release();
        g_dev = nullptr;
    }
    if (g_d3d) {
        g_d3d->Release();
        g_d3d = nullptr;
    }
    g_hwnd = nullptr;
}

void Preview3D_Clear() { ReleaseDraws(); }

bool Preview3D_Load(const Preview3DInput& in) {
    ReleaseDraws();
    if (!CreateDevice()) return false;
    if (!in.dxg || !in.dxgSize) return false;
    DxgModel model;
    if (!model.Parse(in.dxg, in.dxgSize)) return false;

    VraDoc vra;
    if (in.vraText && in.vraSize) vra.Parse(in.vraText, in.vraSize);

    std::map<std::string, IDirect3DTexture9*> byKey;
    IDirect3DTexture9* fallback = nullptr;
    for (const auto& t : in.textures) {
        auto* tex = LoadTex(t);
        if (!tex) continue;
        g_texOwned.push_back(tex);
        byKey[Lower(t.key)] = tex;
        byKey[Lower(FileName(t.key))] = tex;
        if (!fallback) fallback = tex;
    }

    // The decoder now produces real indexed geometry (LOD0 of every part), so all
    // parts can be drawn directly - their vertex data is already in model space.
    DxgModel draw = model;
    draw.meshes.clear();
    for (const auto& mesh : model.meshes) {
        if (mesh.lod1) continue;
        if (mesh.pos.empty() || mesh.idx.empty()) continue;
        draw.meshes.push_back(mesh);
    }
    if (draw.meshes.empty()) draw.meshes = model.meshes;

    FitCamera(draw);
    g_meshCount = 0;
    g_triCount = 0;
    for (const auto& mesh : draw.meshes) {
        const int mid = vra.MaterialOf(mesh.name);
        const VraMaterial* mat = vra.GetMat(mid);
        IDirect3DTexture9* tex = fallback;
        if (mat && !mat->texture.empty()) {
            auto it = byKey.find(Lower(mat->texture));
            if (it == byKey.end()) it = byKey.find(Lower(FileName(mat->texture)));
            if (it != byKey.end()) tex = it->second;
        }
        if (UploadMesh(mesh, tex, mat)) ++g_meshCount;
    }
    g_bones = model.bones;
    g_boneCount = (unsigned)g_bones.size();
    for (auto& d : g_draws) {
        d.vbone.assign(d.basePos.size(), -1);
        bool any = false;
        // Resolve the shape palette against the skeleton by name, exactly like the
        // engine does in sub_B72E80 (up to 26 names, each looked up in the skeleton
        // with an exact string compare).
        std::vector<int> pal;   // palette slot -> skeleton bone index (-1 = absent)
        if (!d.shapeBones.empty()) {
            pal.assign(d.shapeBones.size(), -1);
            for (size_t s = 0; s < d.shapeBones.size(); ++s) {
                for (size_t b = 0; b < g_bones.size(); ++b) {
                    if (g_bones[b].name == d.shapeBones[s]) {
                        pal[s] = (int)b;
                        break;
                    }
                }
            }
        }
        // 1) preferred: the file's own per vertex binding (4 bytes per vertex, the
        //    first byte is the palette slot; see sub_B7ACD0 / sub_B71290).
        if (d.vertBone.size() == d.basePos.size() && !pal.empty()) {
            for (size_t i = 0; i < d.basePos.size(); ++i) {
                const size_t slot = d.vertBone[i];
                if (slot < pal.size() && pal[slot] >= 0) {
                    d.vbone[i] = pal[slot];
                    any = true;
                }
            }
        }
        // 2) fallback (no usable file data): nearest bone segment inside the palette
        if (!any && !pal.empty() && !g_bones.empty() && d.basePos.size() > 4) {
            std::vector<int> ok;
            for (int b : pal) {
                if (b >= 0) ok.push_back(b);
            }
            std::vector<float> bw;
            BindWorld(bw);
            if (!ok.empty() && bw.size() >= g_bones.size() * 16) {
                for (size_t i = 0; i < d.basePos.size(); ++i) {
                    const DxgVec3& p = d.basePos[i];
                    float bestD = 1.0e18f;
                    int bestB = -1;
                    for (int b : ok) {
                        const float dd = BoneSegDist(p, bw, g_bones, b);
                        if (dd < bestD) {
                            bestD = dd;
                            bestB = b;
                        }
                    }
                    d.vbone[i] = bestB;
                    if (bestB >= 0) any = true;
                }
            }
        }
        d.skinned = any;
        // the rigid binding stays available: it is used with skinning off
        d.bone = FindBoneForPart(d.name, d.cx, d.cy, d.cz);
    }
    UpdateSkeletonVb();
    g_active = g_meshCount > 0;
    return g_active;
}

bool Preview3D_LoadMotion(const uint8_t* data, size_t size) {
    g_animTime = 0.0f;
    return g_motion.Parse(data, size);
}

bool Preview3D_HasMotion() { return g_motion.Valid(); }
unsigned Preview3D_MotionDuration() {
    return g_motion.Valid() ? g_motion.Duration() : 0u;
}
unsigned Preview3D_MotionBones() {
    return g_motion.Valid() ? g_motion.nBones : 0u;
}
// How many parts could be attached to a bone (name match + bind pose distance).
unsigned Preview3D_BoundParts() {
    unsigned n = 0;
    for (const auto& d : g_draws) {
        if (d.bone >= 0) ++n;
    }
    return n;
}
unsigned Preview3D_PartCount() { return (unsigned)g_draws.size(); }

bool Preview3D_Active() { return g_active && g_dev; }
unsigned Preview3D_MeshCount() { return g_meshCount; }
unsigned Preview3D_BoneCount() { return g_boneCount; }
unsigned Preview3D_TriCount() { return g_triCount; }

void Preview3D_Resize() {
    if (!g_dev) return;
    ResetDevice();
}

void Preview3D_ToggleSkeleton() { g_showSkel = !g_showSkel; }

void Preview3D_Render() {
    if (!g_dev || !g_hwnd) return;
    RECT rc{};
    GetClientRect(g_hwnd, &rc);
    const int cw = (std::max)(1, (int)rc.right);
    const int ch = (std::max)(1, (int)rc.bottom);
    if (g_pp.BackBufferWidth != (UINT)cw || g_pp.BackBufferHeight != (UINT)ch) ResetDevice();

    HRESULT coop = g_dev->TestCooperativeLevel();
    if (coop == D3DERR_DEVICELOST) return;
    if (coop == D3DERR_DEVICENOTRESET) {
        if (!ResetDevice()) return;
    }

    SetupStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(18, 20, 26), 1.0f, 0);
    if (FAILED(g_dev->BeginScene())) return;
    D3DVIEWPORT9 vp{};
    vp.X = 0;
    vp.Y = 0;
    vp.Width = (DWORD)cw;
    vp.Height = (DWORD)ch;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_dev->SetViewport(&vp);

    if (g_pitch < -1.2f) g_pitch = -1.2f;
    if (g_pitch > 1.2f) g_pitch = 1.2f;
    const float cp = cosf(g_pitch), sp = sinf(g_pitch);
    const float cy = cosf(g_yaw), sy = sinf(g_yaw);
    const float ex = g_cx + sy * cp * g_dist;
    const float ey = g_cy + sp * g_dist;
    const float ez = g_cz + cy * cp * g_dist;

    Mat4 view, proj;
    LookAt(ex, ey, ez, g_cx, g_cy, g_cz, view);
    Perspective(3.14159265f / 4.5f, (float)cw / (float)((std::max)(64, ch)), (std::max)(1.0f, g_radius * 0.02f),
                g_dist + g_radius * 8.0f, proj);
    g_dev->SetTransform(D3DTS_VIEW, (D3DMATRIX*)view.m);
    g_dev->SetTransform(D3DTS_PROJECTION, (D3DMATRIX*)proj.m);
    const float dt = TickControl();

    // drive the skeleton from the motion (bones not in the motion keep bind pose)
    if (!g_bones.empty()) {
        const float kNan = std::numeric_limits<float>::quiet_NaN();
        std::vector<DxgVec3> local(g_bones.size(), DxgVec3{kNan, kNan, kNan});
        std::vector<float> localRot(g_bones.size() * 4, kNan);
        if (g_useBones && g_motion.Valid()) {
            g_animTime += dt * 1000.0f;
            std::vector<DxgVec3> mv;
            std::vector<float> mr;
            if (g_motion.Sample(g_animTime, mv, mr)) {
                for (size_t b = 0; b < g_motion.boneNames.size() && b < mv.size(); ++b) {
                    for (size_t i = 0; i < g_bones.size(); ++i) {
                        if (g_bones[i].name == g_motion.boneNames[b]) {
                            local[i] = mv[b];
                            if (b * 4 + 3 < mr.size()) {
                                for (int k = 0; k < 4; ++k)
                                    localRot[i * 4 + k] = mr[b * 4 + k];
                            }
                            break;
                        }
                    }
                }
            }
        }
        BuildBoneWorld(g_bones, local, localRot, g_boneWorld);
        UpdateSkeletonVb();
        UpdateSkinnedMeshes();
    }

    Mat4 world, spin, move;
    RotY(g_facing, spin);
    Translate(g_moveX, g_bob, g_moveZ, move);
    Mul(move, spin, world);
    g_dev->SetTransform(D3DTS_WORLD, (D3DMATRIX*)world.m);

    // shoulder height used as pivot for the weapon swing
    const float shoulderY = g_cy + g_radius * 0.55f;

    auto drawOne = [&](const DrawMesh& d, bool transparent) {
        if (!d.vb || !d.ib || d.nidx < 3) return;
        if (!WeaponVisible(d)) return;
        bool customWorld = false;
        Mat4 base = world;
        // part rides on a bone: world * animWorld * inverseBind
        // with per vertex skinning on, the vertices are already in model space
        const bool skinActive = d.skinned && g_useSkin;
        if (g_useBones && !skinActive && d.bone >= 0 && (size_t)d.bone < g_bones.size() &&
            g_boneWorld.size() >= ((size_t)d.bone + 1) * 16) {
            Mat4 aw, ib, tmp;
            memcpy(aw.m, &g_boneWorld[(size_t)d.bone * 16], 64);
            memcpy(ib.m, g_bones[(size_t)d.bone].m, 64);
            Mul(aw, ib, tmp);
            // at bind pose tmp is the identity; if the bone world matrix is wrong
            // for this file the translation explodes - ignore the binding then
            const float tx = tmp.m[12], ty = tmp.m[13], tz = tmp.m[14];
            const float lim = (g_radius > 1.0f ? g_radius : 100.0f) * 0.75f;
            if (tx * tx + ty * ty + tz * tz <= lim * lim) {
                Mul(world, tmp, base);
                customWorld = true;
            }
        }
        Mat4 dw = base;
        if (d.weapon && g_attack > 0.0f) {
            const float t = 1.0f - g_attack / kAttackDur;       // 0..1
            const float ang = 1.15f - 2.6f * t;                 // swing down
            Mat4 rx, up, down, tmp1, tmp2;
            RotX(ang, rx);
            Translate(0.0f, -shoulderY, 0.0f, down);
            Translate(0.0f, shoulderY, 0.0f, up);
            Mul(down, rx, tmp1);
            Mul(tmp1, up, tmp2);
            Mul(base, tmp2, dw);
            customWorld = true;
        }
        if (customWorld) g_dev->SetTransform(D3DTS_WORLD, (D3DMATRIX*)dw.m);
        const bool isT = d.translucent || d.additive;
        if (isT != transparent) return;
        g_dev->SetStreamSource(0, d.vb, 0, kVertexStride);
        g_dev->SetIndices(d.ib);
        g_dev->SetTexture(0, d.tex);
        g_dev->SetRenderState(D3DRS_CULLMODE, d.twoSided ? D3DCULL_NONE : D3DCULL_CCW);
        if (d.alphaTest) {
            g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            g_dev->SetRenderState(D3DRS_ALPHAREF, 16);
            g_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        } else {
            g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        if (d.additive) {
            g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            g_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        } else if (d.translucent) {
            g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            g_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        } else {
            g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        }
        g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, d.nverts, 0, d.nidx / 3);
        if (customWorld) g_dev->SetTransform(D3DTS_WORLD, (D3DMATRIX*)world.m);
    };
    for (const auto& d : g_draws) drawOne(d, false);
    for (const auto& d : g_draws) drawOne(d, true);

    if (g_showSkel && g_skelVb && g_skelVerts >= 2) {
        g_dev->SetTexture(0, nullptr);
        g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetFVF(D3DFVF_XYZ);
        g_dev->SetStreamSource(0, g_skelVb, 0, sizeof(float) * 3);
        g_dev->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_XRGB(255, 220, 64));
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        g_dev->DrawPrimitive(D3DPT_LINELIST, 0, g_skelVerts / 2);
        g_dev->SetFVF(g_fvf);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    g_dev->EndScene();
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
}

LRESULT Preview3D_WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Preview3D_Render();
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_LBUTTONDOWN:
        g_drag = true;
        g_last.x = LOWORD(l);
        g_last.y = HIWORD(l);
        g_down.x = g_last.x;
        g_down.y = g_last.y;
        SetCapture(hwnd);
        SetFocus(hwnd);
        return 0;
    case WM_LBUTTONUP: {
        const int dx = (int)(short)LOWORD(l) - g_down.x;
        const int dy = (int)(short)HIWORD(l) - g_down.y;
        g_drag = false;
        ReleaseCapture();
        if (dx * dx + dy * dy < 25) g_attack = kAttackDur;  // click = attack
        Preview3D_Render();
        return 0;
    }
    case WM_KEYDOWN: {
        const unsigned k = (unsigned)w & 0xFF;
        if (k < 256) g_keys[k] = true;
        if (k >= '1' && k <= '4') {
            g_weaponMode = (int)(k - '1');
            Preview3D_Render();
        }
        if (k == 'B' || k == 'b') {
            g_useBones = !g_useBones;
            Preview3D_Render();
        }
        if (k == 'T' || k == 't') {
            // also drive the skeleton from the file's translation channel (it is
            // off by default, see MrbMotion::useTrans)
            g_motion.useTrans = !g_motion.useTrans;
            Preview3D_Render();
        }
        if (k == 'M' || k == 'm') {
            g_useSkin = !g_useSkin;
            Preview3D_Render();
        }
        return 0;
    }
    case WM_KEYUP: {
        const unsigned k = (unsigned)w & 0xFF;
        if (k < 256) g_keys[k] = false;
        return 0;
    }
    case WM_TIMER:
        if (g_active) Preview3D_Render();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    case WM_MOUSEMOVE:
        if (g_drag) {
            const int x = (short)LOWORD(l);
            const int y = (short)HIWORD(l);
            g_yaw += (x - g_last.x) * 0.01f;
            g_pitch += (y - g_last.y) * 0.01f;
            g_last.x = x;
            g_last.y = y;
            Preview3D_Render();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        const short delta = GET_WHEEL_DELTA_WPARAM(w);
        g_dist *= (delta > 0) ? 0.90f : 1.11f;
        if (g_dist < g_radius * 0.35f) g_dist = g_radius * 0.35f;
        if (g_dist > g_radius * 12.0f) g_dist = g_radius * 12.0f;
        Preview3D_Render();
        return 0;
    }
    case WM_RBUTTONUP:
        Preview3D_ToggleSkeleton();
        Preview3D_Render();
        return 0;
    case WM_SIZE:
        Preview3D_Resize();
        Preview3D_Render();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

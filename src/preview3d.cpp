#include "preview3d.h"

#include "dxg.h"
#include "image_io.h"
#include "vra_text.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
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
};

HWND g_hwnd = nullptr;
IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_dev = nullptr;
D3DPRESENT_PARAMETERS g_pp{};
bool g_active = false;
bool g_showSkel = true;
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
DWORD g_fvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

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
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
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
    if (!g_dev || src.pos.empty() || src.idx.empty()) return false;
    DrawMesh d;
    d.nidx = (unsigned)src.idx.size();
    d.nverts = (unsigned)src.pos.size();
    d.tex = tex;
    if (mat) {
        d.twoSided = mat->twoSided;
        d.alphaTest = mat->alphaTest || mat->blend == "translucent";
        d.additive = mat->blend == "add";
        d.translucent = mat->blend == "translucent";
    }
    const UINT vbytes = (UINT)(src.pos.size() * sizeof(Vertex));
    if (FAILED(g_dev->CreateVertexBuffer(vbytes, 0, g_fvf, D3DPOOL_MANAGED, &d.vb, nullptr)))
        return false;
    void* vp = nullptr;
    if (FAILED(d.vb->Lock(0, vbytes, &vp, 0))) {
        d.vb->Release();
        return false;
    }
    auto* v = (Vertex*)vp;
    for (size_t i = 0; i < src.pos.size(); ++i) {
        v[i].x = src.pos[i].x;
        v[i].y = src.pos[i].y;
        v[i].z = src.pos[i].z;
        if (i < src.nrm.size()) {
            v[i].nx = src.nrm[i].x;
            v[i].ny = src.nrm[i].y;
            v[i].nz = src.nrm[i].z;
        } else {
            v[i].nx = 0;
            v[i].ny = 1;
            v[i].nz = 0;
        }
        if (i < src.uv.size()) {
            v[i].u = src.uv[i].u;
            v[i].v = src.uv[i].v;
        } else {
            v[i].u = 0;
            v[i].v = 0;
        }
    }
    d.vb->Unlock();
    const UINT ibytes = (UINT)(src.idx.size() * sizeof(uint32_t));
    if (FAILED(g_dev->CreateIndexBuffer(ibytes, 0, D3DFMT_INDEX32, D3DPOOL_MANAGED, &d.ib, nullptr))) {
        d.vb->Release();
        return false;
    }
    void* ip = nullptr;
    if (FAILED(d.ib->Lock(0, ibytes, &ip, 0))) {
        d.vb->Release();
        d.ib->Release();
        return false;
    }
    memcpy(ip, src.idx.data(), ibytes);
    d.ib->Unlock();
    g_triCount += d.nidx / 3;
    g_draws.push_back(d);
    return true;
}

void UploadSkeleton(const std::vector<DxgBone>& bones) {
    if (!g_dev || bones.size() < 2) return;
    std::vector<float> lines;
    for (size_t i = 0; i < bones.size(); ++i) {
        const int p = bones[i].parent;
        if (p < 0 || p >= (int)bones.size()) continue;
        lines.push_back(bones[(size_t)p].pos.x);
        lines.push_back(bones[(size_t)p].pos.y);
        lines.push_back(bones[(size_t)p].pos.z);
        lines.push_back(bones[i].pos.x);
        lines.push_back(bones[i].pos.y);
        lines.push_back(bones[i].pos.z);
    }
    if (lines.empty()) return;
    g_skelVerts = (unsigned)(lines.size() / 3);
    const UINT bytes = (UINT)(lines.size() * sizeof(float));
    if (FAILED(g_dev->CreateVertexBuffer(bytes, 0, D3DFVF_XYZ, D3DPOOL_MANAGED, &g_skelVb, nullptr)))
        return;
    void* p = nullptr;
    if (SUCCEEDED(g_skelVb->Lock(0, bytes, &p, 0))) {
        memcpy(p, lines.data(), bytes);
        g_skelVb->Unlock();
    }
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
    g_dist = g_radius * 2.4f;
    g_yaw = 0.55f;
    g_pitch = 0.18f;
}

}  // namespace

bool Preview3D_Init(HWND hwnd) {
    g_hwnd = hwnd;
    return CreateDevice();
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

    FitCamera(model);
    g_meshCount = 0;
    g_triCount = 0;
    for (const auto& mesh : model.meshes) {
        if (mesh.lod1) continue;
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
    g_boneCount = (unsigned)model.bones.size();
    UploadSkeleton(model.bones);
    g_active = g_meshCount > 0;
    return g_active;
}

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

    if (g_pitch < -1.2f) g_pitch = -1.2f;
    if (g_pitch > 1.2f) g_pitch = 1.2f;
    const float cp = cosf(g_pitch), sp = sinf(g_pitch);
    const float cy = cosf(g_yaw), sy = sinf(g_yaw);
    const float ex = g_cx + sy * cp * g_dist;
    const float ey = g_cy + sp * g_dist;
    const float ez = g_cz + cy * cp * g_dist;

    Mat4 view, proj;
    LookAt(ex, ey, ez, g_cx, g_cy, g_cz, view);
    Perspective(3.14159265f / 4.5f, (float)cw / (float)ch, (std::max)(1.0f, g_radius * 0.02f),
                g_dist + g_radius * 8.0f, proj);
    g_dev->SetTransform(D3DTS_VIEW, (D3DMATRIX*)view.m);
    g_dev->SetTransform(D3DTS_PROJECTION, (D3DMATRIX*)proj.m);
    Mat4 world;
    Ident(world);
    g_dev->SetTransform(D3DTS_WORLD, (D3DMATRIX*)world.m);

    auto drawOne = [&](const DrawMesh& d, bool transparent) {
        if (!d.vb || !d.ib || d.nidx < 3) return;
        const bool isT = d.translucent || d.additive;
        if (isT != transparent) return;
        g_dev->SetStreamSource(0, d.vb, 0, sizeof(Vertex));
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
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_drag = false;
        ReleaseCapture();
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

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fpmf.h"
#include "image_io.h"
#include "preview3d.h"
#include "text_encoding.h"
#include "vra_text.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum {
    ID_TREE = 1001,
    ID_LIST = 1002,
    ID_PREVIEW = 1003,
    ID_STATUS = 1004,
    ID_FILTER = 1005,
    ID_SEARCH = 1006,
    ID_IMGPREV = 1007,
    ID_LBL_FILTER = 1008,
    ID_LBL_SEARCH = 1009,
    ID_BTN_DIR = 1010,
    ID_LBL_DIR = 1011,
    ID_CHK_STRUCT = 1012,
    ID_PROGRESS = 1013,
    ID_OPEN = 4001,
    ID_EXTRACT = 4002,
    ID_EXTRACT_ALL = 4003,
    ID_EXIT = 4004,
    ID_EXPORT_PNG = 4005,
    ID_SET_EXTRACT = 4006,
    ID_VIEW_PREVIEW = 4007,
    ID_BG_AUTO = 4008,
    ID_BG_DARK = 4009,
    ID_BG_LIGHT = 4010,
    ID_BG_CHECK = 4011,
    ID_CTX_EXPORT = 4101,
    ID_CTX_EXPORT_PNG = 4102,
    ID_CTX_SETDIR = 4103,
    ID_CTX_TREE_ALL = 4110,
    ID_CTX_TREE_VISIBLE = 4111,
    ID_CTX_TREE_PNG = 4112,
    ID_D3DPREV = 1014,
    IDT_FILTER = 1,
    IDT_PREV3D = 2,
    WM_APP_LOAD_DONE = WM_APP + 20,
    WM_APP_PROGRESS = WM_APP + 21,
};

static HWND g_tree, g_list, g_preview, g_status, g_main, g_prevFrame;
static HWND g_filter, g_search, g_img, g_d3dView, g_lblFilter, g_lblSearch, g_btnDir, g_lblDir, g_chkStruct, g_progress;
static void LayoutPreview();
static void ShowPreviewFrame(bool show);
static void ShowPreview(int arch, int fileIndex);
static void ClearBitmap();
static void FillTree();
static void FillList(int node);
static fpmf::Catalog g_cat;
static std::vector<uint8_t> g_heldAnim;  // keeps the loaded .mrb alive
static int g_arch = -1;
static int g_vfsSel = 0;
static bool g_suspendNotify = false;
static std::vector<std::pair<int, int>> g_visible;

struct VfsNode {
    std::wstring name;
    std::wstring full;
    std::vector<int> children;
    std::vector<std::pair<int, int>> files;
};
static std::vector<VfsNode> g_vfs;
static std::vector<unsigned> g_vfsMatch;
static std::map<std::wstring, std::vector<std::pair<int, int>>> g_unitIndex;
static std::map<std::wstring, std::pair<int, int>> g_pathIndex;

// favms_ok_list.csv 机体 ID → 客户端资源坐标的绑定表，由
// datatables/build_favms_asset_bind.py 生成（viewer/ms_bind.csv）。
// 两套编号：練度ID(7 位, 机体清单主键) / モデルID(9 位, chara 资源坐标)。
struct MsBind {
    std::wstring msId;     // 練度ID（7 位）
    std::wstring name;     // 机体名
    std::wstring modelId;  // モデルID（9 位）
    std::wstring dir;      // chara/01/24/1201
    std::wstring unit;     // 资源目录末段 = C（4 位），与 UnitIdFromPath 取值一致
};
static std::map<std::wstring, MsBind> g_msBind;       // key = 練度ID
static std::map<std::wstring, MsBind> g_msBindModel;  // key = モデルID
static std::set<std::wstring> g_idDirs;               // 「按 ID 搜」命中的资源目录 chara/01/24/1201
static std::set<std::wstring> g_idCodes;              // 同机体的文件名编码 0124_1201（A+B 段 + "_" + C 段）
static std::set<std::wstring> g_scanDirs;             // CLI 扫描出的资源目录集合（GUI 用 g_pathIndex）
static std::wstring g_idDetail;                       // 状态栏上的 ID 命中说明
static std::wstring g_msBindPath;                     // 实际加载的绑定表路径
static ImageRgba g_imgData;
static HBITMAP g_bmp = nullptr;
static std::filesystem::path g_extractDir;
static std::wstring g_filterExt;
static std::wstring g_searchText;
static int g_bgMode = 0;
static int g_imgMean = -1;
static bool g_exportStruct = true;
static std::atomic<unsigned> g_loadSeq{0};
static std::atomic<bool> g_loading{false};
static std::atomic<bool> g_exporting{false};

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s)
        if (c >= L'A' && c <= L'Z') c = wchar_t(c + 32);
    return s;
}

static std::filesystem::path ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path();
}

static std::filesystem::path ConfigPath() { return ExeDir() / L"config.ini"; }

static std::wstring IniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"") {
    wchar_t buf[1024]{};
    GetPrivateProfileStringW(sec, key, def, buf, 1024, ConfigPath().wstring().c_str());
    return buf;
}

static void IniSet(const wchar_t* sec, const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(sec, key, val.c_str(), ConfigPath().wstring().c_str());
}

// The preview frame is a top-level window whose rect is remembered in
// config.ini, so a stale rect (monitor unplugged, resolution/DPI changed, or
// a rect captured while the frame was minimized) would restore it off-screen
// with no way to drag it back. Snap it into the work area of a real monitor
// before creating it.
static RECT ClampToVisibleDesktop(RECT r) {
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w < 320) w = 320;
    if (h < 240) h = 240;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        RECT wa{};
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) return r;
        mi.rcWork = wa;
    }
    const RECT wa = mi.rcWork;
    const int waW = wa.right - wa.left;
    const int waH = wa.bottom - wa.top;
    if (w > waW) w = waW;
    if (h > waH) h = waH;
    int x = r.left;
    int y = r.top;
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + w > wa.right) x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    RECT out{x, y, x + w, y + h};
    return out;
}

static void SaveConfig() {
    if (!g_cat.dataRoot.empty()) IniSet(L"paths", L"data", g_cat.dataRoot.wstring());
    IniSet(L"paths", L"extract", g_extractDir.wstring());
    IniSet(L"ui", L"filter", g_filterExt);
    IniSet(L"ui", L"search", g_searchText);
    IniSet(L"ui", L"bg", std::to_wstring(g_bgMode));
    IniSet(L"ui", L"export_struct", g_exportStruct ? L"1" : L"0");
    if (g_prevFrame && IsWindow(g_prevFrame)) {
        // rcNormalPosition, and never while minimized: GetWindowRect() on a
        // minimized window returns the -32000, -32000 "iconic" rect, which
        // would park the frame off-screen on the next start.
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(g_prevFrame, &wp) && wp.showCmd != SW_SHOWMINIMIZED) {
            const RECT& r = wp.rcNormalPosition;
            IniSet(L"ui", L"prev_x", std::to_wstring(r.left));
            IniSet(L"ui", L"prev_y", std::to_wstring(r.top));
            IniSet(L"ui", L"prev_w", std::to_wstring(r.right - r.left));
            IniSet(L"ui", L"prev_h", std::to_wstring(r.bottom - r.top));
        }
        IniSet(L"ui", L"prev_show",
               (IsWindowVisible(g_prevFrame) && !IsIconic(g_prevFrame)) ? L"1" : L"0");
    }
}

static void SetStatus(const wchar_t* s) { SendMessageW(g_status, SB_SETTEXT, 0, (LPARAM)s); }

struct ProgInfo {
    unsigned seq = 0;
    unsigned done = 0;
    unsigned total = 0;
    wchar_t text[200]{};
};

static void PlaceProgressBar() {
    if (!g_main || !g_progress) return;
    RECT rc{};
    GetClientRect(g_main, &rc);
    RECT sb{};
    int sbH = 22;
    if (g_status) {
        GetWindowRect(g_status, &sb);
        sbH = sb.bottom - sb.top;
    }
    const int barW = 240;
    const int pad = 6;
    const int h = (std::max)(10, sbH - 6);
    const int x = rc.right - pad - barW;
    const int y = rc.bottom - sbH + 3;
    MoveWindow(g_progress, x, y, barW, h, TRUE);
    SetWindowPos(g_progress, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

static void ShowProgressBar(bool show) {
    if (!g_progress) return;
    ShowWindow(g_progress, show ? SW_SHOW : SW_HIDE);
    if (show) PlaceProgressBar();
}

static void SetProgressPos(unsigned done, unsigned total) {
    if (!g_progress) return;
    LONG style = GetWindowLongW(g_progress, GWL_STYLE);
    if (style & PBS_MARQUEE) {
        SendMessageW(g_progress, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongW(g_progress, GWL_STYLE, style & ~PBS_MARQUEE);
    }
    unsigned maxv = 100, pos = 0;
    if (total == 0) {
        pos = done == 0 ? 0 : (4 + (done % 16));
    } else {
        maxv = total < 1 ? 1 : total;
        pos = done > maxv ? maxv : done;
    }
    SendMessageW(g_progress, PBM_SETRANGE32, 0, (LPARAM)maxv);
    if (pos < maxv)
        SendMessageW(g_progress, PBM_SETPOS, (WPARAM)(pos + 1), 0);
    SendMessageW(g_progress, PBM_SETPOS, (WPARAM)pos, 0);
}

static void ApplyProgressInfo(ProgInfo* p) {
    if (!p) return;
    if (p->seq && p->seq != g_loadSeq.load()) {
        delete p;
        return;
    }
    if (p->text[0]) SetStatus(p->text);
    SetProgressPos(p->done, p->total);
    PlaceProgressBar();
    delete p;
}

static void PostProgress(HWND hwnd, unsigned seq, unsigned done, unsigned total, const wchar_t* text) {
    const bool last = total > 0 && done >= total;
    static thread_local DWORD s_tick = 0;
    const DWORD now = GetTickCount();
    if (!last && done > 0 && now - s_tick < 50)
        return;
    s_tick = now;
    auto* p = new ProgInfo();
    p->seq = seq;
    p->done = done;
    p->total = total;
    if (text) wcsncpy_s(p->text, text, _TRUNCATE);
    if (!IsWindow(hwnd) || !PostMessageW(hwnd, WM_APP_PROGRESS, 0, (LPARAM)p))
        delete p;
}

struct ScanCtx {
    HWND hwnd = nullptr;
    unsigned seq = 0;
};

static void OnScanProgress(unsigned done, unsigned total, const wchar_t* name, void* ctx) {
    auto* c = (ScanCtx*)ctx;
    if (!c || c->seq != g_loadSeq.load()) return;
    wchar_t buf[200];
    unsigned pct = 0;
    if (total == 0) {
        swprintf_s(buf, L"Looking for .hed archives...  %u", done);
        pct = done == 0 ? 0 : (2 + (done % 8));
    } else {
        swprintf_s(buf, L"Loading archives  %u / %u  %s", done, total, name ? name : L"");
        pct = 10 + (unsigned)(60ull * done / (total ? total : 1));
        if (done >= total) pct = 70;
    }
    PostProgress(c->hwnd, c->seq, pct, 100, buf);
}

static void PumpUi() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool IsBusy() { return g_loading || g_exporting; }

static void SetDirLabel() {
    std::wstring t = g_extractDir.empty() ? L"export: (not set — first export will ask)"
                                          : (L"export: " + g_extractDir.wstring());
    SetWindowTextW(g_lblDir, t.c_str());
    SendMessageW(g_status, SB_SETTEXT, 1, (LPARAM)t.c_str());
}

static const std::filesystem::path& ArchRoot(const fpmf::Archive& ar) {
    return ar.dataRoot.empty() ? g_cat.dataRoot : ar.dataRoot;
}

static std::wstring RelHed(const fpmf::Archive& ar) {
    std::error_code ec;
    auto rel = std::filesystem::relative(ar.hedPath, ArchRoot(ar), ec);
    if (ec) return ar.hedPath.wstring();
    return rel.wstring();
}

static std::wstring DisplayName(const fpmf::FileEntry& e) {
    std::wstring p = e.path.empty() ? e.folder : e.path;
    if (p.size() >= 2 && p[0] == L'.' && (p[1] == L'\\' || p[1] == L'/')) p.erase(0, 2);
    return p;
}

static std::wstring FileExt(const std::wstring& name) {
    return ToLower(std::filesystem::path(name).extension().wstring());
}

// Only these plain-text tables get encoding sniffing; every other format keeps
// the old system-codepage behaviour.
static bool IsTextTableExt(const std::wstring& ext) {
    return ext == L".csv" || ext == L".txt" || ext == L".xml" || ext == L".cfg";
}

static void ResolveIdSearch();

static void ReadFilterUi() {
    wchar_t buf[128]{};
    GetWindowTextW(g_filter, buf, 128);
    g_filterExt = ToLower(buf);
    while (!g_filterExt.empty() && g_filterExt.front() == L' ') g_filterExt.erase(g_filterExt.begin());
    while (!g_filterExt.empty() && g_filterExt.back() == L' ') g_filterExt.pop_back();
    if (g_filterExt == L"(all)" || g_filterExt == L"*" || g_filterExt == L"all") g_filterExt.clear();
    if (!g_filterExt.empty() && g_filterExt[0] != L'.') g_filterExt.insert(g_filterExt.begin(), L'.');

    wchar_t sbuf[256]{};
    GetWindowTextW(g_search, sbuf, 256);
    g_searchText = ToLower(sbuf);
    ResolveIdSearch();
}

static std::wstring VfsPathOf(const fpmf::Archive& ar, const fpmf::FileEntry& e);
static std::wstring UnitIdFromPath(std::wstring path);
static bool PathMatchesUnit(const std::wstring& lowerPath);

static bool MatchesFilter(const fpmf::Archive& ar, const fpmf::FileEntry& e) {
    const std::wstring name = DisplayName(e);
    if (!g_filterExt.empty() && FileExt(name) != g_filterExt) return false;
    // 「按机体 ID 搜」模式：只认绑定的资源目录（含同机体的 anim3/tex 文件名编码），忽略文本内容。
    if (!g_idDirs.empty() || !g_idCodes.empty())
        return PathMatchesUnit(ToLower(VfsPathOf(ar, e)));
    if (g_searchText.empty()) return true;
    if (ToLower(name).find(g_searchText) != std::wstring::npos) return true;
    return ToLower(VfsPathOf(ar, e)).find(g_searchText) != std::wstring::npos;
}

static std::wstring NormVfs(std::wstring p) {
    for (auto& c : p)
        if (c == L'\\') c = L'/';
    while (p.size() >= 2 && p[0] == L'.' && p[1] == L'/') p.erase(0, 2);
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    return p;
}

static std::wstring HedMount(const fpmf::Archive& ar) {
    std::error_code ec;
    auto rel = std::filesystem::relative(ar.hedPath.parent_path(), ArchRoot(ar), ec);
    std::wstring mount;
    if (!ec && !rel.empty() && rel != L".") mount = NormVfs(rel.wstring());
    const auto stem = ar.hedPath.stem().wstring();
    if (!mount.empty()) mount += L"/";
    mount += stem;
    return mount;
}

static std::wstring VfsPathOf(const fpmf::Archive& ar, const fpmf::FileEntry& e) {
    const std::wstring entry = NormVfs(DisplayName(e));
    const std::wstring mount = HedMount(ar);
    if (entry.empty()) return mount;
    if (entry == mount || entry.find(mount + L"/") == 0) return entry;
    if (mount.empty()) return entry;
    return mount + L"/" + entry;
}

static bool IsFourDigits(const std::wstring& s) {
    return s.size() == 4 && iswdigit(s[0]) && iswdigit(s[1]) && iswdigit(s[2]) && iswdigit(s[3]);
}

static bool IsAllDigits(const std::wstring& s) {
    if (s.empty()) return false;
    for (wchar_t c : s)
        if (!iswdigit(c)) return false;
    return true;
}

static void AddIdTarget(const std::wstring& dir);

// 资源树里是否存在该目录（GUI 用已建好的路径索引，CLI 用扫描出的目录集合）。
static bool DirExists(const std::wstring& lowerDir) {
    const std::wstring prefix = lowerDir + L"/";
    if (!g_pathIndex.empty()) {
        auto it = g_pathIndex.lower_bound(prefix);
        return it != g_pathIndex.end() && it->first.compare(0, prefix.size(), prefix) == 0;
    }
    return g_scanDirs.count(lowerDir) > 0;
}

// ★ID → 资源ID 的规则（客户端实证：`sub_473390` 十进制拆位 + `sub_49D030` 拼路径）
//   常规：  モデルID = A(1)B(2)C(4)NN(2) ⇒ 目录 chara/0A/B/C，文件 MSM_0A<B>_<C>_<NN>
//           例 124120101 ⇒ A=1,B=24,C=1201,NN=01 ⇒ chara/01/24/1201
//   大编号：kind==1 的少数机型 ⇒ 目录 chara/01/1<B>/0<C>，文件 MSM_01<1B>_<0C>_<NN>
//           例 201330201 ⇒ chara/01/101/03302（实测 25 例，均为 A=2）
// 两条规则由 names/chara目录-ID清单.csv（1646 条实测）反查确认：1621 常规 + 25 大编号，0 例外。
// 9 位 ID 直接算得出资源坐标，不需要映射表；大编号只在常规目录不存在时才启用（防误配）。
static bool ApplyModelIdRule(const std::wstring& id, std::wstring& dirOut) {
    if (id.size() != 9 || !IsAllDigits(id)) return false;
    const std::wstring a = id.substr(0, 1);
    const std::wstring b = id.substr(1, 2);
    const std::wstring c = id.substr(3, 4);
    std::wstring dir = L"chara/0" + a + L"/" + b + L"/" + c;
    if (!DirExists(ToLower(dir))) {
        const std::wstring big = L"chara/01/1" + b + L"/0" + c;
        if (DirExists(ToLower(big))) dir = big;
    }
    AddIdTarget(dir);  // 目录 + 文件名编码（两套格式都由它拼）
    dirOut = dir;
    return true;
}

// 记录一个要定位的机体资源目录：目录本身 + 文件名里的 8 位编码（MSM_0124_1201_01
// 里的 0124_1201）。只看 4 位 C 段会撞车——chara/01/24/1201 与 chara/01/01/1201
// 的 C 段同为 1201，却不是同一台机体，所以必须带上 A/B 段。
static void AddIdTarget(const std::wstring& dir) {
    if (dir.empty()) return;
    std::wstring rest = ToLower(dir);
    g_idDirs.insert(rest);
    if (rest.compare(0, 6, L"chara/") == 0) rest.erase(0, 6);
    std::vector<std::wstring> seg;
    size_t p0 = 0;
    while (p0 <= rest.size()) {
        size_t sl = rest.find(L'/', p0);
        if (sl == std::wstring::npos) sl = rest.size();
        if (sl > p0) seg.push_back(rest.substr(p0, sl - p0));
        if (sl == rest.size()) break;
        p0 = sl + 1;
    }
    if (seg.size() >= 3) g_idCodes.insert(seg[0] + seg[1] + L"_" + seg[2]);
}

static bool PathMatchesUnit(const std::wstring& lowerPath) {
    for (const auto& d : g_idDirs)
        if (lowerPath.find(d) != std::wstring::npos) return true;
    for (const auto& c : g_idCodes)
        if (lowerPath.find(c) != std::wstring::npos) return true;
    return false;
}

// ms_bind.csv 列：練度ID,机体名,モデルID,资源目录,模型文件,目录形态数,来源,校验
// 只认后 4 段里的前 4 列；# 开头为注释，UTF-8（容忍 BOM）。
static bool LoadMsBindFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);
    int n = 0;
    size_t start = 0;
    while (start <= data.size()) {
        size_t nl = data.find('\n', start);
        std::string line = data.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? data.size() + 1 : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::wstring> c;
        std::string cur;
        for (char ch : line) {
            if (ch == ',') {
                c.push_back(Widen(cur));
                cur.clear();
            } else if (ch != '\r') {
                cur.push_back(ch);
            }
        }
        c.push_back(Widen(cur));
        if (c.size() < 4) continue;
        MsBind b;
        b.msId = c[0];
        b.name = c[1];
        b.modelId = c[2];
        b.dir = c[3];
        const size_t sl = b.dir.find_last_of(L'/');
        b.unit = sl == std::wstring::npos ? b.dir : b.dir.substr(sl + 1);
        if (!IsAllDigits(b.msId) || !IsFourDigits(b.unit)) continue;
        g_msBind[b.msId] = b;
        if (!b.modelId.empty()) g_msBindModel[b.modelId] = b;
        ++n;
    }
    return n > 0;
}

// 绑定表查找顺序：config.ini 的 paths/ms_bind → exe 目录 ms_bind.csv →
// 仓库里的 datatables/names/favms_ok_list-资源绑定.csv（exe 在 viewer/ 或 viewer/build/）。
static void LoadMsBind() {
    g_msBind.clear();
    g_msBindModel.clear();
    g_msBindPath.clear();
    std::vector<std::filesystem::path> cands;
    const std::wstring ini = IniGet(L"paths", L"ms_bind");
    if (!ini.empty()) cands.push_back(ini);
    cands.push_back(ExeDir() / L"ms_bind.csv");
    // exe 常落在 viewer/build/Release/ 之类的子目录，逐级向上找仓库里的绑定表。
    std::filesystem::path up = ExeDir();
    for (int i = 0; i < 4; ++i) {
        cands.push_back(up / L"datatables" / L"names" / L"favms_ok_list-资源绑定.csv");
        if (!up.has_parent_path() || up.parent_path() == up) break;
        up = up.parent_path();
    }
    for (const auto& p : cands) {
        if (LoadMsBindFile(p)) {
            g_msBindPath = p.wstring();
            return;
        }
    }
}

static const MsBind* LookupMsBind(const std::wstring& id) {
    auto it = g_msBind.find(id);
    if (it != g_msBind.end()) return &it->second;
    auto it2 = g_msBindModel.find(id);
    if (it2 != g_msBindModel.end()) return &it2->second;
    return nullptr;
}

// 搜索框里输入纯数字且能在绑定表命中时，切成「按机体 ID 搜」：
// 練度ID(7 位) / モデルID(9 位) 都认，命中后只显示该机体 chara/<A>/<B>/<C> 下的资源。
// 把搜索词解析成要定位的资源坐标，填充 g_idDirs / g_idCodes。
//   ① 9 位数字 = モデルID ⇒ **按规则**拆位算目录（不看表）
//   ② 7 位数字 = 練度ID ⇒ 两套编号独立（M01 §7.6.4），只能查 ms_bind.csv
static bool ResolveIdToTargets(const std::wstring& id, std::wstring* detailOut) {
    g_idDirs.clear();
    g_idCodes.clear();
    std::wstring detail;
    bool ok = false;
    std::wstring dir;
    if (ApplyModelIdRule(id, dir)) {
        detail = L"ID " + id + L"  [rule] -> " + dir;
        if (!DirExists(ToLower(dir))) detail += L"   (not in resource tree)";
        if (const MsBind* b = LookupMsBind(id))
            if (!b->name.empty()) detail += L"   " + b->name;
        ok = true;
    } else if (const MsBind* b = LookupMsBind(id)) {
        AddIdTarget(b->dir);
        detail = L"ID " + id + L"  [table] = " + b->name;
        if (!b->modelId.empty()) detail += L"  [モデルID " + b->modelId + L"]";
        detail += L"  ->  " + b->dir;
        ok = true;
    } else if (id.size() == 7 || id.size() == 9) {
        detail = L"ID " + id + L": no resource found";
        if (id.size() == 7) detail += L"  (練度ID: separate numbering, needs ms_bind.csv)";
        else detail += L"  (no chara/0A/B/C for this モデルID)";
        if (g_msBind.empty() && id.size() == 7) detail += L"  [ms_bind.csv not found]";
    }
    if (detailOut) *detailOut = detail;
    return ok;
}

static void ResolveIdSearch() {
    g_idDirs.clear();
    g_idCodes.clear();
    g_idDetail.clear();
    if (!IsAllDigits(g_searchText)) return;
    ResolveIdToTargets(g_searchText, &g_idDetail);
}

static std::wstring FileNameOf(const std::wstring& path) {
    const auto sl = path.find_last_of(L'/');
    return sl == std::wstring::npos ? path : path.substr(sl + 1);
}

static std::wstring UnitIdFromPath(std::wstring path) {
    for (auto& c : path)
        if (c == L'\\') c = L'/';
    const std::wstring fn = FileNameOf(path);
    const size_t us1 = fn.find(L'_');
    if (us1 >= 2 && us1 <= 4 && us1 + 9 <= fn.size() && fn[us1 + 5] == L'_') {
        const std::wstring series = fn.substr(us1 + 1, 4);
        const std::wstring unit = fn.substr(us1 + 6, 4);
        if (IsFourDigits(series) && IsFourDigits(unit)) return unit;
    }
    const std::wstring low = ToLower(path);
    if (low.find(L"chara") != std::wstring::npos) {
        size_t start = 0;
        while (start < path.size()) {
            size_t sl = path.find(L'/', start);
            if (sl == std::wstring::npos) sl = path.size();
            const std::wstring part = path.substr(start, sl - start);
            if (IsFourDigits(part)) return part;
            start = sl + 1;
        }
    }
    if (low.find(L"/icon/") != std::wstring::npos || low.find(L"battle") != std::wstring::npos ||
        low.find(L"killlog") != std::wstring::npos) {
        std::wstring f = ToLower(fn);
        size_t i = 0;
        if (f.size() > 8 && f[0] == L'b' && iswdigit(f[1])) i = 1;
        if (f.size() >= i + 8 && IsFourDigits(f.substr(i, 4)) && IsFourDigits(f.substr(i + 4, 4)))
            return f.substr(i, 4);
    }
    return {};
}

static bool LooksLikeUnitNode(int node) {
    if (node <= 0 || node >= (int)g_vfs.size()) return false;
    if (!IsFourDigits(g_vfs[node].name)) return false;
    const std::wstring low = ToLower(g_vfs[node].full);
    if (low.find(L"chara") != std::wstring::npos) return true;
    for (int c : g_vfs[node].children) {
        const std::wstring cn = ToLower(g_vfs[c].name);
        if (cn == L"tex" || cn == L"model" || cn == L"anim" || cn == L"attr") return true;
    }
    return false;
}

static std::wstring FileKindOf(const std::wstring& path) {
    const std::wstring low = ToLower(path);
    const std::wstring fn = ToLower(FileNameOf(path));
    const std::wstring ext = FileExt(path);
    if (low.find(L"battle") != std::wstring::npos || low.find(L"killlog") != std::wstring::npos ||
        (low.find(L"/icon/") != std::wstring::npos && ext == L".dds"))
        return L"icon";
    if (low.find(L"/tex/") != std::wstring::npos || fn.rfind(L"mst_", 0) == 0) return L"tex";
    if (low.find(L"/model/") != std::wstring::npos || ext == L".dxg" || ext == L".txg" ||
        fn.rfind(L"msm_", 0) == 0)
        return L"model";
    if (low.find(L"/attr/") != std::wstring::npos || ext == L".vra" || fn.rfind(L"msz_", 0) == 0)
        return L"attr";
    if (low.find(L"/anim/") != std::wstring::npos || ext == L".mrb" || fn.rfind(L"msa_", 0) == 0)
        return L"anim";
    return L"other";
}

static bool IsImageExt(const std::wstring& ext) {
    return ext == L".dds" || ext == L".png" || ext == L".tga" || ext == L".jpg" ||
           ext == L".jpeg" || ext == L".bmp" || ext == L".hdr";
}

static bool IsMeshAsset(const std::wstring& path) {
    const std::wstring ext = FileExt(path);
    const std::wstring kind = FileKindOf(path);
    return ext == L".dxg" || ext == L".txg" || ext == L".mrb" || ext == L".vra" ||
           kind == L"model" || kind == L"anim" || kind == L"attr";
}

static int KindOrder(const std::wstring& kind) {
    if (kind == L"icon") return 0;
    if (kind == L"tex") return 1;
    if (kind == L"model") return 2;
    if (kind == L"attr") return 3;
    if (kind == L"anim") return 4;
    return 5;
}

static int PreviewScore(const std::wstring& path) {
    const std::wstring ext = FileExt(path);
    if (ext != L".dds" && ext != L".png") return -1;
    const std::wstring low = ToLower(path);
    const std::wstring fn = ToLower(FileNameOf(path));
    int s = 10;
    if (low.find(L"battle") != std::wstring::npos || low.find(L"killlog") != std::wstring::npos) s += 80;
    if (fn.rfind(L"mst_", 0) == 0) s += 40;
    if (low.find(L"/tex/") != std::wstring::npos) s += 20;
    if (fn.find(L"01c.") != std::wstring::npos) s += 30;
    if (fn.find(L"_001") != std::wstring::npos || fn.find(L"_002") != std::wstring::npos) s -= 8;
    return s;
}

static std::pair<int, int> BestUnitPreview(const std::wstring& unit) {
    std::pair<int, int> best{-1, -1};
    int bestScore = -1;
    auto consider = [&](const std::vector<std::pair<int, int>>& files) {
        for (auto pr : files) {
            if (pr.first < 0 || pr.first >= (int)g_cat.archives.size()) continue;
            const auto& ar = g_cat.archives[pr.first];
            if (pr.second < 0 || pr.second >= (int)ar.files.size()) continue;
            const int s = PreviewScore(VfsPathOf(ar, ar.files[pr.second]));
            if (s > bestScore) {
                bestScore = s;
                best = pr;
            }
        }
    };
    if (!unit.empty()) {
        auto it = g_unitIndex.find(unit);
        if (it != g_unitIndex.end()) consider(it->second);
    }
    if (best.first < 0) consider(g_visible);
    return best;
}

static void CollectRelatedFiles(const std::wstring& unit, std::vector<std::pair<int, int>>& out) {
    auto it = g_unitIndex.find(unit);
    if (it == g_unitIndex.end()) return;
    for (auto pr : it->second) {
        if (pr.first < 0 || pr.first >= (int)g_cat.archives.size()) continue;
        const auto& ar = g_cat.archives[pr.first];
        if (pr.second < 0 || pr.second >= (int)ar.files.size()) continue;
        if (MatchesFilter(ar, ar.files[pr.second])) out.push_back(pr);
    }
    std::sort(out.begin(), out.end(), [](auto a, auto b) {
        const auto pa = VfsPathOf(g_cat.archives[a.first], g_cat.archives[a.first].files[a.second]);
        const auto pb = VfsPathOf(g_cat.archives[b.first], g_cat.archives[b.first].files[b.second]);
        const int ka = KindOrder(FileKindOf(pa));
        const int kb = KindOrder(FileKindOf(pb));
        if (ka != kb) return ka < kb;
        return pa < pb;
    });
}

static bool Wants3DPreview(const std::wstring& path) {
    const std::wstring ext = FileExt(path);
    return ext == L".dxg" || ext == L".txg" || ext == L".vra" || ext == L".mrb";
}

static std::pair<int, int> FindByVfsPath(std::wstring needle) {
    needle = ToLower(NormVfs(needle));
    if (needle.empty()) return {-1, -1};
    auto it = g_pathIndex.find(needle);
    if (it != g_pathIndex.end()) return it->second;
    for (const auto& kv : g_pathIndex) {
        if (kv.first.size() >= needle.size() &&
            kv.first.compare(kv.first.size() - needle.size(), needle.size(), needle) == 0)
            return kv.second;
    }
    const std::wstring fn = FileNameOf(needle);
    if (!fn.empty()) {
        for (const auto& kv : g_pathIndex)
            if (FileNameOf(kv.first) == fn) return kv.second;
    }
    return {-1, -1};
}

static std::wstring StemOf(const std::wstring& path) {
    std::wstring fn = FileNameOf(path);
    const auto dot = fn.find_last_of(L'.');
    if (dot != std::wstring::npos) fn.resize(dot);
    return fn;
}

static std::pair<int, int> FindUnitByExt(const std::wstring& unit, const wchar_t* ext,
                                         const std::wstring& preferStem) {
    auto it = g_unitIndex.find(unit);
    if (it == g_unitIndex.end()) return {-1, -1};
    std::pair<int, int> best{-1, -1};
    int bestScore = -1;
    const std::wstring wantExt = ToLower(ext);
    for (auto pr : it->second) {
        if (pr.first < 0 || pr.first >= (int)g_cat.archives.size()) continue;
        const auto& ar = g_cat.archives[pr.first];
        if (pr.second < 0 || pr.second >= (int)ar.files.size()) continue;
        const auto path = VfsPathOf(ar, ar.files[pr.second]);
        if (FileExt(path) != wantExt) continue;
        const std::wstring fn = ToLower(FileNameOf(path));
        int s = 10;
        if (!preferStem.empty() && ToLower(StemOf(path)) == ToLower(preferStem)) s += 80;
        if (fn.rfind(L"msm_", 0) == 0 || fn.rfind(L"msz_", 0) == 0) s += 20;
        if (fn.size() > 7 && fn.compare(fn.size() - 7, 7, L"_01.dxg") == 0) s += 40;
        if (fn.size() > 7 && fn.compare(fn.size() - 7, 7, L"_01.vra") == 0) s += 40;
        if (fn.find(L"_02.") != std::wstring::npos || fn.find(L"_03.") != std::wstring::npos) s -= 25;
        if (s > bestScore) {
            bestScore = s;
            best = pr;
        }
    }
    return best;
}

static void AppendRelatedDetails(std::wstring& text, const std::wstring& unit) {
    if (unit.empty()) return;
    auto it = g_unitIndex.find(unit);
    if (it == g_unitIndex.end()) return;
    struct Row {
        std::wstring kind;
        std::wstring path;
        uint32_t size = 0;
    };
    std::vector<Row> rows;
    rows.reserve(it->second.size());
    for (auto pr : it->second) {
        if (pr.first < 0 || pr.first >= (int)g_cat.archives.size()) continue;
        const auto& ar = g_cat.archives[pr.first];
        if (pr.second < 0 || pr.second >= (int)ar.files.size()) continue;
        const auto& f = ar.files[pr.second];
        const auto path = VfsPathOf(ar, f);
        rows.push_back({FileKindOf(path), path, f.size});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        const int ka = KindOrder(a.kind);
        const int kb = KindOrder(b.kind);
        if (ka != kb) return ka < kb;
        return a.path < b.path;
    });
    text += L"\r\n==== unit ";
    text += unit;
    text += L"  ";
    text += std::to_wstring(rows.size());
    text += L" related files ====\r\n";
    std::wstring last;
    for (const auto& r : rows) {
        if (r.kind != last) {
            last = r.kind;
            text += L"[";
            text += r.kind;
            text += L"]\r\n";
        }
        wchar_t line[48];
        swprintf_s(line, L"  %8u  ", r.size);
        text += line;
        text += r.path;
        text += L"\r\n";
    }
}

static void SetPreviewMode3D(bool on) {
    if (g_d3dView) ShowWindow(g_d3dView, on ? SW_SHOW : SW_HIDE);
    if (g_img) ShowWindow(g_img, on ? SW_HIDE : SW_SHOW);
    if (g_prevFrame) {
        if (on)
            SetTimer(g_prevFrame, IDT_PREV3D, 33, nullptr);
        else
            KillTimer(g_prevFrame, IDT_PREV3D);
    }
    if (!on) Preview3D_Clear();
}

static bool Load3DPreview(int arch, int fileIndex, const std::wstring& vfsPath, std::wstring& text) {
    const std::wstring ext = FileExt(vfsPath);
    const std::wstring unit = UnitIdFromPath(vfsPath);
    std::pair<int, int> dxgRef{-1, -1};
    std::pair<int, int> vraRef{-1, -1};
    std::wstring prefer;

    if (ext == L".dxg" || ext == L".txg") {
        dxgRef = {arch, fileIndex};
        prefer = StemOf(vfsPath);
        std::wstring vraStem = prefer;
        if (vraStem.size() >= 4) {
            const std::wstring pfx = ToLower(vraStem.substr(0, 4));
            if (pfx == L"msm_") vraStem.replace(0, 3, L"MSZ");
        }
        vraRef = FindUnitByExt(unit, L".vra", vraStem);
        if (vraRef.first < 0) vraRef = FindUnitByExt(unit, L".vra", L"");
    } else if (ext == L".vra") {
        vraRef = {arch, fileIndex};
    } else if (ext == L".mrb") {
        dxgRef = FindUnitByExt(unit, L".dxg", L"");
        vraRef = FindUnitByExt(unit, L".vra", L"");
    } else {
        return false;
    }

    std::vector<uint8_t> dxgBytes, vraBytes;
    std::wstring dxgPath, vraPath;
    VraDoc vra;
    try {
        if (vraRef.first >= 0) {
            const auto& ar = g_cat.archives[vraRef.first];
            const auto& f = ar.files[(size_t)vraRef.second];
            vraBytes = fpmf::ExtractFile(ar, f, &g_cat);
            vraPath = VfsPathOf(ar, f);
            if (!vraBytes.empty())
                vra.Parse((const char*)vraBytes.data(), vraBytes.size());
        }
        if (dxgRef.first < 0 && !vra.geometry.empty())
            dxgRef = FindByVfsPath(Widen(vra.geometry));
        if (dxgRef.first < 0 && !unit.empty()) dxgRef = FindUnitByExt(unit, L".dxg", L"");
        if (dxgRef.first < 0) {
            text += L"3D: no DXG mesh found for this file.\r\n";
            return false;
        }
        {
            const auto& ar = g_cat.archives[dxgRef.first];
            const auto& f = ar.files[(size_t)dxgRef.second];
            dxgBytes = fpmf::ExtractFile(ar, f, &g_cat);
            dxgPath = VfsPathOf(ar, f);
        }
    } catch (const std::exception& ex) {
        text += L"3D extract failed: ";
        text += Widen(ex.what());
        text += L"\r\n";
        return false;
    }

    struct Held {
        std::string key;
        std::vector<uint8_t> bytes;
    };
    std::vector<Held> held;
    auto addTex = [&](const std::wstring& texPath) {
        auto ref = FindByVfsPath(texPath);
        if (ref.first < 0 && !unit.empty()) {
            const std::wstring fn = FileNameOf(ToLower(NormVfs(texPath)));
            auto uit = g_unitIndex.find(unit);
            if (uit != g_unitIndex.end()) {
                for (auto pr : uit->second) {
                    const auto& ar = g_cat.archives[pr.first];
                    const auto p = VfsPathOf(ar, ar.files[pr.second]);
                    if (ToLower(FileNameOf(p)) == fn) {
                        ref = pr;
                        break;
                    }
                }
            }
        }
        if (ref.first < 0) return;
        try {
            const auto& ar = g_cat.archives[ref.first];
            const auto& f = ar.files[(size_t)ref.second];
            Held h;
            h.key = Narrow(texPath);
            h.bytes = fpmf::ExtractFile(ar, f, &g_cat);
            if (!h.bytes.empty()) held.push_back(std::move(h));
        } catch (...) {
        }
    };
    for (const auto& m : vra.materials)
        if (!m.texture.empty()) addTex(Widen(m.texture));
    if (held.empty() && !unit.empty()) {
        auto uit = g_unitIndex.find(unit);
        if (uit != g_unitIndex.end()) {
            int bestS = -1;
            std::pair<int, int> best{-1, -1};
            for (auto pr : uit->second) {
                const auto& ar = g_cat.archives[pr.first];
                const auto p = VfsPathOf(ar, ar.files[pr.second]);
                const int s = PreviewScore(p);
                if (s > bestS) {
                    bestS = s;
                    best = pr;
                }
            }
            if (best.first >= 0 && bestS > 0) {
                const auto& ar = g_cat.archives[best.first];
                addTex(VfsPathOf(ar, ar.files[best.second]));
            }
        }
    }

    Preview3DInput in;
    in.dxg = dxgBytes.data();
    in.dxgSize = dxgBytes.size();
    if (!vraBytes.empty()) {
        in.vraText = (const char*)vraBytes.data();
        in.vraSize = vraBytes.size();
    }
    for (const auto& h : held) {
        Preview3DTex t;
        t.key = h.key;
        t.data = h.bytes.data();
        t.size = h.bytes.size();
        in.textures.push_back(t);
    }
    if (g_d3dView && !Preview3D_Init(g_d3dView)) {
        text += L"3D: Direct3D 9 device failed.\r\n";
        return false;
    }
    if (!Preview3D_Load(in)) {
        text += L"3D: DXG parse/draw failed.\r\n";
        return false;
    }

    // Motion: walk every .mrb of this unit. Quite a few of them are stubs that
    // do not parse (or carry a single key), and taking the first one used to
    // leave the model frozen in bind pose, so keep the longest one that loads.
    std::wstring animPath;
    {
        auto uit = g_unitIndex.find(unit);
        if (uit != g_unitIndex.end()) {
            std::vector<uint8_t> bestBytes;
            std::wstring bestPath;
            unsigned bestDur = 0;
            unsigned bestBones = 0;
            int tried = 0;
            for (auto pr : uit->second) {
                if (tried >= 60) break;
                if (pr.first < 0 || pr.first >= (int)g_cat.archives.size()) continue;
                const auto& ar = g_cat.archives[pr.first];
                if (pr.second < 0 || pr.second >= (int)ar.files.size()) continue;
                const auto path = VfsPathOf(ar, ar.files[pr.second]);
                if (FileExt(path) != L".mrb") continue;
                ++tried;
                try {
                    auto bytes = fpmf::ExtractFile(ar, ar.files[pr.second], &g_cat);
                    if (!Preview3D_LoadMotion(bytes.data(), bytes.size())) continue;
                    const unsigned dur = Preview3D_MotionDuration();
                    const unsigned bones = Preview3D_MotionBones();
                    // prefer a motion that drives the whole skeleton, then the
                    // longest one, so a small partial clip is not picked
                    if (bestBytes.empty() || bones > bestBones ||
                        (bones == bestBones && dur > bestDur)) {
                        bestBones = bones;
                        bestDur = dur;
                        bestPath = path;
                        bestBytes = std::move(bytes);
                    }
                } catch (...) {
                }
            }
            if (!bestBytes.empty()) {
                Preview3D_LoadMotion(bestBytes.data(), bestBytes.size());
                animPath = bestPath;
                g_heldAnim = std::move(bestBytes);
            }
        }
    }

    text += L"3D preview (game path: one DXG, LOD0 parts, motion driven).\r\n";
    if (!animPath.empty()) {
        text += L"Motion: ";
        text += animPath;
        text += L"\r\n";
    }
    text += L"Mesh: ";
    text += dxgPath;
    text += L"\r\n";
    if (!vraPath.empty()) {
        text += L"Attr: ";
        text += vraPath;
        text += L"\r\n";
    }
    if (!vra.geometry.empty()) {
        text += L"Geometry: ";
        text += Widen(vra.geometry);
        text += L"\r\n";
    }
    wchar_t st[160];
    swprintf_s(st, L"Draw: %u meshes  %u tris  %u bones  %u tex\r\n", Preview3D_MeshCount(),
               Preview3D_TriCount(), Preview3D_BoneCount(), (unsigned)held.size());
    text += st;
    swprintf_s(st, L"Rig: %u/%u parts bound to bones, motion bones=%u dur=%ums\r\n",
               Preview3D_BoundParts(), Preview3D_PartCount(), Preview3D_MotionBones(),
               Preview3D_MotionDuration());
    text += st;
    text += L"LMB orbit  wheel zoom  RMB toggle skeleton\r\n";
    SetPreviewMode3D(true);
    if (g_prevFrame) {
        wchar_t title[640];
        swprintf_s(title, L"%s   3D LOD0", FileNameOf(vfsPath).c_str());
        SetWindowTextW(g_prevFrame, title);
        if (!IsWindowVisible(g_prevFrame)) ShowPreviewFrame(true);
    }
    return true;
}

static bool ArchiveHasDat(const fpmf::Archive& ar) {
    if (fpmf::VolumeExists(ar, 0)) return true;
    return !ar.files.empty() && fpmf::VolumeExists(ar, ar.files[0].volume);
}

static int VfsAddChildTo(std::vector<VfsNode>& vfs, int parent, const std::wstring& name) {
    for (int c : vfs[parent].children)
        if (vfs[c].name == name) return c;
    VfsNode n;
    n.name = name;
    n.full = vfs[parent].full.empty() ? name : (vfs[parent].full + L"/" + name);
    vfs.push_back(std::move(n));
    const int idx = (int)vfs.size() - 1;
    vfs[parent].children.push_back(idx);
    return idx;
}

static void BuildVfsInto(const fpmf::Catalog& cat, std::vector<VfsNode>& vfs,
                         std::map<std::wstring, std::vector<std::pair<int, int>>>& unitIndex,
                         std::map<std::wstring, std::pair<int, int>>& pathIndex,
                         HWND hwnd = nullptr, unsigned seq = 0) {
    vfs.clear();
    vfs.push_back({});
    unitIndex.clear();
    pathIndex.clear();
    std::map<std::wstring, std::pair<int, int>> owned;
    const unsigned narch = (unsigned)cat.archives.size();
    for (int ai = 0; ai < (int)cat.archives.size(); ++ai) {
        const auto& ar = cat.archives[ai];
        if (!ArchiveHasDat(ar)) continue;
        for (int fi = 0; fi < (int)ar.files.size(); ++fi) {
            const auto path = VfsPathOf(ar, ar.files[fi]);
            if (path.empty()) continue;
            // Never drop a distinct file just because its VFS path collides
            // with an earlier one: disambiguate the tree key so both survive.
            std::wstring key = path;
            if (owned.find(key) != owned.end()) {
                unsigned dup = 2;
                std::wstring cand;
                do {
                    cand = path + L"#" + std::to_wstring(dup);
                    ++dup;
                } while (owned.find(cand) != owned.end());
                key = std::move(cand);
            }
            owned[key] = {ai, fi};
            pathIndex[ToLower(key)] = {ai, fi};
            const auto unit = UnitIdFromPath(path);
            if (!unit.empty()) unitIndex[unit].push_back({ai, fi});
        }
        if (hwnd && narch) {
            wchar_t buf[160];
            swprintf_s(buf, L"Indexing archives  %u / %u", (unsigned)(ai + 1), narch);
            PostProgress(hwnd, seq, 70 + 10 * (unsigned)(ai + 1) / narch, 100, buf);
        }
    }
    unsigned oi = 0;
    const unsigned nown = (unsigned)owned.size();
    for (const auto& kv : owned) {
        const std::wstring& path = kv.first;
        int node = 0;
        size_t start = 0;
        while (start < path.size()) {
            size_t sl = path.find(L'/', start);
            if (sl == std::wstring::npos) sl = path.size();
            const std::wstring part = path.substr(start, sl - start);
            if (sl == path.size()) {
                if (!part.empty()) vfs[node].files.push_back(kv.second);
                break;
            }
            if (!part.empty()) node = VfsAddChildTo(vfs, node, part);
            start = sl + 1;
        }
        ++oi;
        if (hwnd && nown && (oi % 256 == 0 || oi == nown)) {
            wchar_t buf[160];
            swprintf_s(buf, L"Building folders  %u / %u", oi, nown);
            PostProgress(hwnd, seq, 80 + 12 * oi / nown, 100, buf);
        }
    }
}

static void BuildVfs() {
    BuildVfsInto(g_cat, g_vfs, g_unitIndex, g_pathIndex);
    g_vfsSel = 0;
}

struct LoadResult {
    unsigned seq = 0;
    fpmf::Catalog cat;
    std::vector<VfsNode> vfs;
    std::map<std::wstring, std::vector<std::pair<int, int>>> unitIndex;
    std::map<std::wstring, std::pair<int, int>> pathIndex;
    std::string error;
};

static void SetBusyUi(bool busy) {
    if (g_tree) EnableWindow(g_tree, !busy);
    if (g_list) EnableWindow(g_list, !busy);
    if (g_filter) EnableWindow(g_filter, !busy);
    if (g_search) EnableWindow(g_search, !busy);
    if (busy) SetCursor(LoadCursor(nullptr, IDC_APPSTARTING));
}

static void SetLoadingUi(bool busy) {
    g_loading = busy;
    SetBusyUi(busy);
    ShowProgressBar(busy);
    if (busy) SetProgressPos(0, 100);
}

static void ApplyLoadResult(LoadResult* r) {
    if (!r) return;
    if (r->seq != g_loadSeq.load()) {
        delete r;
        return;
    }
    if (!r->error.empty()) {
        SetLoadingUi(false);
        SetStatus(L"Scan failed.");
        MessageBoxA(g_main, r->error.c_str(), "Scan failed", MB_ICONERROR);
        delete r;
        return;
    }
    ClearBitmap();
    SetPreviewMode3D(false);
    g_visible.clear();
    g_cat = std::move(r->cat);
    g_vfs = std::move(r->vfs);
    g_unitIndex = std::move(r->unitIndex);
    g_pathIndex = std::move(r->pathIndex);
    g_vfsSel = 0;
    g_arch = -1;
    delete r;
    SaveConfig();
    SetStatus(L"Filling folder tree...");
    SetProgressPos(92, 100);
    FillTree();
    SetProgressPos(100, 100);
    FillList(g_vfsSel);
    SetWindowTextW(g_preview,
                   L"Folders are the game virtual path (world/field/D00/...).\r\n"
                   L"Click a file to preview. Image files show on the left.\r\n"
                   L"Right-click to export original or PNG.");
    SetLoadingUi(false);
}

static void LoadDataThread(std::filesystem::path root, unsigned seq, HWND hwnd) {
    auto* r = new LoadResult();
    r->seq = seq;
    try {
        ScanCtx ctx{hwnd, seq};
        r->cat = fpmf::ScanDataRoot(root, OnScanProgress, &ctx);
        if (seq == g_loadSeq.load()) {
            PostProgress(hwnd, seq, 70, 100, L"Building virtual folder tree...");
            BuildVfsInto(r->cat, r->vfs, r->unitIndex, r->pathIndex, hwnd, seq);
        }
    } catch (const std::exception& ex) {
        r->error = ex.what();
    }
    if (!IsWindow(hwnd) || !PostMessageW(hwnd, WM_APP_LOAD_DONE, seq, (LPARAM)r))
        delete r;
}

static void ComputeVfsMatch() {
    g_vfsMatch.assign(g_vfs.size(), 0);
    for (int i = (int)g_vfs.size() - 1; i >= 0; --i) {
        unsigned n = 0;
        for (auto pr : g_vfs[i].files) {
            if (pr.first >= 0 && pr.first < (int)g_cat.archives.size() &&
                MatchesFilter(g_cat.archives[pr.first], g_cat.archives[pr.first].files[pr.second]))
                ++n;
        }
        for (int c : g_vfs[i].children) n += g_vfsMatch[c];
        g_vfsMatch[i] = n;
    }
}

static unsigned CountVfsMatch(int node) {
    if (node < 0 || node >= (int)g_vfsMatch.size()) return 0;
    return g_vfsMatch[node];
}

static std::pair<int, int> VisibleEntry(int row) {
    if (row < 0 || row >= (int)g_visible.size()) return {-1, -1};
    return g_visible[row];
}

static std::pair<int, int> SelectedEntry() {
    return VisibleEntry(ListView_GetNextItem(g_list, -1, LVNI_SELECTED));
}

static void ClearBitmap() {
    if (g_bmp) {
        DeleteObject(g_bmp);
        g_bmp = nullptr;
    }
    g_imgData = {};
    g_imgMean = -1;
    if (g_img) InvalidateRect(g_img, nullptr, FALSE);
}

static void InsertVfsTree(HTREEITEM parent, int node, int& firstVisible, unsigned& shown) {
    const bool filtering = !g_filterExt.empty() || !g_searchText.empty();
    const unsigned match = CountVfsMatch(node);
    if (filtering && match == 0) return;
    wchar_t buf[512];
    swprintf_s(buf, L"%s  (%u)", g_vfs[node].name.c_str(), match);
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = buf;
    ins.item.lParam = (LPARAM)node;
    HTREEITEM h = TreeView_InsertItem(g_tree, &ins);
    if (node == g_vfsSel) TreeView_SelectItem(g_tree, h);
    if (firstVisible < 0) firstVisible = node;
    ++shown;
    for (int c : g_vfs[node].children) InsertVfsTree(h, c, firstVisible, shown);
}

static void FillTree() {
    ComputeVfsMatch();
    g_suspendNotify = true;
    TreeView_DeleteAllItems(g_tree);
    int firstVisible = -1;
    unsigned shown = 0;
    if (!g_vfs.empty()) {
        for (int c : g_vfs[0].children) InsertVfsTree(TVI_ROOT, c, firstVisible, shown);
    }
    if ((g_vfsSel <= 0 || CountVfsMatch(g_vfsSel) == 0) && firstVisible >= 0)
        g_vfsSel = firstVisible;
    g_suspendNotify = false;
    wchar_t st[200];
    swprintf_s(st, L"%u folders   %u files   %s", shown,
               g_vfs.empty() ? 0 : g_vfsMatch[0], g_cat.dataRoot.wstring().c_str());
    SetStatus(st);
}

static void CollectVisibleFiles(int node, std::vector<std::pair<int, int>>& out) {
    if (node < 0 || node >= (int)g_vfs.size()) return;
    for (auto pr : g_vfs[node].files) {
        if (pr.first >= 0 && pr.first < (int)g_cat.archives.size() &&
            MatchesFilter(g_cat.archives[pr.first], g_cat.archives[pr.first].files[pr.second]))
            out.push_back(pr);
    }
    for (int c : g_vfs[node].children) CollectVisibleFiles(c, out);
}

static void FillList(int node) {
    if (g_vfs.empty()) {
        g_visible.clear();
        ListView_SetItemCountEx(g_list, 0, LVSICF_NOSCROLL);
        return;
    }
    if (node < 0 || node >= (int)g_vfs.size()) node = 0;
    g_vfsSel = node;
    g_visible.clear();
    const bool unitNode = LooksLikeUnitNode(node);
    const std::wstring unit = unitNode ? g_vfs[node].name : L"";
    if (unitNode)
        CollectRelatedFiles(unit, g_visible);
    else
        CollectVisibleFiles(node, g_visible);
    if (!g_visible.empty()) g_arch = g_visible[0].first;
    ListView_SetItemCountEx(g_list, (int)g_visible.size(), LVSICF_NOSCROLL);
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    InvalidateRect(g_list, nullptr, TRUE);
    wchar_t st[240];
    const wchar_t* folder = g_vfs[node].full.empty() ? L"(root)" : g_vfs[node].full.c_str();
    if (unitNode) {
        swprintf_s(st, L"%s   unit %s   %u related files (tex/model/anim/icon)", folder, unit.c_str(),
                   (unsigned)g_visible.size());
    } else {
        swprintf_s(st, L"%s   %u files (this folder + children)", folder, (unsigned)g_visible.size());
    }
    if (!g_idDetail.empty()) {
        std::wstring t = g_idDetail + L"   |   ";
        t += st;
        SetStatus(t.c_str());
    } else {
        SetStatus(st);
    }
    if (unitNode) {
        auto best = BestUnitPreview(unit);
        if (best.first >= 0) {
            for (int i = 0; i < (int)g_visible.size(); ++i) {
                if (g_visible[i] == best) {
                    ListView_SetItemState(g_list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(g_list, i, FALSE);
                    break;
                }
            }
            ShowPreview(best.first, best.second);
        }
    }
}

static void ApplyFilter(bool refreshTree) {
    ReadFilterUi();
    SaveConfig();
    if (IsBusy()) return;
    if (refreshTree) FillTree();
    FillList(g_vfsSel);
}

static std::wstring SanitizePathPart(std::wstring p) {
    for (auto& ch : p) {
        if (ch == L'\\') ch = L'/';
        if (ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*')
            ch = L'_';
    }
    while (p.size() >= 2 && p[0] == L'.' && p[1] == L'/') p.erase(0, 2);
    while (!p.empty() && p.front() == L'/') p.erase(p.begin());
    return p;
}

static std::filesystem::path SanitizeRel(const fpmf::Archive& ar, const fpmf::FileEntry& e) {
    return SanitizePathPart(VfsPathOf(ar, e));
}

static std::filesystem::path ExportDest(const std::filesystem::path& destRoot, int node,
                                       const fpmf::Archive& ar, const fpmf::FileEntry& e) {
    std::wstring folderName = L"root";
    std::wstring nodeFull;
    if (node >= 0 && node < (int)g_vfs.size()) {
        if (!g_vfs[node].name.empty()) folderName = g_vfs[node].name;
        nodeFull = g_vfs[node].full;
    }
    folderName = SanitizePathPart(folderName);
    const std::wstring vfs = SanitizePathPart(VfsPathOf(ar, e));
    std::wstring rel = vfs;
    const std::wstring nf = SanitizePathPart(nodeFull);
    if (!nf.empty()) {
        if (vfs == nf)
            rel = FileNameOf(vfs);
        else if (vfs.size() > nf.size() && vfs.compare(0, nf.size(), nf) == 0 && vfs[nf.size()] == L'/')
            rel = vfs.substr(nf.size() + 1);
    }
    if (rel.empty()) rel = FileNameOf(vfs);
    if (!g_exportStruct) rel = FileNameOf(rel);
    auto dest = destRoot / folderName / rel;
    if (!g_exportStruct) {
        std::error_code existsEc;
        if (std::filesystem::exists(dest, existsEc)) {
            const auto parent = dest.parent_path();
            const auto stem = dest.stem().wstring();
            const auto ext = dest.extension().wstring();
            for (int i = 2; i < 10000; ++i) {
                auto cand = parent / (stem + L"_" + std::to_wstring(i) + ext);
                if (!std::filesystem::exists(cand, existsEc)) {
                    dest = std::move(cand);
                    break;
                }
            }
        }
    }
    return dest;
}

static void ShowPreview(int arch, int fileIndex) {
    if (arch < 0 || fileIndex < 0) return;
    g_arch = arch;
    const auto& ar = g_cat.archives[arch];
    const auto& f = ar.files[fileIndex];
    ClearBitmap();
    SetPreviewMode3D(false);
    try {
        auto data = fpmf::ExtractFile(ar, f, &g_cat);
        const char* kind = fpmf::GuessKind(DisplayName(f), data.data(), data.size());
        std::wstring text;
        text += L"File: ";
        text += DisplayName(f);
        text += L"\r\nKind: ";
        text += Widen(kind);
        text += L"\r\nSize: ";
        text += std::to_wstring(data.size());
        text += L" bytes\r\nVolume: ";
        text += fpmf::VolumePath(ar, f.volume).wstring();
        text += L"\r\nPack: ";
        text += RelHed(ar);
        text += L"\r\n\r\n";

        const std::wstring vfsPath = VfsPathOf(ar, f);
        const std::wstring ext = FileExt(DisplayName(f));
        const std::wstring unit = UnitIdFromPath(vfsPath);
        const bool mesh = IsMeshAsset(vfsPath);
        const bool want3d = Wants3DPreview(vfsPath);
        if (want3d) {
            Load3DPreview(arch, fileIndex, vfsPath, text);
        } else if (!mesh && DecodeImage(data.data(), data.size(), DisplayName(f), g_imgData)) {
            g_bmp = ImageToBitmap(g_imgData);
            {
                const size_t n = (size_t)g_imgData.w * (size_t)g_imgData.h;
                unsigned long long sum = 0, mx = 0;
                for (size_t i = 0; i < n; ++i) {
                    const unsigned r = g_imgData.rgba[i * 4 + 0];
                    const unsigned g = g_imgData.rgba[i * 4 + 1];
                    const unsigned b = g_imgData.rgba[i * 4 + 2];
                    sum += r + g + b;
                    if (r > mx) mx = r;
                    if (g > mx) mx = g;
                    if (b > mx) mx = b;
                }
                g_imgMean = n ? (int)(sum / (n * 3)) : -1;
                if (n && mx == 0) text += L"\r\nNote: solid black (empty slot / placeholder)";
            }
            wchar_t info[192];
            swprintf_s(info, L"Image: %dx%d  %S\r\nRight-click the list to export original or PNG.\r\n",
                       g_imgData.w, g_imgData.h, g_imgData.format.c_str());
            text += info;
            if (g_img) InvalidateRect(g_img, nullptr, FALSE);
            if (g_prevFrame) {
                wchar_t title[640];
                swprintf_s(title, L"%s   %s", DisplayName(f).c_str(), Widen(g_imgData.format).c_str());
                SetWindowTextW(g_prevFrame, title);
                if (!IsWindowVisible(g_prevFrame)) ShowPreviewFrame(true);
            }
        } else {
            if (mesh) {
                const std::wstring mk = FileKindOf(vfsPath);
                text += L"This is 3D data, not a PNG/DDS texture.\r\n";
                if (mk == L"model" || ext == L".dxg" || ext == L".txg")
                    text += L"Kind: mesh / model\r\n";
                else if (mk == L"anim" || ext == L".mrb")
                    text += L"Kind: animation\r\n";
                else if (mk == L"attr" || ext == L".vra")
                    text += L"Kind: model attributes\r\n";
                text += L"Image preview is only for textures. Related DDS is not shown.\r\n\r\n";
                if (g_prevFrame) {
                    wchar_t title[640];
                    swprintf_s(title, L"%s   3D (no image)", DisplayName(f).c_str());
                    SetWindowTextW(g_prevFrame, title);
                }
            } else if (IsImageExt(ext)) {
                text += L"Preview failed: ";
                text += Widen(LastImageError());
                text += L"\r\n\r\n";
            }
            if (!mesh && !g_bmp && !unit.empty()) {
                const auto best = BestUnitPreview(unit);
                if (best.first >= 0 && (best.first != arch || best.second != fileIndex)) {
                    try {
                        const auto& ar2 = g_cat.archives[best.first];
                        const auto& f2 = ar2.files[(size_t)best.second];
                        auto data2 = fpmf::ExtractFile(ar2, f2, &g_cat);
                        if (DecodeImage(data2.data(), data2.size(), DisplayName(f2), g_imgData)) {
                            g_bmp = ImageToBitmap(g_imgData);
                            text += L"Related preview: ";
                            text += VfsPathOf(ar2, f2);
                            text += L"\r\n";
                            wchar_t info[192];
                            swprintf_s(info, L"Image: %dx%d  %S\r\nRight-click the list to export original or PNG.\r\n",
                                       g_imgData.w, g_imgData.h, g_imgData.format.c_str());
                            text += info;
                            if (g_img) InvalidateRect(g_img, nullptr, FALSE);
                            if (g_prevFrame) {
                                wchar_t title[640];
                                swprintf_s(title, L"unit %s   %s", unit.c_str(), VfsPathOf(ar2, f2).c_str());
                                SetWindowTextW(g_prevFrame, title);
                                if (!IsWindowVisible(g_prevFrame)) ShowPreviewFrame(true);
                            }
                        }
                    } catch (...) {
                    }
                }
            }
        }

        AppendRelatedDetails(text, unit);

        const bool textLike = strstr(kind, "CSV") || strstr(kind, "Text") || strstr(kind, "XML");
        if (textLike && !data.empty() && !Preview3D_Active()) {
            std::wstring body;
            if (IsTextTableExt(ext)) {
                const std::string bytes((const char*)data.data(), data.size());
                const textenc::Sniffed enc = textenc::ToWide(bytes, body);
                text += L"Text encoding: ";
                text += enc.name;
                text += L"\r\n";
            } else {
                data.push_back(0);
                const int n = MultiByteToWideChar(CP_ACP, 0, (char*)data.data(), -1, nullptr, 0);
                body.assign(n > 0 ? (size_t)n - 1 : 0, L'\0');
                if (n > 1) MultiByteToWideChar(CP_ACP, 0, (char*)data.data(), -1, body.data(), n);
            }
            if (body.size() > 8000) body.resize(8000);
            text += body;
        } else if (!g_bmp && !Preview3D_Active()) {
            wchar_t line[96];
            const size_t show = (std::min)(data.size(), (size_t)256);
            for (size_t i = 0; i < show; i += 16) {
                swprintf_s(line, L"%04X  ", (unsigned)i);
                text += line;
                for (size_t j = 0; j < 16 && i + j < show; ++j) {
                    swprintf_s(line, L"%02X ", data[i + j]);
                    text += line;
                }
                text += L"  ";
                for (size_t j = 0; j < 16 && i + j < show; ++j) {
                    const wchar_t c = data[i + j];
                    text += (c >= 32 && c < 127) ? c : L'.';
                }
                text += L"\r\n";
            }
            if (strstr(kind, "Ogg"))
                text += L"\r\nAudio/video: extract then play with FMOD, VLC or foobar2000.";
        }
        SetWindowTextW(g_preview, text.c_str());
    } catch (const std::exception& ex) {
        SetWindowTextW(g_preview, Widen(ex.what()).c_str());
    }
}

static bool DialogSetFolder(IFileDialog* dlg, const std::filesystem::path& start) {
    if (!dlg || start.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::exists(start, ec)) return false;
    IShellItem* folder = nullptr;
    const HRESULT hr = SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder));
    if (FAILED(hr) || !folder) return false;
    dlg->SetFolder(folder);
    folder->Release();
    return true;
}

static bool PickFolder(const wchar_t* title, std::filesystem::path& out,
                       const std::filesystem::path& start = {}) {
    out.clear();
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return false;

    dlg->SetTitle(title ? title : L"Select folder");
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts))) {
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    } else {
        dlg->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    DialogSetFolder(dlg, start);

    hr = dlg->Show(g_main);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || FAILED(hr)) {
        dlg->Release();
        return false;
    }

    IShellItem* item = nullptr;
    hr = dlg->GetResult(&item);
    dlg->Release();
    dlg = nullptr;
    if (FAILED(hr) || !item) return false;

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(hr) || !path || path[0] == 0) {
        if (path) CoTaskMemFree(path);
        return false;
    }
    out = path;
    CoTaskMemFree(path);
    if (out.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(out, ec)) {
        out.clear();
        return false;
    }
    return true;
}

static std::filesystem::path EnsureExtractDir() {
    std::error_code ec;
    if (!g_extractDir.empty() && std::filesystem::is_directory(g_extractDir, ec)) {
        return g_extractDir;
    }
    std::filesystem::path p;
    if (!PickFolder(L"Choose default export folder (saved to config.ini)", p, g_extractDir)) {
        return {};
    }
    g_extractDir = p;
    SaveConfig();
    SetDirLabel();
    return g_extractDir;
}

static void OpenDataRoot(const std::filesystem::path& root) {
    std::error_code ec;
    if (g_exporting) return;
    if (root.empty() || !std::filesystem::exists(root, ec)) return;
    const unsigned seq = ++g_loadSeq;
    SetLoadingUi(true);
    wchar_t st[512];
    swprintf_s(st, L"Scanning .hed archives...  %s", root.wstring().c_str());
    SetStatus(st);
    if (g_tree) TreeView_DeleteAllItems(g_tree);
    if (g_list) {
        g_visible.clear();
        ListView_SetItemCountEx(g_list, 0, LVSICF_NOSCROLL);
    }
    std::thread(LoadDataThread, root, seq, g_main).detach();
}

static void ExtractSelected(bool asPng) {
    if (IsBusy()) return;
    const auto sel = SelectedEntry();
    if (sel.first < 0) return;
    g_arch = sel.first;
    const int fi = sel.second;
    auto destRoot = EnsureExtractDir();
    if (destRoot.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(destRoot, ec)) return;
    const auto& ar = g_cat.archives[g_arch];
    const auto& f = ar.files[(size_t)fi];
    try {
        auto dest = ExportDest(destRoot, g_vfsSel, ar, f);
        if (asPng) {
            if (IsMeshAsset(VfsPathOf(ar, f))) {
                MessageBoxW(g_main, L"This is a 3D file, not a texture. Export original instead.",
                            L"Export PNG", MB_ICONWARNING);
                return;
            }
            dest.replace_extension(L".png");
            ImageRgba img = g_imgData;
            if (img.w <= 0) {
                auto data = fpmf::ExtractFile(ar, f, &g_cat);
                if (!DecodeImage(data.data(), data.size(), DisplayName(f), img)) {
                    MessageBoxW(g_main, L"This file is not a previewable image.", L"Export PNG",
                                MB_ICONWARNING);
                    return;
                }
            }
            std::filesystem::create_directories(dest.parent_path());
            if (!SavePngFile(dest, img)) throw std::runtime_error("WIC PNG save failed");
        } else {
            fpmf::ExtractFileTo(ar, f, dest, &g_cat);
        }
        wchar_t msg[1024];
        swprintf_s(msg, L"Saved:\n%s", dest.wstring().c_str());
        MessageBoxW(g_main, msg, L"Exported", MB_OK);
    } catch (const std::exception& ex) {
        MessageBoxA(g_main, ex.what(), "Export failed", MB_ICONERROR);
    }
}

static void CollectVfs(int node, bool rec, bool onlyVisible, std::vector<std::pair<int, int>>& out) {
    if (node < 0 || node >= (int)g_vfs.size()) return;
    for (auto pr : g_vfs[node].files) {
        if (!onlyVisible || MatchesFilter(g_cat.archives[pr.first], g_cat.archives[pr.first].files[pr.second]))
            out.push_back(pr);
    }
    if (rec)
        for (int c : g_vfs[node].children) CollectVfs(c, true, onlyVisible, out);
}

static void ExtractArchive(int node, bool onlyVisible, bool asPng) {
    if (IsBusy() || g_vfs.empty()) return;
    if (node < 0 || node >= (int)g_vfs.size()) node = g_vfsSel;
    auto destRoot = EnsureExtractDir();
    if (destRoot.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(destRoot, ec)) return;
    std::vector<std::pair<int, int>> idxs;
    CollectVfs(node, true, onlyVisible, idxs);
    if (idxs.empty()) {
        MessageBoxW(g_main, L"Nothing to export in this folder.", L"Export", MB_OK);
        return;
    }
    int ok = 0, fail = 0;
    g_exporting = true;
    SetBusyUi(true);
    ShowProgressBar(true);
    SetProgressPos(0, (unsigned)idxs.size());
    for (size_t n = 0; n < idxs.size(); ++n) {
        const auto& ar = g_cat.archives[idxs[n].first];
        const auto& f = ar.files[(size_t)idxs[n].second];
        try {
            auto dest = ExportDest(destRoot, node, ar, f);
            if (asPng) {
                if (IsMeshAsset(VfsPathOf(ar, f))) {
                    ++fail;
                    SetProgressPos((unsigned)(n + 1), (unsigned)idxs.size());
                    continue;
                }
                dest.replace_extension(L".png");
                auto data = fpmf::ExtractFile(ar, f, &g_cat);
                ImageRgba img;
                std::filesystem::create_directories(dest.parent_path());
                if (!DecodeImage(data.data(), data.size(), DisplayName(f), img) ||
                    !SavePngFile(dest, img)) {
                    ++fail;
                } else {
                    ++ok;
                }
            } else {
                fpmf::ExtractFileTo(ar, f, dest, &g_cat);
                ++ok;
            }
        } catch (...) {
            ++fail;
        }
        SetProgressPos((unsigned)(n + 1), (unsigned)idxs.size());
        if ((n % 8) == 0 || n + 1 == idxs.size()) {
            wchar_t st[192];
            const wchar_t* folder = g_vfs[node].full.empty() ? L"(root)" : g_vfs[node].full.c_str();
            swprintf_s(st, L"Exporting %s  %u / %u  (ok %d, fail %d)", folder,
                       (unsigned)(n + 1), (unsigned)idxs.size(), ok, fail);
            SetStatus(st);
            UpdateWindow(g_status);
            UpdateWindow(g_progress);
            PumpUi();
        }
    }
    g_exporting = false;
    SetBusyUi(false);
    ShowProgressBar(false);
    const auto outDir = destRoot / SanitizePathPart(g_vfs[node].name.empty() ? L"root" : g_vfs[node].name);
    wchar_t msg[256];
    swprintf_s(msg, L"Saved %d, failed %d\n%s", ok, fail, outDir.wstring().c_str());
    MessageBoxW(g_main, msg, L"Export done", MB_OK);
}

static int TreeArchAtCursor() {
    TVHITTESTINFO ht{};
    GetCursorPos(&ht.pt);
    ScreenToClient(g_tree, &ht.pt);
    TreeView_HitTest(g_tree, &ht);
    if (!ht.hItem) return -1;
    TreeView_SelectItem(g_tree, ht.hItem);
    TVITEMW it{};
    it.mask = TVIF_PARAM;
    it.hItem = ht.hItem;
    TreeView_GetItem(g_tree, &it);
    return (int)it.lParam;
}

static void ShowTreeMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_CTX_TREE_VISIBLE, L"Export this folder (filter)");
    AppendMenuW(m, MF_STRING, ID_CTX_TREE_ALL, L"Export this folder (all)");
    AppendMenuW(m, MF_STRING, ID_CTX_TREE_PNG, L"Export this folder as PNG");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CTX_SETDIR, L"Set export folder...");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
}

static void ChooseExtractDir() {
    std::filesystem::path p;
    if (!PickFolder(L"Choose default export folder", p, g_extractDir)) return;
    g_extractDir = p;
    SaveConfig();
    SetDirLabel();
}

static void ShowListMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_CTX_EXPORT, L"Export original");
    AppendMenuW(m, MF_STRING, ID_CTX_EXPORT_PNG, L"Export as PNG");
    AppendMenuW(m, MF_STRING, ID_CTX_TREE_VISIBLE, L"Export this folder (filter)");
    AppendMenuW(m, MF_STRING, ID_CTX_TREE_PNG, L"Export this folder as PNG");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_CTX_SETDIR, L"Set export folder...");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
}

static void LayoutPreview() {
    if (!g_prevFrame || !g_img || !g_preview) return;
    RECT rc{};
    GetClientRect(g_prevFrame, &rc);
    const int pad = 8;
    const int infoH = 260;
    const int imgH = (std::max)(80, (int)rc.bottom - infoH - pad * 3);
    const int cw = (std::max)(1, (int)rc.right - pad * 2);
    MoveWindow(g_img, pad, pad, cw, imgH, TRUE);
    if (g_d3dView) MoveWindow(g_d3dView, pad, pad, cw, imgH, TRUE);
    MoveWindow(g_preview, pad, pad + imgH + pad, cw, infoH, TRUE);
    if (Preview3D_Active()) Preview3D_Resize();
}

static LRESULT CALLBACK PrevFrameProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_SIZE:
        LayoutPreview();
        return 0;
    case WM_TIMER:
        if (w == IDT_PREV3D && Preview3D_Active()) Preview3D_Render();
        return 0;
    case WM_CLOSE:
        KillTimer(hwnd, IDT_PREV3D);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

static void ShowPreviewFrame(bool show) {
    if (!g_prevFrame) return;
    ShowWindow(g_prevFrame, show ? SW_SHOW : SW_HIDE);
    if (show) {
        LayoutPreview();
        InvalidateRect(g_img, nullptr, FALSE);
        if (g_d3dView && Preview3D_Active()) Preview3D_Render();
    }
}

static void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    RECT sb{};
    GetWindowRect(g_status, &sb);
    const int sbH = sb.bottom - sb.top;
    const int pad = 6;
    const int barH = 28;
    const int treeW = 300;
    const int x = pad, y = pad + barH;
    const int w = rc.right - pad * 2;
    const int h = rc.bottom - sbH - pad * 2 - barH;
    MoveWindow(g_lblFilter, pad, 8, 36, 20, TRUE);
    MoveWindow(g_filter, pad + 38, 4, 100, 220, TRUE);
    MoveWindow(g_lblSearch, pad + 148, 8, 58, 20, TRUE);
    MoveWindow(g_search, pad + 208, 4, 230, 22, TRUE);
    MoveWindow(g_btnDir, pad + 444, 4, 120, 22, TRUE);
    MoveWindow(g_chkStruct, pad + 568, 6, 120, 20, TRUE);
    MoveWindow(g_lblDir, pad + 692, 8, (std::max)(80, (int)rc.right - pad - 700), 20, TRUE);
    MoveWindow(g_tree, x, y, treeW, h, TRUE);
    MoveWindow(g_list, x + treeW + pad, y, w - treeW - pad, h, TRUE);
    int parts[2] = {(std::max)(80, (int)rc.right - 250), -1};
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    PlaceProgressBar();
}

static LRESULT CALLBACK ImgProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int mode = g_bgMode;
        if (mode == 0) {
            if (g_imgMean >= 0 && g_imgMean < 40) mode = 2;
            else if (g_imgMean > 200) mode = 1;
            else mode = 1;
        }
        const int cw = (std::max)(1, (int)rc.right);
        const int ch = (std::max)(1, (int)rc.bottom);
        HDC back = CreateCompatibleDC(hdc);
        HBITMAP backBmp = CreateCompatibleBitmap(hdc, cw, ch);
        HGDIOBJ oldBack = SelectObject(back, backBmp);
        if (mode == 3) {
            static HBRUSH checker = nullptr;
            if (!checker) {
                const int cell = 8;
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = cell * 2;
                bi.bmiHeader.biHeight = -cell * 2;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HBITMAP tile = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (bits && tile) {
                    auto* px = (COLORREF*)bits;
                    for (int y = 0; y < cell * 2; ++y)
                        for (int x = 0; x < cell * 2; ++x)
                            px[y * cell * 2 + x] =
                                (((x / cell) + (y / cell)) & 1) ? RGB(204, 204, 204) : RGB(255, 255, 255);
                    checker = CreatePatternBrush(tile);
                    DeleteObject(tile);
                }
            }
            if (checker) FillRect(back, &rc, checker);
            else FillRect(back, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        } else {
            const COLORREF col = (mode == 2) ? RGB(255, 255, 255) : RGB(0, 0, 0);
            HBRUSH br = CreateSolidBrush(col);
            FillRect(back, &rc, br);
            DeleteObject(br);
        }
        if (g_bmp && g_imgData.w > 0 && g_imgData.h > 0) {
            HDC mem = CreateCompatibleDC(back);
            HGDIOBJ old = SelectObject(mem, g_bmp);
            const int dw = cw, dh = ch;
            const int sw = g_imgData.w, sh = g_imgData.h;
            double s = (std::min)((double)dw / sw, (double)dh / sh);
            if (s > 1.0) s = 1.0;
            const int tw = (std::max)(1, (int)(sw * s));
            const int th = (std::max)(1, (int)(sh * s));
            const int ox = (dw - tw) / 2, oy = (dh - th) / 2;
            SetStretchBltMode(back, COLORONCOLOR);
            BLENDFUNCTION bf{};
            bf.BlendOp = AC_SRC_OVER;
            bf.SourceConstantAlpha = 255;
            bf.AlphaFormat = AC_SRC_ALPHA;
            if (!AlphaBlend(back, ox, oy, tw, th, mem, 0, 0, sw, sh, bf)) {
                StretchBlt(back, ox, oy, tw, th, mem, 0, 0, sw, sh, SRCCOPY);
            }
            SelectObject(mem, old);
            DeleteDC(mem);
        } else {
            SetBkMode(back, TRANSPARENT);
            SetTextColor(back, RGB(180, 180, 180));
            DrawTextW(back, L"image preview", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt(hdc, 0, 0, cw, ch, back, 0, 0, SRCCOPY);
        SelectObject(back, oldBack);
        DeleteObject(backBmp);
        DeleteDC(back);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                     ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&icc);
        HMENU men = CreateMenu();
        HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, ID_OPEN, L"Open data folder...");
        AppendMenuW(file, MF_STRING, ID_EXTRACT, L"Export selected");
        AppendMenuW(file, MF_STRING, ID_EXPORT_PNG, L"Export selected as PNG");
        AppendMenuW(file, MF_STRING, ID_EXTRACT_ALL, L"Export this folder (filter)...");
        AppendMenuW(file, MF_STRING, ID_SET_EXTRACT, L"Set export folder...");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, ID_EXIT, L"Exit");
        AppendMenuW(men, MF_POPUP, (UINT_PTR)file, L"File");
        HMENU view = CreatePopupMenu();
        AppendMenuW(view, MF_STRING, ID_VIEW_PREVIEW, L"Preview window");
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, ID_BG_AUTO, L"Background: auto");
        AppendMenuW(view, MF_STRING, ID_BG_DARK, L"Background: dark");
        AppendMenuW(view, MF_STRING, ID_BG_LIGHT, L"Background: light");
        AppendMenuW(view, MF_STRING, ID_BG_CHECK, L"Background: soft checker");
        AppendMenuW(men, MF_POPUP, (UINT_PTR)view, L"View");
        SetMenu(hwnd, men);

        g_lblFilter = CreateWindowExW(0, L"STATIC", L"Ext", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                                      (HMENU)ID_LBL_FILTER, nullptr, nullptr);
        g_filter = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                                   0, 0, 0, 0, hwnd, (HMENU)ID_FILTER, nullptr, nullptr);
        const wchar_t* exts[] = {L"(all)", L".dds", L".png", L".tga", L".ogg", L".ogv",
                                 L".mrb",  L".vra", L".dxg", L".txg", L".cfg", L".xml", L".csv",
                                 L".ttc",  L".ttf"};
        for (auto* e : exts) SendMessageW(g_filter, CB_ADDSTRING, 0, (LPARAM)e);

        g_lblSearch = CreateWindowExW(0, L"STATIC", L"ID/Name", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                      hwnd, (HMENU)ID_LBL_SEARCH, nullptr, nullptr);
        g_search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                   (HMENU)ID_SEARCH, nullptr, nullptr);
        g_btnDir = CreateWindowExW(0, L"BUTTON", L"Export folder...",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                   (HMENU)ID_BTN_DIR, nullptr, nullptr);
        g_lblDir = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 0, 0, 0,
                                   0, hwnd, (HMENU)ID_LBL_DIR, nullptr, nullptr);
        g_chkStruct = CreateWindowExW(0, L"BUTTON",
                                      L"\x521b\x5efa\x7ed3\x6784\x76ee\x5f55",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd,
                                      (HMENU)ID_CHK_STRUCT, nullptr, nullptr);
        SendMessageW(g_chkStruct, BM_SETCHECK, g_exportStruct ? BST_CHECKED : BST_UNCHECKED, 0);

        g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
                                     TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                                 0, 0, 0, 0, hwnd, (HMENU)ID_TREE, nullptr, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                                     LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                                 0, 0, 0, 0, hwnd, (HMENU)ID_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                                      LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)L"Path";
        col.cx = 420;
        ListView_InsertColumn(g_list, 0, &col);
        col.pszText = (LPWSTR)L"Kind";
        col.cx = 60;
        ListView_InsertColumn(g_list, 1, &col);
        col.pszText = (LPWSTR)L"Size";
        col.cx = 80;
        ListView_InsertColumn(g_list, 2, &col);
        col.pszText = (LPWSTR)L"Ext";
        col.cx = 60;
        ListView_InsertColumn(g_list, 3, &col);
        col.pszText = (LPWSTR)L"Pack";
        col.cx = 160;
        ListView_InsertColumn(g_list, 4, &col);

        {
            WNDCLASSW pc{};
            pc.lpfnWndProc = PrevFrameProc;
            pc.hInstance = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
            pc.lpszClassName = L"GundamPrevFrame";
            pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            pc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassW(&pc);
            int px = CW_USEDEFAULT, py = CW_USEDEFAULT, pw = 960, ph = 720;
            const auto sx = IniGet(L"ui", L"prev_x");
            if (!sx.empty()) {
                px = _wtoi(sx.c_str());
                py = _wtoi(IniGet(L"ui", L"prev_y").c_str());
                pw = (std::max)(320, _wtoi(IniGet(L"ui", L"prev_w").c_str()));
                ph = (std::max)(240, _wtoi(IniGet(L"ui", L"prev_h").c_str()));
                const RECT r = ClampToVisibleDesktop({px, py, px + pw, py + ph});
                px = (int)r.left;
                py = (int)r.top;
                pw = (int)(r.right - r.left);
                ph = (int)(r.bottom - r.top);
            }
            g_prevFrame = CreateWindowExW(0, pc.lpszClassName, L"Preview",
                                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                          px, py, pw, ph, hwnd, nullptr, pc.hInstance, nullptr);
        }
        g_img = CreateWindowExW(WS_EX_CLIENTEDGE, L"GundamImgPrev", L"", WS_CHILD | WS_VISIBLE, 0, 0,
                                0, 0, g_prevFrame, (HMENU)ID_IMGPREV, nullptr, nullptr);
        g_d3dView = CreateWindowExW(0, L"GundamD3DPrev", L"", WS_CHILD, 0, 0, 0, 0, g_prevFrame,
                                    (HMENU)ID_D3DPREV, nullptr, nullptr);
        g_preview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
                                        ES_AUTOVSCROLL,
                                    0, 0, 0, 0, g_prevFrame, (HMENU)ID_PREVIEW, nullptr, nullptr);
        LayoutPreview();
        HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0,
                                 FIXED_PITCH, L"Consolas");
        SendMessageW(g_preview, WM_SETFONT, (WPARAM)font, TRUE);
        HFONT ui = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(g_filter, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_search, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_lblFilter, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_lblSearch, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_btnDir, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_lblDir, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_chkStruct, WM_SETFONT, (WPARAM)ui, TRUE);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)ui, TRUE);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                                   (HMENU)ID_STATUS, nullptr, nullptr);
        g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, 0, 0, 0, 0, hwnd,
                                     (HMENU)ID_PROGRESS, nullptr, nullptr);
        SetWindowTheme(g_progress, L"", L"");
        SendMessageW(g_progress, PBM_SETRANGE32, 0, 100);
        SendMessageW(g_progress, PBM_SETPOS, 0, 0);

        if (!g_filterExt.empty())
            SetWindowTextW(g_filter, g_filterExt.c_str());
        else
            SetWindowTextW(g_filter, L"(all)");
        SetWindowTextW(g_search, g_searchText.c_str());
        SetDirLabel();
        return 0;
    }
    case WM_SETCURSOR:
        if (IsBusy() && (HWND)w == hwnd) {
            SetCursor(LoadCursor(nullptr, IDC_APPSTARTING));
            return TRUE;
        }
        break;
    case WM_APP_LOAD_DONE:
        ApplyLoadResult((LoadResult*)l);
        return 0;
    case WM_APP_PROGRESS:
        ApplyProgressInfo((ProgInfo*)l);
        return 0;
    case WM_SIZE:
        Layout(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_OPEN: {
            std::filesystem::path p;
            if (!PickFolder(L"Select Gundam Online data folder", p, g_cat.dataRoot)) break;
            OpenDataRoot(p);
            break;
        }
        case ID_EXTRACT:
        case ID_CTX_EXPORT:
            ExtractSelected(false);
            break;
        case ID_EXPORT_PNG:
        case ID_CTX_EXPORT_PNG:
            ExtractSelected(true);
            break;
        case ID_EXTRACT_ALL:
        case ID_CTX_TREE_VISIBLE:
            ExtractArchive(g_vfsSel, true, false);
            break;
        case ID_CTX_TREE_ALL:
            ExtractArchive(g_vfsSel, false, false);
            break;
        case ID_CTX_TREE_PNG:
            ExtractArchive(g_vfsSel, true, true);
            break;
        case ID_SET_EXTRACT:
        case ID_CTX_SETDIR:
        case ID_BTN_DIR:
            ChooseExtractDir();
            break;
        case ID_CHK_STRUCT:
            g_exportStruct = SendMessageW(g_chkStruct, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveConfig();
            break;
        case ID_VIEW_PREVIEW:
            ShowPreviewFrame(!g_prevFrame || !IsWindowVisible(g_prevFrame));
            break;
        case ID_BG_AUTO:
        case ID_BG_DARK:
        case ID_BG_LIGHT:
        case ID_BG_CHECK:
            g_bgMode = (LOWORD(w) == ID_BG_AUTO) ? 0 : (LOWORD(w) == ID_BG_DARK) ? 1
                      : (LOWORD(w) == ID_BG_LIGHT) ? 2 : 3;
            SaveConfig();
            if (g_img) InvalidateRect(g_img, nullptr, FALSE);
            break;
        case ID_EXIT:
            DestroyWindow(hwnd);
            break;
        case ID_FILTER:
            if (HIWORD(w) == CBN_SELCHANGE) {
                const int i = (int)SendMessageW(g_filter, CB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    wchar_t t[64]{};
                    SendMessageW(g_filter, CB_GETLBTEXT, i, (LPARAM)t);
                    SetWindowTextW(g_filter, t);
                }
                ApplyFilter(true);
            } else if (HIWORD(w) == CBN_EDITCHANGE) {
                SetTimer(hwnd, IDT_FILTER, 250, nullptr);
            }
            break;
        case ID_SEARCH:
            if (HIWORD(w) == EN_CHANGE) SetTimer(hwnd, IDT_FILTER, 250, nullptr);
            break;
        }
        return 0;
    case WM_TIMER:
        if (w == IDT_FILTER) {
            KillTimer(hwnd, IDT_FILTER);
            ApplyFilter(true);
        }
        return 0;
    case WM_NOTIFY: {
        const auto* nmh = (NMHDR*)l;
        if (nmh->idFrom == ID_TREE && nmh->code == TVN_SELCHANGEDW && !g_suspendNotify) {
            const auto* nmtv = (NMTREEVIEWW*)l;
            FillList((int)nmtv->itemNew.lParam);
        }
        if (nmh->idFrom == ID_TREE && nmh->code == NM_RCLICK) {
            const int arch = TreeArchAtCursor();
            if (arch >= 0) {
                FillList(arch);
                ShowTreeMenu();
            }
        }
        if (nmh->idFrom == ID_LIST && nmh->code == LVN_GETDISPINFOW) {
            auto* di = (NMLVDISPINFOW*)l;
            const auto pr = VisibleEntry(di->item.iItem);
            if (pr.first < 0) return 0;
            const auto& f = g_cat.archives[pr.first].files[(size_t)pr.second];
            const std::wstring name = DisplayName(f);
            const std::wstring vfs = VfsPathOf(g_cat.archives[pr.first], f);
            if (di->item.mask & LVIF_TEXT) {
                wchar_t tmp[64];
                const wchar_t* src = L"";
                std::wstring ext, kind, pack;
                switch (di->item.iSubItem) {
                case 0:
                    src = vfs.c_str();
                    break;
                case 1:
                    kind = FileKindOf(vfs);
                    src = kind.c_str();
                    break;
                case 2:
                    swprintf_s(tmp, L"%u", f.size);
                    src = tmp;
                    break;
                case 3:
                    ext = FileExt(name);
                    src = ext.c_str();
                    break;
                case 4:
                    pack = RelHed(g_cat.archives[pr.first]);
                    src = pack.c_str();
                    break;
                }
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, src, _TRUNCATE);
            }
        }
        if (nmh->idFrom == ID_LIST && nmh->code == LVN_ITEMCHANGED) {
            const auto* nmlv = (NMLISTVIEW*)l;
            if ((nmlv->uNewState & LVIS_SELECTED) && nmlv->iItem >= 0)
                { const auto e = VisibleEntry(nmlv->iItem); ShowPreview(e.first, e.second); }
        }
        if (nmh->idFrom == ID_LIST && nmh->code == NM_RCLICK) {
            { const auto e = SelectedEntry(); if (e.first >= 0) ShowPreview(e.first, e.second); }
            ShowListMenu();
        }
        return 0;
    }
    case WM_DESTROY:
        ++g_loadSeq;
        g_loading = false;
        g_exporting = false;
        if (g_prevFrame) KillTimer(g_prevFrame, IDT_PREV3D);
        Preview3D_Shutdown();
        SaveConfig();
        ClearBitmap();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

static int RunCli(int argc, wchar_t** argv) {
    if (argc < 3) {
        wprintf(L"GundamRes  FPMF unpacker / viewer (not Unreal)\n");
        wprintf(L"  GundamRes.exe                  GUI\n");
        wprintf(L"  GundamRes.exe list <data>      list archives\n");
        wprintf(L"  GundamRes.exe extract <data> <substr> <outdir>\n");
        wprintf(L"  GundamRes.exe id <data> <id>   list a unit's resources\n");
        wprintf(L"       <id> = 9-digit model id (rule A|B(2)|C(4)|NN(2) -> chara/0A/B/C)\n");
        wprintf(L"            | 7-digit msma id (via ms_bind.csv)\n");
        return 1;
    }
    const std::wstring cmd = argv[1];
    const std::filesystem::path data = argv[2];
    auto cat = fpmf::ScanDataRoot(data);
    if (cmd == L"list") {
        for (const auto& ar : cat.archives) {
            wprintf(L"%s  %u files  %s\n", ar.hedPath.wstring().c_str(),
                    (unsigned)ar.files.size(), ar.volume.format.c_str());
        }
        return 0;
    }
    if (cmd == L"extract" && argc >= 5) {
        const std::wstring needle = argv[3];
        const std::filesystem::path outdir = argv[4];
        int n = 0;
        for (const auto& ar : cat.archives) {
            for (const auto& f : ar.files) {
                const auto name = DisplayName(f);
                if (name.find(needle) == std::wstring::npos) continue;
                const auto dest = outdir / SanitizeRel(ar, f);
                try {
                    fpmf::ExtractFileTo(ar, f, dest, &cat);
                    wprintf(L"OK %s\n", dest.wstring().c_str());
                    ++n;
                } catch (const std::exception& ex) {
                    wprintf(L"FAIL %s  %S\n", dest.wstring().c_str(), ex.what());
                }
            }
        }
        wprintf(L"%d files\n", n);
        return 0;
    }
    if (cmd == L"id" && argc >= 4) {
        LoadMsBind();
        const std::wstring id = argv[3];
        g_scanDirs.clear();
        for (const auto& ar : cat.archives) {
            for (const auto& f : ar.files) {
                const std::wstring vfs = ToLower(VfsPathOf(ar, f));
                const size_t sl = vfs.find_last_of(L'/');
                if (sl != std::wstring::npos) g_scanDirs.insert(vfs.substr(0, sl));
            }
        }
        std::wstring detail;
        if (!ResolveIdToTargets(id, &detail)) {
            wprintf(L"%s\n", detail.c_str());
            return 2;
        }
        wprintf(L"%s\n", detail.c_str());
        int n = 0;
        for (const auto& ar : cat.archives) {
            for (const auto& f : ar.files) {
                const std::wstring vfs = VfsPathOf(ar, f);
                if (PathMatchesUnit(ToLower(vfs))) {
                    wprintf(L"%s\n", vfs.c_str());
                    ++n;
                }
            }
        }
        wprintf(L"%d files\n", n);
        return 0;
    }
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2) return RunCli(argc, argv);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_extractDir = IniGet(L"paths", L"extract");
    g_filterExt = ToLower(IniGet(L"ui", L"filter"));
    g_searchText = IniGet(L"ui", L"search");
    g_bgMode = _wtoi(IniGet(L"ui", L"bg", L"0").c_str());
    if (g_bgMode < 0 || g_bgMode > 3) g_bgMode = 0;
    g_exportStruct = IniGet(L"ui", L"export_struct", L"1") != L"0";
    if (g_filterExt == L"(all)" || g_filterExt == L"*") g_filterExt.clear();
    LoadMsBind();
    ResolveIdSearch();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"GundamResWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    WNDCLASSW ic{};
    ic.lpfnWndProc = ImgProc;
    ic.hInstance = hi;
    ic.lpszClassName = L"GundamImgPrev";
    ic.hCursor = LoadCursor(nullptr, IDC_ARROW);
    ic.hbrBackground = nullptr;
    RegisterClassW(&ic);

    WNDCLASSW dc{};
    dc.lpfnWndProc = Preview3D_WndProc;
    dc.hInstance = hi;
    dc.lpszClassName = L"GundamD3DPrev";
    dc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    dc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&dc);

    g_main = CreateWindowExW(0, wc.lpszClassName, L"Gundam Online FPMF Viewer",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                             nullptr, nullptr, hi, nullptr);
    ShowWindow(g_main, SW_SHOW);

    std::filesystem::path guess = IniGet(L"paths", L"data");
    if (guess.empty() || !std::filesystem::exists(guess)) {
        guess = std::filesystem::current_path() / L"data";
        if (!std::filesystem::exists(guess)) {
            guess = ExeDir().parent_path().parent_path() / L"data";
        }
    }
    if (std::filesystem::exists(guess)) OpenDataRoot(guess);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

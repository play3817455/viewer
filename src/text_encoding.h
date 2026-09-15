#pragma once
// Encoding sniffing for plain-text data files (csv / txt / xml / cfg).
//
// The game ships both UTF-8 tables (settings\*.csv) and UTF-16LE ones
// (settings\str_table.txt), sitting next to legacy ANSI / GBK files, so the
// preview pane must not decode everything with the system codepage.
//
// Guess order: BOM -> BOM-less UTF-16 (zero byte pattern) -> strict UTF-8 ->
// GBK(936) -> Shift-JIS(932) -> system ANSI. A file that is valid GBK *and*
// valid Shift-JIS is only switched to Shift-JIS when the GBK reading has no
// kana at all while the Shift-JIS reading clearly has some, so a Chinese file
// never flips to Japanese.

#include <windows.h>

#include <algorithm>
#include <string>

namespace textenc {

enum Kind {
    KindAnsi = 0,
    KindUtf8,
    KindUtf16Le,
    KindUtf16Be,
    KindGbk,
    KindSjis,
};

struct Sniffed {
    Kind kind = KindAnsi;
    size_t skip = 0;  // BOM bytes to drop before decoding
    const wchar_t* name = L"ANSI (ACP)";
};

inline bool ValidMultiByte(const char* s, int n, UINT cp) {
    if (n <= 0) return true;
    return MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, n, nullptr, 0) > 0;
}

inline bool HasHighByte(const std::string& s) {
    for (char c : s)
        if ((unsigned char)c >= 0x80) return true;
    return false;
}

// Only full-width kana counts: GBK bytes read as Shift-JIS turn into *half*
// width katakana, so counting those would make every Chinese file look Japanese.
inline int KanaCount(const std::wstring& w) {
    int n = 0;
    for (wchar_t c : w)
        if (c >= 0x3040 && c <= 0x30FF) ++n;
    return n;
}

inline void DecodeMultiByte(const char* p, int n, UINT cp, std::wstring& out) {
    out.clear();
    if (n <= 0) return;
    const int w = MultiByteToWideChar(cp, 0, p, n, nullptr, 0);
    if (w <= 0) return;
    out.assign((size_t)w, L'\0');
    MultiByteToWideChar(cp, 0, p, n, out.data(), w);
}

inline void DecodeUtf16(const char* p, int n, bool bigEndian, std::wstring& out) {
    out.clear();
    out.reserve((size_t)n / 2);
    for (int i = 0; i + 1 < n; i += 2) {
        const unsigned lo = (unsigned char)p[i];
        const unsigned hi = (unsigned char)p[i + 1];
        const unsigned c = bigEndian ? ((lo << 8) | hi) : (lo | (hi << 8));
        // Text files have no NUL characters; drop them instead of letting the
        // edit control cut the preview short.
        if (c) out.push_back((wchar_t)c);
    }
}

inline Sniffed Sniff(const std::string& raw) {
    Sniffed r;
    const unsigned char* b = (const unsigned char*)raw.data();
    const size_t n = raw.size();

    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
        r.kind = KindUtf8;
        r.skip = 3;
        r.name = L"UTF-8 with BOM";
        return r;
    }
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        r.kind = KindUtf16Le;
        r.skip = 2;
        r.name = L"UTF-16LE with BOM";
        return r;
    }
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        r.kind = KindUtf16Be;
        r.skip = 2;
        r.name = L"UTF-16BE with BOM";
        return r;
    }

    // BOM-less UTF-16: latin text leaves a zero byte in every other byte.
    const size_t probe = (std::min)(n, (size_t)4096);
    size_t zerosOdd = 0, zerosEven = 0, other = 0;
    for (size_t i = 0; i < probe; ++i) {
        if (b[i] == 0) {
            if (i & 1) ++zerosOdd;
            else ++zerosEven;
        } else {
            ++other;
        }
    }
    if (probe >= 16 && other >= probe / 4) {
        if (zerosOdd > probe / 4 && zerosOdd > zerosEven * 4) {
            r.kind = KindUtf16Le;
            r.name = L"UTF-16LE";
            return r;
        }
        if (zerosEven > probe / 4 && zerosEven > zerosOdd * 4) {
            r.kind = KindUtf16Be;
            r.name = L"UTF-16BE";
            return r;
        }
    }

    const char* p = raw.data();
    const int cnt = (int)n;
    if (HasHighByte(raw) && ValidMultiByte(p, cnt, CP_UTF8)) {
        r.kind = KindUtf8;
        r.name = L"UTF-8";
        return r;
    }
    if (ValidMultiByte(p, cnt, 936)) {
        if (ValidMultiByte(p, cnt, 932)) {
            std::wstring asGbk, asSjis;
            DecodeMultiByte(p, cnt, 936, asGbk);
            DecodeMultiByte(p, cnt, 932, asSjis);
            if (KanaCount(asGbk) == 0 && KanaCount(asSjis) >= 2) {
                r.kind = KindSjis;
                r.name = L"Shift-JIS (932)";
                return r;
            }
        }
        r.kind = KindGbk;
        r.name = L"GBK (936)";
        return r;
    }
    if (ValidMultiByte(p, cnt, 932)) {
        r.kind = KindSjis;
        r.name = L"Shift-JIS (932)";
        return r;
    }
    r.kind = KindAnsi;
    r.name = L"ANSI (ACP)";
    return r;
}

inline void Decode(const std::string& raw, const Sniffed& s, std::wstring& out) {
    out.clear();
    if (s.skip >= raw.size()) return;
    const char* p = raw.data() + s.skip;
    const int n = (int)(raw.size() - s.skip);
    switch (s.kind) {
    case KindUtf16Le:
        DecodeUtf16(p, n, false, out);
        return;
    case KindUtf16Be:
        DecodeUtf16(p, n, true, out);
        return;
    case KindUtf8:
        DecodeMultiByte(p, n, CP_UTF8, out);
        return;
    case KindGbk:
        DecodeMultiByte(p, n, 936, out);
        return;
    case KindSjis:
        DecodeMultiByte(p, n, 932, out);
        return;
    default:
        DecodeMultiByte(p, n, CP_ACP, out);
        return;
    }
}

// Sniff + decode in one go.
inline Sniffed ToWide(const std::string& raw, std::wstring& out) {
    const Sniffed s = Sniff(raw);
    Decode(raw, s, out);
    return s;
}

}  // namespace textenc

#include "scene_vra.h"

#include <cstdlib>
#include <sstream>

namespace {

std::string TrimAscii(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool ReadQuoted(const std::string& line, std::string& out) {
    const auto a = line.find('"');
    if (a == std::string::npos) return false;
    const auto b = line.find('"', a + 1);
    if (b == std::string::npos) return false;
    out.assign(line, a + 1, b - a - 1);
    return true;
}

bool ReadNumbers(const std::string& line, float* out, size_t count) {
    auto p = line.find(']');
    if (p == std::string::npos) p = line.find(')');
    else ++p;
    if (p == std::string::npos) return false;
    const auto end = line.find(')', p);
    if (end == std::string::npos) return false;
    for (size_t i = 0; i < count; ++i) {
        while (p < end && (line[p] == ' ' || line[p] == '\t' || line[p] == ',')) ++p;
        if (p >= end) return false;
        char* next = nullptr;
        out[i] = std::strtof(line.c_str() + p, &next);
        if (next == line.c_str() + p || next > line.c_str() + end) return false;
        p = static_cast<size_t>(next - line.c_str());
    }
    return true;
}

bool ReadBoolOrInt(const std::string& line, int& out) {
    float v = 0;
    if (!ReadNumbers(line, &v, 1)) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

bool SceneVraDoc::Parse(const char* text, size_t size) {
    objects.clear();
    if (!text || size == 0) return false;

    std::istringstream input(std::string(text, size));
    std::string line;
    SceneObject current;
    bool inObject = false;
    int depth = 0;
    while (std::getline(input, line)) {
        const std::string s = TrimAscii(line);
        if (!inObject) {
            if (s.rfind("{object", 0) != 0) continue;
            current = SceneObject{};
            inObject = true;
            depth = 1;
            continue;
        }

        if (s.find('{') != std::string::npos) ++depth;
        if (s.find('}') != std::string::npos) {
            --depth;
            if (depth <= 0) {
                if (!current.name.empty() && !current.category.empty()) objects.push_back(current);
                inObject = false;
                continue;
            }
        }

        if (s.rfind("(name string", 0) == 0) ReadQuoted(s, current.name);
        else if (s.rfind("(category string", 0) == 0) ReadQuoted(s, current.category);
        else if (s.rfind("(renderid", 0) == 0) ReadBoolOrInt(s, current.renderId);
        else if (s.rfind("(pos vector3", 0) == 0) current.hasPosition = ReadNumbers(s, &current.position.x, 3);
        else if (s.rfind("(rot vector4", 0) == 0) current.hasRotation = ReadNumbers(s, &current.rotation.x, 4);
        else if (s.rfind("(scale vector3", 0) == 0) current.hasScale = ReadNumbers(s, &current.scale.x, 3);
        else if (s.rfind("(physics", 0) == 0) { int v=0; if(ReadBoolOrInt(s,v)) current.physics = v != 0; }
        else if (s.rfind("(shadow", 0) == 0) { int v=0; if(ReadBoolOrInt(s,v)) current.shadow = v != 0; }
        else if (s.rfind("(castshadow", 0) == 0) { int v=0; if(ReadBoolOrInt(s,v)) current.castShadow = v != 0; }
    }
    return !objects.empty();
}

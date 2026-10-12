#include "../scene_vra.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    static const char sample[] =
        "{Version\n}\n{Models\n {object\n"
        "  (name string[1] \"OZ_98_wv_box01A\")\n"
        "  (category string[1] \"98\")\n"
        "  (renderid int32[1] 545)\n"
        "  (pos vector3[1] 437.923004, -116.657997, 9497.160156)\n"
        "  (rot vector4[1] 0.000000, 0.000000, 0.000000, 1.000000)\n"
        "  (scale vector3[1] 0.667891, 1.126870, 1.000000)\n"
        "  (physics int8[1] 1)\n"
        "  (shadow int8[1] 0)\n"
        "  (castshadow int8[1] 0)\n"
        " }\n}\n";
    SceneVraDoc doc;
    if (!doc.Parse(sample, sizeof(sample) - 1) || doc.objects.size() != 1) return 1;
    const auto& o = doc.objects.front();
    if (o.name != "OZ_98_wv_box01A" || o.category != "98" || o.renderId != 545) return 2;
    if (!o.hasPosition || std::fabs(o.position.x - 437.923004f) > 0.001f ||
        std::fabs(o.position.y + 116.657997f) > 0.001f || std::fabs(o.position.z - 9497.160156f) > 0.001f) return 3;
    if (!o.hasRotation || !o.hasScale || !o.physics || o.shadow || o.castShadow) return 4;
    std::cout << "scene VRA placement record parsed\n";
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        std::string actual((std::istreambuf_iterator<char>(f)), {});
        SceneVraDoc field;
        if (actual.empty() || !field.Parse(actual.data(), actual.size())) return 5;
        std::cout << "actual field VRA objects: " << field.objects.size() << "\n";
    }
    return 0;
}

cmake_minimum_required(VERSION 3.16)
project(GundamRes LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DXTEX ${CMAKE_CURRENT_SOURCE_DIR}/third_party/DirectXTex/DirectXTex)
add_library(DirectXTex STATIC
    ${DXTEX}/BC.cpp
    ${DXTEX}/BC4BC5.cpp
    ${DXTEX}/BC6HBC7.cpp
    ${DXTEX}/DirectXTexCompress.cpp
    ${DXTEX}/DirectXTexConvert.cpp
    ${DXTEX}/DirectXTexDDS.cpp
    ${DXTEX}/DirectXTexFlipRotate.cpp
    ${DXTEX}/DirectXTexHDR.cpp
    ${DXTEX}/DirectXTexImage.cpp
    ${DXTEX}/DirectXTexMipmaps.cpp
    ${DXTEX}/DirectXTexMisc.cpp
    ${DXTEX}/DirectXTexNormalMaps.cpp
    ${DXTEX}/DirectXTexPMAlpha.cpp
    ${DXTEX}/DirectXTexResize.cpp
    ${DXTEX}/DirectXTexTGA.cpp
    ${DXTEX}/DirectXTexUtil.cpp
    ${DXTEX}/DirectXTexWIC.cpp
)
target_include_directories(DirectXTex PUBLIC ${DXTEX})
target_compile_definitions(DirectXTex PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)
if (MSVC)
    target_compile_options(DirectXTex PRIVATE /utf-8 /W3 /wd4061 /wd4365 /wd4668 /wd5219 /bigobj)
endif()

add_executable(GundamRes
    src/main.cpp
    src/fpmf.cpp
    src/image_io.cpp
    src/dxg.cpp
    src/anim.cpp
    src/vra_text.cpp
    src/preview3d.cpp
)
target_include_directories(GundamRes PRIVATE src)
target_compile_definitions(GundamRes PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(GundamRes PRIVATE DirectXTex ole32 windowscodecs d3d9)
if (MSVC)
    target_compile_options(GundamRes PRIVATE /utf-8 /W3)
endif()

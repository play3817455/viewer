cmake_minimum_required(VERSION 3.16)
project(GundamMachineConsole LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DXTEX ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/DirectXTex/DirectXTex)
add_library(DirectXTex STATIC
    ${DXTEX}/BC.cpp ${DXTEX}/BC4BC5.cpp ${DXTEX}/BC6HBC7.cpp
    ${DXTEX}/DirectXTexCompress.cpp ${DXTEX}/DirectXTexConvert.cpp
    ${DXTEX}/DirectXTexDDS.cpp ${DXTEX}/DirectXTexFlipRotate.cpp
    ${DXTEX}/DirectXTexHDR.cpp ${DXTEX}/DirectXTexImage.cpp
    ${DXTEX}/DirectXTexMipmaps.cpp ${DXTEX}/DirectXTexMisc.cpp
    ${DXTEX}/DirectXTexNormalMaps.cpp ${DXTEX}/DirectXTexPMAlpha.cpp
    ${DXTEX}/DirectXTexResize.cpp ${DXTEX}/DirectXTexTGA.cpp
    ${DXTEX}/DirectXTexUtil.cpp ${DXTEX}/DirectXTexWIC.cpp)
target_include_directories(DirectXTex PUBLIC ${DXTEX})
target_compile_definitions(DirectXTex PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)

add_executable(GundamMachineConsole WIN32
    main.cpp
    scene_vra.cpp
    ../src/image_io.cpp ../src/dxg.cpp ../src/anim.cpp
    ../src/vra_text.cpp engine/preview3d.cpp)
target_include_directories(GundamMachineConsole PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine ../src)
target_compile_definitions(GundamMachineConsole PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(GundamMachineConsole PRIVATE DirectXTex ole32 windowscodecs d3d9 comctl32 shell32 msimg32)
if(MSVC)
    target_compile_options(DirectXTex PRIVATE /utf-8 /W3 /bigobj)
    target_compile_options(GundamMachineConsole PRIVATE /utf-8 /W3)
endif()

foreach(binding ms_bind.csv ms_weapons.csv item_names.csv)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../${binding})
        add_custom_command(TARGET GundamMachineConsole POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../${binding}
            $<TARGET_FILE_DIR:GundamMachineConsole>/${binding})
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(scene_vra_test tests/scene_vra_test.cpp scene_vra.cpp)
    target_include_directories(scene_vra_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(scene_vra_test PRIVATE /utf-8 /W3)
    endif()
    add_test(NAME scene_vra_placements COMMAND scene_vra_test)
endif()

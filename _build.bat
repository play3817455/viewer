@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d F:\9ymsgo\9you\viewer
cl /std:c++17 /EHsc /I src _bindtest.cpp src\dxg.cpp src\anim.cpp /Fe_bindtest.exe > _cl.txt 2>&1
echo === BUILD === >> _cl.txt
if exist _bindtest.exe (
  _bindtest.exe "F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\unpackdat\chara\01\01\0101\model\MSM_0101_0101_01.dxg" "F:\9ymsgo\9you\机动战士敢达Online\GundamOnline\unpackdat\chara\01\04\anim\MSA_0104_00003.mrb" >> _cl.txt 2>&1
) else (
  echo NO_EXE >> _cl.txt
)
echo === END === >> _cl.txt

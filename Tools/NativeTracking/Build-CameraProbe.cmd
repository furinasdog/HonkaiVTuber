@echo off
setlocal
call "D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "OPENCV=D:\Epic Games\UE_5.8\Engine\Plugins\Runtime\OpenCV"
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DNOMINMAX /I"%OPENCV%\Source\ThirdParty\OpenCV\include" "%~dp0CameraProbe.cpp" /Fo"%~dp0build\CameraProbe.obj" /Fe"%~dp0bin\HonkaiCameraProbe.exe" /link /LIBPATH:"%OPENCV%\Source\ThirdParty\OpenCV\lib\Win64" opencv_world455.lib
if errorlevel 1 exit /b %ERRORLEVEL%
copy /y "%OPENCV%\Binaries\ThirdParty\Win64\opencv_world455.dll" "%~dp0bin\opencv_world455.dll" >nul

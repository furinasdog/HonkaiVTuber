@echo off
setlocal
set "NATIVE_ROOT=%~dp0"
if not defined VCToolsInstallDir (
  if exist "D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else (
    echo Run from an x64 Visual Studio Developer Command Prompt.
    exit /b 1
  )
)
if not exist "%NATIVE_ROOT%bin" mkdir "%NATIVE_ROOT%bin"
if not exist "%NATIVE_ROOT%build" mkdir "%NATIVE_ROOT%build"
set "OPENCV=D:\Epic Games\UE_5.8\Engine\Plugins\Runtime\OpenCV"
if defined HONKAI_OPENCV_ROOT set "OPENCV=%HONKAI_OPENCV_ROOT%"
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"%NATIVE_ROOT%ThirdParty\MediaPipe\include" /I"%OPENCV%\Source\ThirdParty\OpenCV\include" "%NATIVE_ROOT%NativeTracking.cpp" "%NATIVE_ROOT%AppContainer.cpp" /Fo"%NATIVE_ROOT%build\\" /Fe"%NATIVE_ROOT%bin\HonkaiNativeTracking.exe" /link mfplat.lib mfreadwrite.lib mfuuid.lib mf.lib ole32.lib userenv.lib advapi32.lib OneCoreUAP.lib /LIBPATH:"%OPENCV%\Source\ThirdParty\OpenCV\lib\Win64" opencv_world455.lib delayimp.lib /DELAYLOAD:opencv_world455.dll
exit /b %ERRORLEVEL%

@echo off
setlocal
set "PROBE_ROOT=%~dp0"
set "SPOUT_SDK=%PROBE_ROOT%..\..\Plugins\Spout2_DX12\Source\ThirdParty"
if not defined VCToolsInstallDir (
  if exist "D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    call "D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
  ) else (
    echo Run from an x64 Visual Studio Developer Command Prompt.
    exit /b 1
  )
)
if not exist "%PROBE_ROOT%bin" mkdir "%PROBE_ROOT%bin"
if not exist "%PROBE_ROOT%build" mkdir "%PROBE_ROOT%build"
cl /nologo /std:c++17 /EHsc /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DSPOUT_IMPORT_DLL /I"%SPOUT_SDK%\include" "%PROBE_ROOT%SpoutProbe.cpp" /Fo"%PROBE_ROOT%build\SpoutProbe.obj" /Fe"%PROBE_ROOT%bin\HonkaiSpoutProbe.exe" /link /LIBPATH:"%SPOUT_SDK%\lib\Win64" SpoutDX.lib Spout.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b %ERRORLEVEL%
copy /Y "%SPOUT_SDK%\bin\Win64\SpoutDX.dll" "%PROBE_ROOT%bin\SpoutDX.dll" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
copy /Y "%SPOUT_SDK%\bin\Win64\Spout.dll" "%PROBE_ROOT%bin\Spout.dll" >nul
exit /b %ERRORLEVEL%

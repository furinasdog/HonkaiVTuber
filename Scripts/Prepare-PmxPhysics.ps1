param([string]$MsvcVersion = '14.44', [string]$BulletRoot = '', [ValidateRange(1, 32)][int]$Parallel = 3)
$ErrorActionPreference = 'Stop'
if ($MsvcVersion -notmatch '^\d+\.\d+$') { throw 'Invalid MSVC version.' }
$projectRoot = Split-Path $PSScriptRoot -Parent
$umtRoot = Join-Path $projectRoot 'Tools/UMTNativePlugin'
$buildRoot = Join-Path $umtRoot 'build-static-ue144'
if (!(Test-Path -LiteralPath (Join-Path $umtRoot 'CMakeLists.txt'))) {
    throw 'Initialize submodules first: git submodule update --init --recursive'
}
$cmake = (Get-Command cmake -ErrorAction Stop).Source
Get-Command ninja -ErrorAction Stop | Out-Null
$ctest = (Get-Command ctest -ErrorAction Stop).Source
Get-Command git -ErrorAction Stop | Out-Null
$savedEnvironment = @{}
try {
    $currentCompiler = Get-Command cl -ErrorAction SilentlyContinue
    $hasRuntimeHeaders = @($env:INCLUDE -split ';' | Where-Object {
        $_ -and (Test-Path -LiteralPath (Join-Path $_ 'math.h'))
    }).Count -gt 0
    if (!$currentCompiler -or !$hasRuntimeHeaders -or $currentCompiler.Source -notmatch ('[\\/]' + [regex]::Escape($MsvcVersion) + '\.')) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (!(Test-Path -LiteralPath $vswhere)) { throw 'Run from an x64 Visual Studio Developer PowerShell.' }
        $vsRoots = & $vswhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $vsRoot = $vsRoots | Where-Object {
            $toolsets = Join-Path $_ 'VC/Tools/MSVC'
            (Test-Path -LiteralPath $toolsets) -and @(
                Get-ChildItem -LiteralPath $toolsets -Directory | Where-Object { $_.Name -like ($MsvcVersion + '.*') }
            ).Count -gt 0
        } | Select-Object -First 1
        if (!$vsRoot) { throw ('No installed Visual Studio instance contains MSVC ' + $MsvcVersion + '.') }
        $vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
        if (!(Test-Path -LiteralPath $vcvars)) { throw 'Installed x64 Visual C++ environment was not found.' }
        $environmentLines = & $env:ComSpec /d /c ('call "' + $vcvars + '" -vcvars_ver=' + $MsvcVersion + ' >nul && set')
        if ($LASTEXITCODE -ne 0) { throw 'Unable to initialize the installed MSVC toolset.' }
        foreach ($line in $environmentLines) {
            if ($line -match '^(PATH|INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCToolsVersion|WindowsSdkDir|WindowsSDKVersion)=(.*)$') {
                $name = $Matches[1]
                $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
                [Environment]::SetEnvironmentVariable($name, $Matches[2], 'Process')
            }
        }
    }
    $configureArgs = @('-S', $umtRoot, '-B', $buildRoot, '-G', 'Ninja', '-DUMT_STATIC=ON',
        '-DUMT_BUILD_TESTS=ON', '-DUMT_REPRODUCIBLE_MSVC=ON',
        ('-DCMAKE_CXX_COMPILER=' + (Get-Command cl -ErrorAction Stop).Source),
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
        '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc',
        '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release', '-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1',
        '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7')
    # Clear the old cached 2.75 override. The UMT build manages/verifies its pinned 3.07 checkout.
    if ($BulletRoot) {
        $configureArgs += '-DBulletRoot=' + (Resolve-Path -LiteralPath $BulletRoot).Path
    }
    else {
        $configureArgs += '-DBulletRoot='
    }
    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'UMT configure failed; existing build/cache files were preserved.' }
    & $cmake --build $buildRoot --config Release --parallel $Parallel
    if ($LASTEXITCODE -ne 0) { throw 'UMT build failed.' }
    & $ctest --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'UMT native regression failed; do not use this build for UE.' }
    $library = Join-Path $buildRoot 'bundled/UMTNativePlugin.lib'
    if (!(Test-Path -LiteralPath $library)) { throw 'Bundled UMT archive was not produced.' }
    Get-FileHash -LiteralPath $library -Algorithm SHA256
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}

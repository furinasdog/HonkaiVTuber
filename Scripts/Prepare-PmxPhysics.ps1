param([string]$MsvcVersion = '14.44')
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
$savedEnvironment = @{}
try {
    if (!(Get-Command cl -ErrorAction SilentlyContinue)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (!(Test-Path -LiteralPath $vswhere)) { throw 'Run from an x64 Visual Studio Developer PowerShell.' }
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
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
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
        '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7')
    foreach ($relative in @('build-static-ue144/_deps/bullet275-src', 'build-static/_deps/bullet275-src', 'build/_deps/bullet275-src')) {
        $bulletRoot = Join-Path $umtRoot $relative
        if (Test-Path -LiteralPath (Join-Path $bulletRoot 'CMakeLists.txt')) {
            $configureArgs += '-DBulletRoot=' + $bulletRoot
            break
        }
    }
    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'UMT configure failed; existing build/cache files were preserved.' }
    & $cmake --build $buildRoot --config Release --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'UMT build failed.' }
    $library = Join-Path $buildRoot 'bundled/UMTNativePlugin.lib'
    if (!(Test-Path -LiteralPath $library)) { throw 'Bundled UMT archive was not produced.' }
    Get-FileHash -LiteralPath $library -Algorithm SHA256
}
finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}

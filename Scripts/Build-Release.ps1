#requires -Version 7.2
[CmdletBinding()]
param(
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$RequireDlss,
    [switch]$CheckOnly,
    [string]$LicensedEpicSplashBmp,
    [string]$EpicSplashApproval
)
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'Win64 builds require Windows.' }
$root = Split-Path $PSScriptRoot -Parent
$uproject = Join-Path $root 'HonkaiRendering.uproject'
function Require-File([string]$Path, [string]$Hint) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing dependency: $Path. $Hint" }
}
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed (exit $LASTEXITCODE)." }
}
$git = (Get-Command git -ErrorAction Stop).Source
$project = Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json
$association = $project.EngineAssociation
if (!$EngineRoot) {
    $EngineRoot = Get-ItemPropertyValue -LiteralPath 'HKCU:/Software/Epic Games/Unreal Engine/Builds' -Name $association -ErrorAction SilentlyContinue
    if (!$EngineRoot) {
        $installFile = Join-Path $env:ProgramData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
        if (Test-Path -LiteralPath $installFile) {
            $EngineRoot = (Get-Content -LiteralPath $installFile -Raw | ConvertFrom-Json).InstallationList |
                Where-Object AppName -eq "UE_$association" | Select-Object -First 1 -ExpandProperty InstallLocation
        }
    }
    if (!$EngineRoot) {
        $EngineRoot = Get-ItemPropertyValue -LiteralPath "HKLM:/SOFTWARE/EpicGames/Unreal Engine/$association" -Name InstalledDirectory -ErrorAction SilentlyContinue
    }
}
if (!$EngineRoot) { throw "Unreal Engine $association not found. Install it or set UE_ROOT / -EngineRoot." }
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$uat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
$ubt = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$engineVersionFile = Join-Path $EngineRoot 'Engine/Build/Build.version'
foreach ($file in @($uat, $ubt, $editor, $engineVersionFile)) { Require-File $file 'Supply a complete UE installation.' }
$engine = Get-Content -LiteralPath $engineVersionFile -Raw | ConvertFrom-Json
$engineVersion = "$($engine.MajorVersion).$($engine.MinorVersion).$($engine.PatchVersion)"
if ($association -match '^\d+\.\d+$' -and "$($engine.MajorVersion).$($engine.MinorVersion)" -ne $association) {
    throw "Expected UE $association; found $engineVersion at $EngineRoot."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
Require-File $vswhere 'Install Visual Studio Build Tools with Desktop development with C++.'
$vsRoots = @(Invoke-Checked $vswhere @('-all','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'))
$toolset = foreach ($vs in $vsRoots) {
    Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory -Filter '14.44.*' -ErrorAction SilentlyContinue
}
$toolset = $toolset | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (!$toolset) { throw 'Install the x64 MSVC 14.44 toolset used by the pinned PMX physics build.' }
$vcVersion = $toolset.Name
$cl = Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
$kits = Get-ItemPropertyValue -LiteralPath 'HKLM:/SOFTWARE/WOW6432Node/Microsoft/Windows Kits/Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue
if (!$kits) { $kits = Get-ItemPropertyValue -LiteralPath 'HKLM:/SOFTWARE/Microsoft/Windows Kits/Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue }
if (!$kits) { throw 'Windows SDK is missing. Install Windows SDK through Visual Studio Installer.' }
$sdks = Get-ChildItem -LiteralPath (Join-Path $kits 'Include') -Directory -ErrorAction SilentlyContinue | Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and
    (Test-Path -LiteralPath (Join-Path $kits "Lib/$($_.Name)/um/x64/kernel32.lib")) -and
    (Test-Path -LiteralPath (Join-Path $kits "Lib/$($_.Name)/ucrt/x64/ucrt.lib")) -and
    (Test-Path -LiteralPath (Join-Path $kits "bin/$($_.Name)/x64/rc.exe"))
} | Sort-Object { [version]$_.Name } -Descending
$sdk = $sdks | Select-Object -First 1
if (!$sdk) { throw 'No complete Windows SDK for x64 was found.' }
$preferredSdk = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Config/Windows/Windows_SDK.json') -Raw | ConvertFrom-Json
$candidate = $sdks | Where-Object Name -eq $preferredSdk.MainVersion | Select-Object -First 1
if ($candidate) { $sdk = $candidate }

$submodules = @(Invoke-Checked $git @('-C',$root,'submodule','status','--recursive'))
if (!$submodules.Count -or ($submodules | Where-Object { $_ -match '^[-+U]' })) {
    throw 'Submodules must be initialized at their pinned revisions: git submodule update --init --recursive'
}
$tracked = @(Invoke-Checked $git @('-C',$root,'ls-files','--recurse-submodules'))
$forbidden = '(?i)\.(pmx|pmd|vrm|vrma|fbx|glb|gltf|blend|vmd|mp4|mkv|avi|webm|mov|7z|rar|zip)$'
if ($tracked -match $forbidden) { throw 'Model, archive or video found in tracked files; release stopped.' }
foreach ($dep in @(
    'Tools/UMTNativePlugin/build-static-ue144/bundled/UMTNativePlugin.lib',
    'Plugins/VRM4U/ThirdParty/assimp/lib/x64/Release/assimp-vc141-mt.lib',
    'Plugins/VRM4U/ThirdParty/assimp/bin/x64/assimp-vc141-mt.dll',
    'Plugins/HonkaiMotionCapture/Binaries/ThirdParty/Win64/libmediapipe.dll',
    'Plugins/HonkaiMotionCapture/Resources/Models/face_landmarker.task',
    'Plugins/HonkaiMotionCapture/Resources/Models/hand_landmarker.task',
    'Plugins/HonkaiMotionCapture/Resources/Models/pose_landmarker_lite.task'
)) { Require-File (Join-Path $root $dep) 'See doc/BUILD.md for dependency preparation.' }

$runtimeManifest = Join-Path $root 'Plugins/HonkaiMotionCapture/Scripts/runtime-artifacts.json'
foreach ($artifact in (Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json).artifacts) {
    $path = Join-Path $root "Plugins/HonkaiMotionCapture/$($artifact.target)"
    if ((Get-FileHash -LiteralPath $path).Hash -ne $artifact.sha256) { throw "Invalid MediaPipe artifact hash: $($artifact.target)" }
}
$nvidia = Join-Path $root 'Plugins/NVIDIA'
$hasDlss = Test-Path -LiteralPath (Join-Path $nvidia 'DLSS/DLSS.uplugin') -PathType Leaf
if ($RequireDlss -and !$hasDlss) { throw 'Official NVIDIA DLSS plugin is missing. See doc/BUILD.md.' }
$licenses = [ordered]@{
    'Project-MIT.txt' = 'LICENSE'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'HonkaiToonRenderer-MIT.txt' = 'Plugins/HonkaiToonRenderer/LICENSE'
    'HonkaiToonRenderer-Notices.md' = 'Plugins/HonkaiToonRenderer/THIRD_PARTY_NOTICES.md'
    'VRM4U-MIT.txt' = 'Plugins/VRM4U/LICENSE'
    'Assimp-BSD.txt' = 'Plugins/VRM4U/LICENSE_assimp'
    'UnityMMDTools-MIT.txt' = 'Plugins/VRM4U/LICENSE_UMT'
    'UMTNativePlugin-MIT.txt' = 'Tools/UMTNativePlugin/LICENSE'
    'Bullet-zlib.txt' = 'Tools/UMTNativePlugin/ThirdParty/Bullet-LICENSE.txt'
    'Spout2-SDK-BSD.txt' = 'doc/Spout2-SDK-LICENSE.txt'
    'Spout2-MIT.txt' = 'Plugins/Spout2_DX12/LICENSE'
    'HonkaiMotionCapture-MIT.txt' = 'Plugins/HonkaiMotionCapture/LICENSE'
    'HonkaiMotionCapture-Notices.md' = 'Plugins/HonkaiMotionCapture/THIRD_PARTY_NOTICES.md'
    'OpenCV.txt' = 'Plugins/HonkaiMotionCapture/Source/ThirdParty/OpenCV/LICENSE'
    'OpenCV-Contrib.txt' = 'Plugins/HonkaiMotionCapture/Source/ThirdParty/OpenCV/OpenCVContrib_License.txt'
    'MediaPipe.txt' = 'Plugins/HonkaiMotionCapture/Source/ThirdParty/MediaPipe/LICENSE'
}
if ($hasDlss) {
    foreach ($dll in 'nvngx_dlss.dll','nvngx_dlssd.dll') {
        $path = Join-Path $nvidia "DLSS/Binaries/ThirdParty/Win64/$dll"
        Require-File $path 'Reinstall the official DLSS SDK.'
        if ((Get-AuthenticodeSignature -LiteralPath $path).Status -ne 'Valid') { throw "Invalid NVIDIA runtime signature: $dll" }
    }
    $licenses['NVIDIA-RTX-SDK-EULA.pdf'] = 'Plugins/NVIDIA/NVIDIA RTX SDK EULA.pdf'
    $licenses['NVIDIA-NGX-License.txt'] = 'Plugins/NVIDIA/DLSS/Source/ThirdParty/NGX/LICENSE.txt'
}
foreach ($file in $licenses.Values) { Require-File (Join-Path $root $file) 'Restore the distribution notices.' }
if ([bool]$LicensedEpicSplashBmp -ne [bool]$EpicSplashApproval) { throw 'Supply both the official splash BMP and the local Epic permission record, or neither.' }
if ($LicensedEpicSplashBmp) {
    Require-File $LicensedEpicSplashBmp 'Supply the approved Epic splash image.'
    Require-File $EpicSplashApproval 'Supply the Epic authorization record for this product and use.'
    $stream = [IO.File]::OpenRead((Resolve-Path -LiteralPath $LicensedEpicSplashBmp).Path)
    try {
        if ($stream.ReadByte() -ne 66 -or $stream.ReadByte() -ne 77) { throw 'Epic splash must be an approved original BMP.' }
    } finally { $stream.Dispose() }
}
if (Get-ChildItem -LiteralPath (Join-Path $root 'Content/Splash') -File -ErrorAction SilentlyContinue) {
    throw 'Project Content/Splash contains unmanaged files. Use the licensed splash arguments.'
}
Write-Host "UE $engineVersion : $EngineRoot"
Write-Host "MSVC $vcVersion : $cl"
Write-Host "Windows SDK $($sdk.Name) : $kits"
Write-Host "Official NVIDIA DLSS: $hasDlss"
if ($CheckOnly) { return }

$source = Invoke-Checked $git @('-C',$root,'rev-parse','HEAD')
$dirty = [bool](Invoke-Checked $git @('-C',$root,'status','--porcelain','--untracked-files=no','--ignore-submodules=dirty'))
if ($dirty) { throw 'Commit tracked source changes before making a release; the embedded version must identify the build source.' }
$projectSettings = Get-Content -LiteralPath (Join-Path $root 'Config/DefaultGame.ini') -Raw
$product = [regex]::Match($projectSettings,'(?m)^ProjectName=([A-Za-z0-9]+)\s*$').Groups[1].Value
$base = [regex]::Match($projectSettings,'(?m)^ProjectVersion=(\d+\.\d+\.\d+)\s*$').Groups[1].Value
if (!$product -or !$base) { throw 'Define ProjectName and a numeric ProjectVersion in DefaultGame.ini.' }
$version = "$base+$($source.Substring(0,12))"
$instant = [datetime]::UtcNow
$timestamp = $instant.ToString('yyyy-MM-dd HH:mm:ss ''UTC''')
$authors = @(Invoke-Checked $git @('-C',$root,'-c','i18n.logOutputEncoding=UTF-8','log','--format=%aN','HEAD') | Sort-Object -Unique)
if (!$authors.Count) { throw 'No source contributors were found.' }
function To-CppText([string]$Text) { 'TEXT(' + (ConvertTo-Json -InputObject $Text -Compress) + ')' }
$id = "$product-$base-$($source.Substring(0,12))-$($instant.ToString('yyyyMMddTHHmmssZ'))"
$temp = Join-Path $root "Saved/ReleaseBuilds/$id"
$dist = Join-Path $root "dist/$id"
if ((Test-Path -LiteralPath $temp) -or (Test-Path -LiteralPath $dist)) { throw "Build ID already exists: $id" }
$stamp = Join-Path $root 'Intermediate/HonkaiBuildInfo'
New-Item -ItemType Directory -Force -Path $stamp,$temp | Out-Null
$stampFile = Join-Path $stamp 'HonkaiBuildStamp.h'
@(
    '#pragma once'
    ('#define HONKAI_BUILD_STAMP ' + (To-CppText $version))
    ('#define HONKAI_BUILD_TIME ' + (To-CppText $timestamp))
    ('#define HONKAI_BUILD_CONTRIBUTORS ' + (To-CppText ($authors -join "`n")))
) | Set-Content -LiteralPath $stampFile -Encoding utf8NoBOM

$disabled = '-DisablePlugins=EditorToolset,ToolsetRegistry,MCPClientToolset,ModelContextProtocol,Terminal,RenderDocPlugin'
$baseArgs = @('BuildCookRun',"-project=$uproject",'-clientconfig=Shipping','-platform=Win64','-noP4','-unattended','-utf8output')
$cook = Join-Path $temp 'Cooked'
$archive = Join-Path $temp 'Archive'
try {
    Invoke-Checked $ubt @('HonkaiRenderingEditor','Win64','Development',"-Project=$uproject",$disabled,"-CompilerVersion=$vcVersion",'-MaxParallelActions=8')
    $records = (Join-Path $temp 'ProjectRecords.json').Replace('\','/')
    $cookOptions = "-SkipZenStore -ddc=InstalledNoZenLocalFallback $disabled -ini:Engine:[/Script/PythonScriptPlugin.PythonScriptPluginSettings]:bRunPipInstallOnStartup=False -ini:Engine:[ConsoleVariables]:r.Editor.ProjectEditorRecordsFile=`"$records`""
    Invoke-Checked $uat ($baseArgs + @('-build','-cook','-NoCompileEditor',"-CookOutputDir=$cook/Windows","-UbtArgs=$disabled -CompilerVersion=$vcVersion -MaxParallelActions=8","-AdditionalCookerOptions=$cookOptions"))

    $registry = Join-Path $cook 'Windows/HonkaiRendering/Metadata/DevelopmentAssetRegistry.bin'
    Require-File $registry 'Cook did not produce a development asset registry.'
    $dump = Join-Path $temp 'Registry'
    $user = Join-Path $temp 'EditorUser'
    New-Item -ItemType Directory -Path $dump,$user | Out-Null
    Invoke-Checked $editor @($uproject,'-run=DumpAssetRegistry',"-Path=$registry","-OutDir=$dump",'-All','-NullRHI','-unattended','-nop4','-nosplash',"-UserDir=$user/",$disabled,'-ini:Engine:[/Script/PythonScriptPlugin.PythonScriptPluginSettings]:bRunPipInstallOnStartup=False',"-ini:Engine:[ConsoleVariables]:r.Editor.ProjectEditorRecordsFile=$records","-abslog=$temp/Registry.log")
    $classPage = Get-ChildItem -LiteralPath $dump -Filter 'Page_*.txt' |
        Where-Object { (Get-Content -LiteralPath $_.FullName -TotalCount 1) -eq '--- Begin CachedAssetsByClass ---' } | Select-Object -First 1
    if (!$classPage) { throw 'Could not find the asset registry class inventory.' }
    $kind = $null
    $assets = @(foreach ($line in Get-Content -LiteralPath $classPage.FullName) {
        if ($line -match '^	(/\S+) : \d+ item\(s\)$') { $kind = $Matches[1] }
        elseif ($line -match '^		(/\S+)$') { "$kind`t$($Matches[1])" }
        elseif ($line -like '--- End CachedAssetsByClass*') { break }
    })
    $allowed = Get-Content -LiteralPath (Join-Path $root 'doc/CookedAssets.allowlist.tsv')
    if (!$assets.Count -or $assets.Count -ne $allowed.Count -or (Compare-Object $allowed $assets -CaseSensitive)) {
        throw "Cooked inventory differs from the reviewed allowlist. Inspect $dump before allowing new assets."
    }
    Invoke-Checked $uat ($baseArgs + @('-skipbuild','-skipcook','-stage','-pak','-iostore','-compressed','-archive','-nodebuginfo',"-CookOutputDir=$cook","-archivedirectory=$archive"))
    $output = Join-Path $archive 'Windows'
    Require-File (Join-Path $output 'HonkaiRendering/Binaries/Win64/HonkaiRendering-Win64-Shipping.exe') 'Staging did not create the Shipping binary.'
    Require-File (Join-Path $output 'HonkaiRendering.exe') 'Staging did not create the Windows launcher.'
    foreach ($f in Get-ChildItem -LiteralPath $output -Recurse -File) {
        $rel = [IO.Path]::GetRelativePath($output,$f.FullName)
        if ($rel -match $forbidden -or $rel -match '(?i)(^|[\\/])Saved[\\/]|\.(pdb|uasset|png|jpg|jpeg|tga|psd|dds|bmp|cpp|h)$') {
            throw "Unexpected loose release file: $rel"
        }
    }
    if ($LicensedEpicSplashBmp) {
        $splashDir = Join-Path $output 'HonkaiRendering/Content/Splash'
        New-Item -ItemType Directory -Force $splashDir | Out-Null
        Copy-Item -LiteralPath $LicensedEpicSplashBmp -Destination (Join-Path $splashDir 'Splash.bmp')
    }
    $licenseDir = Join-Path $output 'Licenses'
    New-Item -ItemType Directory -Path $licenseDir | Out-Null
    foreach ($license in $licenses.GetEnumerator()) {
        Copy-Item -LiteralPath (Join-Path $root $license.Value) -Destination (Join-Path $licenseDir $license.Key)
    }
    [ordered]@{ product=$product; version=$version; source=$source; built_utc=$instant.ToString('o');
        engine=$engineVersion; msvc=$vcVersion; windows_sdk=$sdk.Name; contributors=$authors; submodules=$submodules;
        nvidia_dlss=$hasDlss; licensed_epic_splash=[bool]$LicensedEpicSplashBmp;
        cooked_assets=$assets.Count; cooked_registry_sha256=(Get-FileHash -LiteralPath $registry).Hash
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'BuildManifest.json') -Encoding utf8NoBOM
    Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName | ForEach-Object {
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant(), [IO.Path]::GetRelativePath($output,$_.FullName).Replace('\','/')
    } | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding utf8NoBOM
    if (!(Test-Path -LiteralPath (Join-Path $root 'dist'))) { New-Item -ItemType Directory -Path (Join-Path $root 'dist') | Out-Null }
    # Publish the complete directory only after the audit; never replace a prior release.
    [IO.Directory]::Move($output, $dist)
    Write-Host "Release: $dist/HonkaiRendering.exe"
}
finally {
    Remove-Item -LiteralPath $stampFile -ErrorAction SilentlyContinue
}

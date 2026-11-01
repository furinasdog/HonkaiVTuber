param(
    [string]$LocalMediaPipeDll = 'D:/Program_Files/Lib/site-packages/mediapipe/tasks/c/libmediapipe.dll',
    [string]$UEOpenCvRoot = 'D:/Epic Games/UE_5.8/Engine/Plugins/Runtime/OpenCV',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$nativeRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Tools/NativeTracking'))
$binaryRoot = Join-Path $nativeRoot 'bin'
$downloadRoot = Join-Path $nativeRoot 'downloads'
New-Item -ItemType Directory -Force -Path $binaryRoot, $downloadRoot, (Join-Path $binaryRoot 'Models') | Out-Null
$dllHash = 'AA8E6C1B618C30CD3A6AD584DEE1B2F2C99C3F3025D683BADA36E1566D9092B7'
$wheelHash = 'B08F001CF3C3CD0D88D9ED68F3368DC8A4913F568281A93117F083115AA672BA'
$modelHash = '64184E229B263107BC2B804C6625DB1341FF2BB731874B0BCC2FE6544E0BC9FF'
$handModelHash = 'FBC2A30080C3C557093B5DDFC334698132EB341044CCEE322CCF8BCF3607CDE1'
$poseModelHash = '59929E1D1EE95287735DDD833B19CF4AC46D29BC7AFDDBBF6753C459690D574A'
function Test-Hash([string]$Path, [string]$Hash) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and ((Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash -eq $Hash)
}
function Get-PinnedArtifact([string]$Url, [string]$Path, [string]$Hash) {
    if (Test-Hash $Path $Hash) { return }
    $partial = $Path + '.download'
    Invoke-WebRequest -Uri $Url -OutFile $partial
    if (-not (Test-Hash $partial $Hash)) { throw "SHA256 mismatch; existing artifact preserved: $Path" }
    Move-Item -LiteralPath $partial -Destination $Path -Force
}
$dll = Join-Path $binaryRoot 'libmediapipe.dll'
if (-not (Test-Hash $dll $dllHash)) {
    if (Test-Hash $LocalMediaPipeDll $dllHash) {
        Copy-Item -LiteralPath $LocalMediaPipeDll -Destination $dll
    } else {
        $wheel = Join-Path $downloadRoot 'mediapipe-0.10.35-py3-none-win_amd64.whl'
        Get-PinnedArtifact 'https://files.pythonhosted.org/packages/5b/f6/763477e9aeed98accc984ed6ee3f11a21a0c5fd1d1c6586b8d07067748ff/mediapipe-0.10.35-py3-none-win_amd64.whl' $wheel $wheelHash
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [System.IO.Compression.ZipFile]::OpenRead($wheel)
        try {
            $entry = $archive.GetEntry('mediapipe/tasks/c/libmediapipe.dll')
            if (-not $entry) { throw 'The official wheel is missing libmediapipe.dll.' }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dll + '.download', $true)
            if (-not (Test-Hash ($dll + '.download') $dllHash)) { throw 'Unexpected native DLL hash.' }
            Move-Item -LiteralPath ($dll + '.download') -Destination $dll -Force
        } finally { $archive.Dispose() }
    }
}
Get-PinnedArtifact 'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task' (Join-Path $binaryRoot 'Models/face_landmarker.task') $modelHash
Get-PinnedArtifact 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task' (Join-Path $binaryRoot 'Models/hand_landmarker.task') $handModelHash
Get-PinnedArtifact 'https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task' (Join-Path $binaryRoot 'Models/pose_landmarker_lite.task') $poseModelHash
$opencvSource = Join-Path $UEOpenCvRoot 'Binaries/ThirdParty/Win64/opencv_world455.dll'
$opencvTarget = Join-Path $binaryRoot 'opencv_world455.dll'
if (-not (Test-Path -LiteralPath $opencvSource -PathType Leaf)) { throw "Existing UE OpenCV 4.5.5 SDK missing: $opencvSource" }
if (-not (Test-Hash $opencvTarget (Get-FileHash -Algorithm SHA256 -LiteralPath $opencvSource).Hash)) {
    Copy-Item -LiteralPath $opencvSource -Destination $opencvTarget
}
if (-not $SkipBuild) {
    $env:HONKAI_OPENCV_ROOT = [System.IO.Path]::GetFullPath($UEOpenCvRoot)
    & (Join-Path $nativeRoot 'Build.cmd')
    if ($LASTEXITCODE -ne 0) { throw 'NativeTracking C++ build failed.' }
    & (Join-Path $binaryRoot 'HonkaiNativeTracking.exe') --self-test
    if ($LASTEXITCODE -ne 0) { throw 'NativeTracking self-test failed.' }
}
Write-Output "Prepared pinned native MediaPipe runtime: $binaryRoot"
Write-Output 'Inference runs inside a native AppContainer with no network capability. No Python runtime is used.'

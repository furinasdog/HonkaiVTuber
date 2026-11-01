param(
    [string]$Archive,
    [string]$Model,
    [string]$UnrealEditor = 'D:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$projectFile = Join-Path $projectRoot 'HonkaiRendering.uproject'
if (!(Test-Path -LiteralPath $UnrealEditor)) { throw 'UnrealEditor.exe not found. Pass -UnrealEditor with the installed UE 5.8 path.' }
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-HonkaiRendering.dll'))) { throw 'Build the HonkaiRenderingEditor target before starting the preview.' }
$running = Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*HonkaiVTuber*' }
if ($running) { Write-Output 'HonkaiVTuber is already running.'; exit 0 }
$previewArgs = @(('"' + $projectFile + '"'), '/Game/MmdViewer/L_Viewer', '-game', '-windowed', '-ResX=1440', '-ResY=900', '-nosound', '-nop4', '-DisablePlugins=EditorToolset,ToolsetRegistry,MCPClientToolset,ModelContextProtocol')
if ($Archive) {
    $archivePath = (Resolve-Path -LiteralPath $Archive).Path
    if ([IO.Path]::GetExtension($archivePath) -ine '.zip') { throw 'Archive must be a ZIP file.' }
    $previewArgs += '-MmdArchive="' + $archivePath + '"'
}
if ($Model) { if ($Model.Contains('"')) { throw 'Invalid model filename.' }; $previewArgs += '-MmdModel="' + $Model + '"' }
# The user is explicitly opening an interactive preview.
Start-Process -FilePath $UnrealEditor -ArgumentList $previewArgs -WindowStyle Normal -WorkingDirectory $projectRoot

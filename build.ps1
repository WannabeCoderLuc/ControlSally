# Builds src\SahelanControl\bin\Release\SahelanControl.dll with the installed Visual Studio (found by vswhere).
#   powershell -ExecutionPolicy Bypass -File build.ps1 [-Sdk 10.0.26100.0] [-SdkDir D:\MySDK] [-Package]
# -Sdk     Windows SDK version (default: newest installed under SdkDir)
# -SdkDir  Windows Kits\10 folder (default: the standard install); for a portable or custom SDK
# -Package also assembles release\ : plugins\ (DLL + default config), driver\, docs.
param([string]$Sdk = "", [string]$SdkDir = "${env:ProgramFiles(x86)}\Windows Kits\10", [switch]$Package)
$ErrorActionPreference = "Stop"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 not found (vswhere.exe missing)" }
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found; install 'Desktop development with C++'" }
if (-not (Test-Path "$SdkDir\Include")) { throw "Windows SDK not found in '$SdkDir' (install it or pass -SdkDir)" }
if (-not $Sdk) {
    $Sdk = (Get-ChildItem "$SdkDir\Include" -Directory | Where-Object Name -match '^10\.' | Sort-Object { [version]$_.Name } | Select-Object -Last 1).Name
}
Write-Host "Windows SDK $Sdk from $SdkDir"
$sdkArg = $SdkDir.TrimEnd('\') + '\\'   # trailing double backslash keeps the closing quote from being escaped
& $msbuild "$PSScriptRoot\src\SahelanControl\SahelanControl.vcxproj" /p:Configuration=Release /p:Platform=x64 `
    /p:WindowsTargetPlatformVersion=$Sdk /p:WindowsSdkDir_10="$sdkArg" /p:WindowsSdkDir="$sdkArg" `
    /p:UniversalCRTSdkDir_10="$sdkArg" /p:UniversalCRTSdkDir="$sdkArg" /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "build failed" }
$dll = "$PSScriptRoot\src\SahelanControl\bin\Release\SahelanControl.dll"
Write-Host "built:" $dll (Get-FileHash $dll -Algorithm SHA1).Hash

if ($Package) {
    $r = "$PSScriptRoot\release"
    if (Test-Path $r) { Remove-Item -Recurse -Force $r }
    New-Item -ItemType Directory -Force "$r\plugins", "$r\driver", "$r\docs" | Out-Null
    Copy-Item $dll, "$PSScriptRoot\dist\plugins\SahelanControl_gate.txt" "$r\plugins\"
    Copy-Item "$PSScriptRoot\driver\*.py" "$r\driver\"
    Copy-Item "$PSScriptRoot\docs\*.md", "$PSScriptRoot\README.md", "$PSScriptRoot\DEPENDENCIES.md", `
        "$PSScriptRoot\LICENSE", "$PSScriptRoot\THIRD_PARTY_NOTICES.md" "$r\docs\"
    Write-Host "release folder:" $r "(add ControlSally.mgsv before zipping)"
}

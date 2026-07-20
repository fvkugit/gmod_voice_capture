$ErrorActionPreference = "Stop"
$nativeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

& (Join-Path $PSScriptRoot "bootstrap_windows.ps1")
if ($LASTEXITCODE -ne 0) { throw "Dependency bootstrap failed" }

$premake = Join-Path $nativeRoot ".tools\premake-5.0.0-beta8\premake5.exe"
$gmCommon = Join-Path $nativeRoot ".deps\garrysmod_common"
$opus = Join-Path $nativeRoot ".deps\opus-1.6.1"
$opusBuild = Join-Path $nativeRoot ".deps\opus-build-vs2022bt"

Push-Location $nativeRoot
try {
    & $premake --gmcommon=$gmCommon --opus=$opus --opus-build=$opusBuild vs2022
    if ($LASTEXITCODE -ne 0) { throw "Premake generation failed" }

    $solution = Join-Path $nativeRoot "projects\windows\vs2022\gmod_voice_capture.sln"
    $msbuild = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    & $msbuild $solution /m /p:Configuration=ReleaseWithSymbols /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "Module build failed" }

    $builtModule = Join-Path $nativeRoot "projects\windows\vs2022\x86_64\ReleaseWithSymbols\gmsv_gmod_voice_capture_win64.dll"
    if (-not (Test-Path -LiteralPath $builtModule)) {
        throw "Expected module was not produced: $builtModule"
    }

    $artifactDir = Join-Path $nativeRoot "bin\windows-x64"
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    Copy-Item -LiteralPath $builtModule -Destination (Join-Path $artifactDir "gmsv_gmod_voice_capture_win64.dll") -Force
    Write-Host "Built: $(Join-Path $artifactDir 'gmsv_gmod_voice_capture_win64.dll')"
} finally {
    Pop-Location
}

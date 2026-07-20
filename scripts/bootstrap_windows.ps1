param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$nativeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$depsRoot = Join-Path $nativeRoot ".deps"
$toolsRoot = Join-Path $nativeRoot ".tools"
$gmCommonRoot = Join-Path $depsRoot "garrysmod_common"
$opusRoot = Join-Path $depsRoot "opus-1.6.1"
$opusBuild = Join-Path $depsRoot "opus-build-vs2022bt"
$premakeRoot = Join-Path $toolsRoot "premake-5.0.0-beta8"
$premakeExe = Join-Path $premakeRoot "premake5.exe"
$gmCommonCommit = "f77a18d86f780a59ea30e4237016b05b790d4b70"

New-Item -ItemType Directory -Force -Path $depsRoot, $toolsRoot | Out-Null

if ($Force -or -not (Test-Path -LiteralPath $premakeExe)) {
    $premakeZip = Join-Path $toolsRoot "premake.zip"
    New-Item -ItemType Directory -Force -Path $premakeRoot | Out-Null
    Invoke-WebRequest -UseBasicParsing `
        -Uri "https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip" `
        -OutFile $premakeZip
    Expand-Archive -LiteralPath $premakeZip -DestinationPath $premakeRoot -Force
}

if ((Test-Path -LiteralPath (Join-Path $gmCommonRoot ".git")) -and
    -not (Test-Path -LiteralPath (Join-Path $gmCommonRoot "sourcesdk-minimal\.git"))) {
    $resolvedDeps = (Resolve-Path $depsRoot).Path
    $resolvedGmCommon = (Resolve-Path $gmCommonRoot).Path
    if (-not $resolvedGmCommon.StartsWith($resolvedDeps + [IO.Path]::DirectorySeparatorChar)) {
        throw "Refusing to remove unexpected dependency path: $resolvedGmCommon"
    }
    Remove-Item -LiteralPath $resolvedGmCommon -Recurse -Force
}

if (-not (Test-Path -LiteralPath (Join-Path $gmCommonRoot ".git"))) {
    git clone --recursive https://github.com/danielga/garrysmod_common.git $gmCommonRoot
}
git -C $gmCommonRoot fetch --depth 1 origin $gmCommonCommit
git -C $gmCommonRoot checkout --detach $gmCommonCommit
git -C $gmCommonRoot submodule update --init --recursive
# A cancelled/partial bootstrap can leave a submodule registered at the right
# commit while its worktree still contains staged deletions. Restore only the
# generated dependency checkouts so Premake always sees their source files.
git -C $gmCommonRoot submodule foreach --recursive git reset --hard HEAD
if ($LASTEXITCODE -ne 0) { throw "garrysmod_common submodule repair failed" }

if ($Force -or -not (Test-Path -LiteralPath (Join-Path $opusRoot "CMakeLists.txt"))) {
    $opusArchive = Join-Path $depsRoot "opus-1.6.1.tar.gz"
    Invoke-WebRequest -UseBasicParsing `
        -Uri "https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz" `
        -OutFile $opusArchive
    tar -xzf $opusArchive -C $depsRoot
}

$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path -LiteralPath $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
}

$vsBuildTools = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
& $cmake -S $opusRoot -B $opusBuild -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_GENERATOR_INSTANCE=$vsBuildTools" `
    -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF `
    -DOPUS_STATIC_RUNTIME=ON
if ($LASTEXITCODE -ne 0) { throw "Opus CMake generation failed" }

& $cmake --build $opusBuild --config Release
if ($LASTEXITCODE -ne 0) { throw "Opus build failed" }

Write-Host "Dependencies ready."
Write-Host "Premake: $premakeExe"
Write-Host "garrysmod_common: $gmCommonRoot"
Write-Host "Opus: $opusRoot"
Write-Host "Opus build: $opusBuild"

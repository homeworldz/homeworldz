[CmdletBinding()]
param([switch]$Test)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio Installer (vswhere.exe) was not found." }
$installation = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)[0]
if (-not $installation) { throw "Visual Studio C++ tools were not found." }
$vs = $installation.installationPath
$major = [int]($installation.installationVersion.Split('.')[0])
$generator = switch ($major) { 18 { "Visual Studio 18 2026" } 17 { "Visual Studio 17 2022" } default { throw "Unsupported Visual Studio major version $major." } }
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$toolchain = Join-Path $vs "VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
$build = Join-Path $root "build\windows-vcpkg"
$configure = @("-S", $root, "-B", $build, "-G", $generator, "-A", "x64")
if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
    $configure += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}
& $cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) {
    & (Join-Path (Split-Path $cmake) "ctest.exe") --test-dir $build -C Debug --output-on-failure
    exit $LASTEXITCODE
}

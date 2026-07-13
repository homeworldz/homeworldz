[CmdletBinding()]
param(
    [string]$DataPath,
    [switch]$SkipToolchainCheck
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

if (-not $DataPath) {
    $DataPath = Join-Path $repositoryRoot "var\region"
}

if (-not $SkipToolchainCheck) {
    $missing = @()
    foreach ($commandName in @("cmake")) {
        if (-not (Get-Command $commandName -ErrorAction SilentlyContinue)) {
            $missing += $commandName
        }
    }

    $compilerFound = @("cl", "clang++", "g++") | Where-Object {
        Get-Command $_ -ErrorAction SilentlyContinue
    } | Select-Object -First 1
    if (-not $compilerFound) {
        $missing += "a C++20 compiler (Visual Studio, clang++, or g++)"
    }

    if ($missing.Count -gt 0) {
        throw "Missing region build prerequisites: $($missing -join ', '). Use -SkipToolchainCheck only when preparing data for a prebuilt region server."
    }
}

$resolvedDataPath = [IO.Path]::GetFullPath($DataPath)
foreach ($directory in @("assets", "scene", "logs")) {
    New-Item -ItemType Directory -Path (Join-Path $resolvedDataPath $directory) -Force | Out-Null
}

Write-Host "HomeWorldz region directories are ready at '$resolvedDataPath'."
Write-Host "The region bootstrap does not require PostgreSQL; central state belongs to the grid service."
Write-Host "As region configuration and registration are implemented, this script will initialize them here."


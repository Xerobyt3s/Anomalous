<#
.SYNOPSIS
    Configure, build and test Anomalous with Ninja and MSVC.
.EXAMPLE
    .\tools\build.ps1 -Test
    .\tools\build.ps1 -Config Release
    .\tools\build.ps1 -Target anomalous_tests -Test
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'Debug',

    [string]$Target,

    [switch]$Test,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

$presetMap = @{
    'Debug'          = 'ninja-debug'
    'Release'        = 'ninja-release'
    'RelWithDebInfo' = 'ninja-relwithdebinfo'
}
$preset = $presetMap[$Config]

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere. Is Visual Studio installed?"
    }

    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio installation with the C++ toolset was found.'
    }

    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found at $vcvars"
    }

    Write-Host "Importing MSVC environment from $vsPath" -ForegroundColor DarkGray

    & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'vcvars64 ran but cl.exe is still not on PATH.'
    }
}

$buildDir = Join-Path $repoRoot "build\$preset"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir" -ForegroundColor DarkGray
    Remove-Item -Recurse -Force $buildDir
}

Push-Location $repoRoot
try {
    cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

    $buildArgs = @('--build', '--preset', $preset)
    if ($Target) { $buildArgs += @('--target', $Target) }

    cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }

    if ($Test) {
        ctest --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "ctest failed ($LASTEXITCODE)" }
    }

    Write-Host "`n$Config build OK -> $buildDir\bin" -ForegroundColor Green
}
finally {
    Pop-Location
}

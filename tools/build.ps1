# C-Otter -- configura o ambiente MSVC e roda o build.
#
#   tools\build.ps1                 # configura + compila (win-debug)
#   tools\build.ps1 -Preset win-release
#   tools\build.ps1 -Test
param(
    [string]$Preset = "win-debug",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# --- Localiza o VS com toolchain C++ e importa as variaveis de ambiente -------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe nao encontrado" }

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) { throw "Visual Studio com toolchain C++ nao encontrado" }

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat nao encontrado em $vsPath" }

# vcvars64.bat so' exporta para cmd.exe; capturamos e replicamos no PowerShell.
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
    }
}

Write-Host "MSVC: $(Split-Path -Leaf $vsPath) | preset: $Preset" -ForegroundColor Cyan

$buildDir = Join-Path $root "build\$Preset"
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure falhou" }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "build falhou" }

    if ($Test) {
        ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw "testes falharam" }
    }
    Write-Host "OK" -ForegroundColor Green
}
finally {
    Pop-Location
}

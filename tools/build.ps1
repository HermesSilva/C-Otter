# C-Otter -- build com o ambiente do MSVC carregado.
#
# Por que existe: chamar `cmake --build` de um shell qualquer falha com
# "Cannot open include file: 'cstdint'" -- o cl.exe e' encontrado pelo PATH do
# CMakeCache, mas as variaveis INCLUDE/LIB/LIBPATH so' existem depois do
# vcvars64.bat. Este script importa essas variaveis para a sessao atual.
#
#   tools\build.ps1                 # alvo padrao, build\win-release
#   tools\build.ps1 -Target tests
#   tools\build.ps1 -BuildDir build\win-debug

param(
    [string] $BuildDir = 'build\win-release',
    [string] $Target   = ''
)

$ErrorActionPreference = 'Stop'

function Import-VsEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }   # ja' carregado

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere nao encontrado em $vswhere -- Visual Studio instalado?"
    }

    $root = & $vswhere -latest -property installationPath
    if (-not $root) { throw 'Nenhuma instalacao do Visual Studio encontrada.' }

    $vcvars = Join-Path $root 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat nao encontrado em $vcvars" }

    # O .bat so' altera o ambiente do cmd.exe filho. `set` despeja o resultado,
    # que reimportamos aqui -- e' o unico jeito de herdar INCLUDE e LIB.
    & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -EA SilentlyContinue
        }
    }
    Write-Host "MSVC: $root" -ForegroundColor DarkGray
}

Import-VsEnvironment

$repo = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $repo $BuildDir

if (-not (Test-Path $dir)) {
    throw "Diretorio de build ausente: $dir -- rodar cmake --preset primeiro."
}

$args = @('--build', $dir)
if ($Target) { $args += @('--target', $Target) }

& cmake @args
exit $LASTEXITCODE

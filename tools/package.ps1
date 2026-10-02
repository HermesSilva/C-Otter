# C-Otter -- monta o pacote Windows a partir de um build pronto.
#
#   tools\package.ps1 -Version 0.1.0                  # build\win-release
#   tools\package.ps1 -Version 0.1.0 -BuildDir build\win-debug
#
# Gera build\dist\c-otter-<versao>-windows-x64.zip e o .sha256 ao lado.
#
# O pacote e' a pasta do produto portatil (ADR 0020): o executavel e, ao lado
# dele, so' o que nao esta' embutido -- licencas e a pasta de idiomas. Os
# dados (.C-Otter\, .script\) nascem ali na primeira execucao. Nao ha' DLL:
# a runtime da MSVC entra estatica (/MT).

param(
    [Parameter(Mandatory)] [string] $Version,
    [string] $BuildDir = 'build\win-release'
)

$ErrorActionPreference = 'Stop'

$repo  = Split-Path -Parent $PSScriptRoot
$name  = "c-otter-$Version-windows-x64"
$dist  = Join-Path $repo 'build\dist'
$stage = Join-Path $dist $name
$zip   = Join-Path $dist "$name.zip"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip)   { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force (Join-Path $stage 'lang') | Out-Null

Copy-Item (Join-Path $repo "$BuildDir\bin\c-otter.exe") $stage
Copy-Item (Join-Path $repo 'docs\LICENSES.md')          $stage
# Os icones embutidos sao os originais do DBeaver (Apache 2.0): o aviso viaja
# com o binario.
Copy-Item (Join-Path $repo 'assets\icons\dbeaver\NOTICE') $stage
Copy-Item (Join-Path $repo 'lang\*') (Join-Path $stage 'lang')

Compress-Archive -Path $stage -DestinationPath $zip

# Mesmo formato do sha256sum, para conferir com `sha256sum -c` nos dois sistemas.
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip`n")

Write-Output $zip

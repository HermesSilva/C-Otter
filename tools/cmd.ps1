# Envia comandos ao C-Otter em execucao, pelo arquivo que ele le quando
# iniciado com OTTER_COMMAND_FILE (ver docs/ELEMENTS.md, "Variaveis de
# inspecao").
#
#   $env:OTTER_COMMAND_FILE = "$env:TEMP\otter-commands.txt"   # ANTES de abrir
#   tools\cmd.ps1 "select 0 2", "extend 2 3", "Copy"
#
# Cada argumento e' uma linha: o ROTULO do comando em ingles, ou
# "select <linha> <coluna>" / "extend <linha> <coluna>" para a celula da grade.
# PositionalBinding desligado: sem isso o primeiro comando iria parar em
# -WaitMs, que e' o primeiro parametro "livre".
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Commands,
    [int]$WaitMs = 600
)

$path = $env:OTTER_COMMAND_FILE
if (-not $path) { $path = Join-Path $env:TEMP "otter-commands.txt" }

# Espera o anterior ser consumido: o programa apaga o arquivo ao le-lo.
$deadline = (Get-Date).AddSeconds(5)
while ((Test-Path $path) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }

[System.IO.File]::WriteAllLines($path, $Commands)

$deadline = (Get-Date).AddSeconds(5)
while ((Test-Path $path) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
if (Test-Path $path) { throw "o C-Otter nao consumiu os comandos (OTTER_COMMAND_FILE definido ao abrir?)" }

Start-Sleep -Milliseconds $WaitMs

# Captura a MESMA tela no DBeaver e no C-Otter, lado a lado, para conferir a
# diretiva 12 (elemento de tela parecido com o do DBeaver, e validado).
#
# Por que existe: a diretiva manda comparar com o DBeaver rodando, e não com a
# lembrança de como ele é. Sem uma forma barata de pôr as duas telas lado a
# lado, a comparação deixa de acontecer e a diretiva vira boa intenção.
#
#   tools\compare_ui.ps1 -Out dialogo.png
#
# Deixe as duas janelas abertas na tela a comparar ANTES de rodar: o script
# não navega por elas, só fotografa. Navegar por cliques erra o alvo com
# frequência, e uma foto de tela errada é pior que nenhuma.
#
# CUIDADO: a foto sai como a janela está. A do DBeaver costuma ter um script
# aberto, com nomes de tabela e dados de produção -- e a imagem pode acabar
# anexada a um chat ou a um commit. Feche o que for sensível antes, e apague
# a imagem depois de conferir.
param(
    [string] $Out = "$env:TEMP\compare.png",
    [string] $DbeaverProcess = "dbeaver",
    [string] $OtterProcess   = "c-otter"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class CmpUi {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

function Get-WindowShot([string] $ProcessName) {
    $proc = Get-Process $ProcessName -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $proc) { return $null }

    [void][CmpUi]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 900

    $rect = New-Object CmpUi+RECT
    [void][CmpUi]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)

    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { return $null }

    $bitmap = New-Object System.Drawing.Bitmap($w, $h)
    $gfx = [System.Drawing.Graphics]::FromImage($bitmap)
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                        (New-Object System.Drawing.Size($w, $h)))
    $gfx.Dispose()
    return $bitmap
}

$left  = Get-WindowShot $DbeaverProcess
$right = Get-WindowShot $OtterProcess

if (-not $left)  { throw "$DbeaverProcess nao esta rodando com janela visivel" }
if (-not $right) { $left.Dispose(); throw "$OtterProcess nao esta rodando com janela visivel" }

# Lado a lado, alinhados pelo TOPO e na mesma altura: comparar a posicao de um
# campo exige que as duas imagens tenham a mesma referencia vertical.
$height = [Math]::Max($left.Height, $right.Height)
$width  = $left.Width + $right.Width + 16

$canvas = New-Object System.Drawing.Bitmap($width, $height)
$gfx = [System.Drawing.Graphics]::FromImage($canvas)
$gfx.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$gfx.DrawImage($left,  0, 0)
$gfx.DrawImage($right, $left.Width + 16, 0)
$gfx.Dispose()

$canvas.Save($Out)
$canvas.Dispose(); $left.Dispose(); $right.Dispose()

"$Out  (DBeaver a esquerda, C-Otter a direita)"

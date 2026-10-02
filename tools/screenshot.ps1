# Captura a janela do C-Otter para verificacao visual.
#   tools\screenshot.ps1 -Out shot.png
#
# Captura a JANELA, nao a tela: PrintWindow pede ao compositor o conteudo dela,
# esteja ou nao coberta por outra. A versao anterior copiava o retangulo da
# tela (CopyFromScreen) depois de trazer o C-Otter para a frente -- o que
# roubava o foco de quem estava usando a maquina e, com outra janela por cima,
# fotografava a janela DELA (aconteceu: um DevTools do navegador do usuario
# saiu na captura).
#
# -Screen volta ao comportamento antigo, para quando se quer ver o que esta'
# de fato na tela (uma janela nativa por cima, um menu do sistema).
param(
    [string]$ProcessName = "c-otter",
    [string]$Out = "$env:TEMP\c-otter.png",
    [switch]$Screen
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterShot {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "processo '$ProcessName' sem janela visivel" }

$hwnd = $proc.MainWindowHandle

$rect = New-Object OtterShot+RECT
[void][OtterShot]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { throw "dimensoes invalidas: ${w}x${h}" }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)

if ($Screen) {
    [void][OtterShot]::SetForegroundWindow($hwnd)
    Start-Sleep -Milliseconds 700
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
} else {
    # 2 = PW_RENDERFULLCONTENT: sem ele uma janela OpenGL sai preta.
    $hdc = $gfx.GetHdc()
    $ok = [OtterShot]::PrintWindow($hwnd, $hdc, 2)
    $gfx.ReleaseHdc($hdc)
    if (-not $ok) { throw "PrintWindow falhou; tente -Screen" }
}

$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose()
$bmp.Dispose()

Write-Host "$Out (${w}x${h})"

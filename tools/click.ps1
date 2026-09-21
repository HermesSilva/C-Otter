# Clica em coordenadas relativas a janela do C-Otter e captura a tela.
#
#   tools\click.ps1 -X 492 -Y 771 -Out shot.png
param(
    [int]$X,
    [int]$Y,
    [string]$Out = "$env:TEMP\c-otter.png",
    [int]$WaitMs = 700,
    [switch]$NoShot,
    # Duplo clique: dois clicks dentro do intervalo do sistema. Chamar o
    # script duas vezes NAO serve -- o tempo entre as duas execucoes passa
    # do limite e o app ve dois cliques simples.
    [switch]$Double,
    # Botao direito, para menu de contexto.
    [switch]$Right
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterClick {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$proc = Get-Process -Name "c-otter" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "c-otter nao esta rodando" }

$hwnd = $proc.MainWindowHandle
[void][OtterClick]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 500

$rect = New-Object OtterClick+RECT
[void][OtterClick]::GetWindowRect($hwnd, [ref]$rect)

if ($PSBoundParameters.ContainsKey('X')) {
    [void][OtterClick]::SetCursorPos($rect.Left + $X, $rect.Top + $Y)
    Start-Sleep -Milliseconds 250

    # 0x0002/0x0004 = botao esquerdo desce/sobe; 0x0008/0x0010 = direito.
    $down = if ($Right) { 0x0008 } else { 0x0002 }
    $up   = if ($Right) { 0x0010 } else { 0x0004 }

    [OtterClick]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero)
    [OtterClick]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero)

    if ($Double) {
        # 80 ms: bem abaixo do limite padrao de 500 ms do Windows.
        Start-Sleep -Milliseconds 80
        [OtterClick]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero)
        [OtterClick]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero)
    }

    Start-Sleep -Milliseconds $WaitMs
}

if (-not $NoShot) {
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose(); $bmp.Dispose()
    Write-Host "$Out (${w}x${h})"
}

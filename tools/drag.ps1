# Arrasta o mouse dentro da janela do C-Otter, para exercitar o docking e
# alcancar controles que ficaram sob outra janela.
#
#   tools\drag.ps1 -FromX 800 -FromY 110 -ToX 800 -ToY 600
#
# As coordenadas sao relativas a janela, como em click.ps1.
param(
    [int]$FromX,
    [int]$FromY,
    [int]$ToX,
    [int]$ToY,
    [string]$Out = "",
    [int]$Steps = 24,
    [int]$WaitMs = 900
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterDrag {
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
[void][OtterDrag]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400

$rect = New-Object OtterDrag+RECT
[void][OtterDrag]::GetWindowRect($hwnd, [ref]$rect)

# Passos intermediarios: o ImGui decide o alvo do docking pelo movimento, e
# um salto direto da origem ao destino nao produz os eventos que ele observa.
[void][OtterDrag]::SetCursorPos($rect.Left + $FromX, $rect.Top + $FromY)
Start-Sleep -Milliseconds 200
[OtterDrag]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # botao desce

for ($i = 1; $i -le $Steps; $i++) {
    $x = $FromX + ($ToX - $FromX) * $i / $Steps
    $y = $FromY + ($ToY - $FromY) * $i / $Steps
    [void][OtterDrag]::SetCursorPos($rect.Left + [int]$x, $rect.Top + [int]$y)
    Start-Sleep -Milliseconds 25
}

[OtterDrag]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # botao sobe
Start-Sleep -Milliseconds $WaitMs

if ($Out) {
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose(); $bmp.Dispose()
    Write-Host "$Out (${w}x${h})"
}

# Preenche o dialogo de conexao, conecta e captura a tela.
param(
    [string]$Password = "senhas",
    [string]$Out = "$env:TEMP\connected.png",
    [int]$WaitMs = 2500
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

function Get-OtterWindow {
    $proc = Get-Process -Name "c-otter" -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $proc) { throw "c-otter nao esta rodando" }
    return $proc.MainWindowHandle
}

function Click-At([int]$x, [int]$y) {
    [void][W]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 200
    [W]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [W]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function Save-Shot([IntPtr]$hwnd, [string]$path) {
    $rect = New-Object W+RECT
    [void][W]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose(); $bmp.Dispose()
    return "${w}x${h}"
}

$hwnd = Get-OtterWindow
[void][W]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 800

$rect = New-Object W+RECT
[void][W]::GetWindowRect($hwnd, [ref]$rect)

# O dialogo abre centralizado. O campo "senha" e' o quinto, ~118px abaixo do topo
# da area de campos.
$cx = $rect.Left + ($rect.Right - $rect.Left) / 2
$cy = $rect.Top  + ($rect.Bottom - $rect.Top) / 2

Click-At ([int]($cx + 40)) ([int]($cy + 30))       # campo senha

# ImGui processa entrada por frame: SendKeys de uma vez pode perder teclas.
foreach ($ch in $Password.ToCharArray()) {
    [System.Windows.Forms.SendKeys]::SendWait($ch)
    Start-Sleep -Milliseconds 90
}
Start-Sleep -Milliseconds 400

Click-At ([int]($cx - 100)) ([int]($cy + 85))      # botao Conectar
Start-Sleep -Milliseconds $WaitMs

$size = Save-Shot $hwnd $Out
Write-Host "$Out ($size)"

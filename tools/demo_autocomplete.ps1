# Dispara o autocomplete na janela do C-Otter e captura o popup.
#
# Clica no fim do editor, digita um prefixo e espera o popup aparecer.
param(
    [string]$Type = "cat",
    [string]$Out  = "$env:TEMP\autocomplete.png"
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

$proc = Get-Process -Name "c-otter" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "c-otter nao esta rodando" }

$hwnd = $proc.MainWindowHandle
[void][W]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 800

$rect = New-Object W+RECT
[void][W]::GetWindowRect($hwnd, [ref]$rect)

# Clica dentro do editor SQL, na linha vazia do fim (linha 12).
$clickX = $rect.Left + 700
$clickY = $rect.Top  + 345
[void][W]::SetCursorPos($clickX, $clickY)
Start-Sleep -Milliseconds 300
[W]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # left down
[W]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # left up
Start-Sleep -Milliseconds 400

# Digita o prefixo; o autocomplete dispara ao digitar.
[System.Windows.Forms.SendKeys]::SendWait("SELECT ")
Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait($Type)
Start-Sleep -Milliseconds 1200   # triggerDelay = 200ms + folga

$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()

Write-Host "$Out"

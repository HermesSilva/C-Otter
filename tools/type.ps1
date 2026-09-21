# Digita texto na janela do C-Otter, para exercitar caminhos que exigem
# conteudo no editor (script modificado, filtro, celula editada).
#
#   tools\type.ps1 -Text "SELECT 1"
#   tools\type.ps1 -Keys "%{F4}"      # Alt+F4, sintaxe do SendKeys
#
# SendKeys usa a janela em PRIMEIRO PLANO: o script a traz para frente antes
# de enviar. Caracteres especiais de SendKeys (+ ^ % ~ ( ) { } [ ]) precisam
# de chaves ao redor quando literais -- -Text ja' faz esse escape, -Keys nao.
param(
    [string]$Text,
    [string]$Keys,
    [string]$Out = "",
    [int]$WaitMs = 700
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterType {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$proc = Get-Process -Name "c-otter" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "c-otter nao esta rodando" }

$hwnd = $proc.MainWindowHandle
[void][OtterType]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400

if ($Text) {
    # Escapa os metacaracteres do SendKeys para que o texto chegue literal.
    $escaped = $Text -replace '([+^%~(){}\[\]])', '{$1}'
    [System.Windows.Forms.SendKeys]::SendWait($escaped)
}
if ($Keys) {
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
}

Start-Sleep -Milliseconds $WaitMs

if ($Out) {
    $rect = New-Object OtterType+RECT
    [void][OtterType]::GetWindowRect($hwnd, [ref]$rect)
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose(); $bmp.Dispose()
    Write-Host "$Out (${w}x${h})"
}

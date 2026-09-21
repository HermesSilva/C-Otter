# Clique e captura na janela do C-Otter, em coordenadas RELATIVAS a ela.
#
# Por que existe: o tipo P/Invoke declarado com Add-Type nao sobrevive entre
# chamadas do shell, e redeclara-lo a cada comando encheu o historico de
# ruido. Aqui ele nasce uma vez por invocacao do script.
#
#   tools\ui.ps1 -Click "173,145" -Out shot.png
#   tools\ui.ps1 -Click "173,145;700,667" -Out shot.png   # varios, em ordem
#   tools\ui.ps1 -Out shot.png                            # so' captura
param(
    [string] $Click = "",
    [string] $Out   = "$env:TEMP\c-otter.png",
    [int]    $WaitMs = 600
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterUi {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$proc = Get-Process c-otter -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "c-otter nao esta rodando" }

[void][OtterUi]::SetForegroundWindow($proc.MainWindowHandle)
Start-Sleep -Milliseconds 500

$rect = New-Object OtterUi+RECT
[void][OtterUi]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)

foreach ($point in ($Click -split ';')) {
    if (-not $point) { continue }
    $xy = $point -split ','
    if ($xy.Count -ne 2) { continue }

    [void][OtterUi]::SetCursorPos($rect.Left + [int]$xy[0], $rect.Top + [int]$xy[1])
    Start-Sleep -Milliseconds 250
    [OtterUi]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [OtterUi]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds $WaitMs
}

# A janela pode ter sido movida ou redimensionada pelos cliques.
[void][OtterUi]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top

$bitmap = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bitmap)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                    (New-Object System.Drawing.Size($w, $h)))
$gfx.Dispose()
$bitmap.Save($Out)
$bitmap.Dispose()

"$Out ($w x $h)"

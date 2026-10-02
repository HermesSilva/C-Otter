# Abre o C-Otter, conecta pelo perfil salvo, roda uma consulta e deixa a
# grade pronta -- depois envia as teclas pedidas e captura.
#
# Por que existe: verificar uma tecla da grade na aplicacao levava vinte
# linhas de P/Invoke repetidas a cada tentativa, e o duplo clique precisa ser
# um duplo clique de verdade (o click.ps1 so' faz clique simples).
param(
    [string] $Sql  = "SELECT id, nome, limite FROM otter_test.cliente ORDER BY id",
    [string] $Keys = "",
    [string] $Out  = "$env:TEMP\grid.png",
    [string] $Profile = "localhost",
    # Celula onde clicar antes de enviar as teclas, em coordenadas da janela.
    [int] $CellX = 560,
    [int] $CellY = 598
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterGrid {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

$root = Split-Path $PSScriptRoot -Parent
& build\stop_app.ps1
Start-Process "$root\build\win-release\bin\c-otter.exe"
Start-Sleep -Seconds 4

# O dialogo de conexao abre sozinho quando ha' perfil salvo.
& "$root\tools\click.ps1" -X 734 -Y 750 -NoShot | Out-Null
Start-Sleep -Milliseconds 500

$proc = Get-Process c-otter | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[void][OtterGrid]::SetForegroundWindow($proc.MainWindowHandle)
$rect = New-Object OtterGrid+RECT
[void][OtterGrid]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)

function Send-DoubleClick([int]$X, [int]$Y) {
    [void][OtterGrid]::SetCursorPos($rect.Left + $X, $rect.Top + $Y)
    Start-Sleep -Milliseconds 300
    [OtterGrid]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)
    [OtterGrid]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [OtterGrid]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero)
    [OtterGrid]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero)
}

# Conectar: duplo clique no perfil salvo da lista.
Send-DoubleClick 54 253
Start-Sleep -Seconds 6

# Consulta.
& "$root\tools\click.ps1" -X 900 -Y 300 -NoShot | Out-Null
Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait("^a")
Start-Sleep -Milliseconds 200
[System.Windows.Forms.SendKeys]::SendWait($Sql)
Start-Sleep -Milliseconds 400
[System.Windows.Forms.SendKeys]::SendWait("^{ENTER}")
Start-Sleep -Seconds 3

# Foco na grade, clicando numa celula.
& "$root\tools\click.ps1" -X $CellX -Y $CellY -NoShot | Out-Null
Start-Sleep -Milliseconds 500

# SendKeys NAO entrega teclas de SETA a esta aplicacao.
#
# Descoberto com um trace que registrava toda tecla recebida: letras chegavam
# ("A", "B"), setas nunca. PostMessage com WM_KEYDOWN/WM_KEYUP entrega. Custou
# varias tentativas de "corrigir" um codigo que estava certo -- por isso fica
# escrito aqui.
$virtualKeys = @{
    '{DOWN}'  = 0x28; '{UP}'    = 0x26
    '{LEFT}'  = 0x25; '{RIGHT}' = 0x27
    '{HOME}'  = 0x24; '{END}'   = 0x23
    '{PGUP}'  = 0x21; '{PGDN}'  = 0x22
    '{ENTER}' = 0x0D; '{DELETE}'= 0x2E; '{ESC}' = 0x1B
}

function Send-VirtualKey([int]$Vk) {
    # bit 24 do lParam marca tecla estendida, que e' o caso das setas.
    [void][OtterGrid]::PostMessage($proc.MainWindowHandle, 0x0100, [IntPtr]$Vk, [IntPtr]0x01500001)
    Start-Sleep -Milliseconds 25
    [void][OtterGrid]::PostMessage($proc.MainWindowHandle, 0x0101, [IntPtr]$Vk, [IntPtr]0xC1500001)
}

if ($Keys) {
    # Uma tecla por vez, com pausa: o ImGui processa por quadro, e um lote
    # enviado de uma vez perde teclas.
    foreach ($key in $Keys -split ' ') {
        if (-not $key) { continue }

        if ($virtualKeys.ContainsKey($key)) {
            Send-VirtualKey $virtualKeys[$key]
        } else {
            [System.Windows.Forms.SendKeys]::SendWait($key)
        }
        Start-Sleep -Milliseconds 400
    }
}

& "$root\tools\screenshot.ps1" -Out $Out



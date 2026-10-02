# Clique na janela do C-Otter DE TESTE por mensagem, sem tocar no mouse de
# quem usa a maquina.
#   tools\postclick.ps1 -X 497 -Y 107 [-Hover]
#
# X/Y sao as coordenadas da captura de tools\screenshot.ps1 (o retangulo da
# janela, com a borda). tools\click.ps1 move o cursor de verdade e clica com
# mouse_event -- e a diretiva 11-A proibe isso desde que um clique caiu no
# navegador do usuario. Aqui vao WM_MOUSEMOVE/WM_LBUTTONDOWN/WM_LBUTTONUP so'
# para a janela de teste: o cursor real nao se move e o foco nao muda.
#
# Prova o caminho do clique ate' o ImGui (o GLFW le' a posicao do lParam
# enquanto a janela nao tem foco); NAO prova o que o Windows faz com um clique
# fisico (ativar a janela, por exemplo).
param(
    [Parameter(Mandatory)] [int]$X,
    [Parameter(Mandatory)] [int]$Y,
    # So' move: para capturar o realce e a dica.
    [switch]$Hover,
    [int]$WaitMs = 600
)

$ErrorActionPreference = "Stop"
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class OtterPost {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
'@

# So' a instancia de teste (o PID que build\start_app.ps1 anotou).
$file = Join-Path $env:TEMP "otter-test.pid"
if (-not (Test-Path $file)) { throw "sem C-Otter de teste aberto (falta $file)" }
$process = Get-Process -Id ([int]([System.IO.File]::ReadAllText($file).Trim())) -ErrorAction Stop
if ($process.ProcessName -ne 'c-otter' -or $process.MainWindowHandle -eq 0) {
    throw "o PID anotado nao e' um C-Otter com janela"
}
$hwnd = $process.MainWindowHandle

$rect = New-Object OtterPost+RECT
[void][OtterPost]::GetWindowRect($hwnd, [ref]$rect)
$origin = New-Object OtterPost+POINT
[void][OtterPost]::ClientToScreen($hwnd, [ref]$origin)
$cx = $X - ($origin.X - $rect.Left)
$cy = $Y - ($origin.Y - $rect.Top)
$l  = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))

$WM_MOUSEMOVE = 0x0200; $WM_LBUTTONDOWN = 0x0201; $WM_LBUTTONUP = 0x0202; $MK_LBUTTON = 1

# Dois movimentos: o primeiro faz o GLFW registrar a entrada do cursor.
[void][OtterPost]::PostMessage($hwnd, $WM_MOUSEMOVE, [IntPtr]0, $l)
Start-Sleep -Milliseconds 80
[void][OtterPost]::PostMessage($hwnd, $WM_MOUSEMOVE, [IntPtr]0, $l)
Start-Sleep -Milliseconds 120
if (-not $Hover) {
    [void][OtterPost]::PostMessage($hwnd, $WM_LBUTTONDOWN, [IntPtr]$MK_LBUTTON, $l)
    Start-Sleep -Milliseconds 60
    [void][OtterPost]::PostMessage($hwnd, $WM_LBUTTONUP, [IntPtr]0, $l)
}
Start-Sleep -Milliseconds $WaitMs
"client ($cx,$cy)"

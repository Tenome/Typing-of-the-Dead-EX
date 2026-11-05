# Closes every running Tod.exe like Alt+F4 and answers its "quit?" confirmation box with Yes.
param([int]$TimeoutSeconds = 10)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class GameWindows {
    public delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr FindDialog(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            var name = new StringBuilder(64);
            GetClassName(window, name, name.Capacity);
            if (owner == processId && IsWindowVisible(window) && name.ToString() == "#32770") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$WM_COMMAND = 0x0111; $WM_SYSKEYDOWN = 0x0104; $WM_SYSKEYUP = 0x0105; $VK_F4 = 0x73; $IDYES = 6
$games = @(Get-Process Tod -ErrorAction SilentlyContinue)
if (-not $games) { "Tod.exe is not running."; exit 0 }
foreach ($game in $games) {
    [GameWindows]::PostMessage($game.MainWindowHandle, $WM_SYSKEYDOWN, [IntPtr]$VK_F4, [IntPtr]0x20000000) | Out-Null
    [GameWindows]::PostMessage($game.MainWindowHandle, $WM_SYSKEYUP, [IntPtr]$VK_F4, [IntPtr]0xE0000000) | Out-Null
}
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (($games | Where-Object { -not $_.HasExited }) -and (Get-Date) -lt $deadline) {
    foreach ($game in $games | Where-Object { -not $_.HasExited }) {
        $dialog = [GameWindows]::FindDialog([uint32]$game.Id)
        if ($dialog -ne [IntPtr]::Zero) { [GameWindows]::PostMessage($dialog, $WM_COMMAND, [IntPtr]$IDYES, [IntPtr]::Zero) | Out-Null }
    }
    Start-Sleep -Milliseconds 250
    foreach ($game in $games) { $game.Refresh() }
}
$open = @($games | Where-Object { -not $_.HasExited })
foreach ($game in $games | Where-Object { $_.HasExited }) { "Tod.exe $($game.Id) closed (exit code $($game.ExitCode))." }
if ($open) { "Tod.exe $($open.Id -join ', ') did not close within $TimeoutSeconds s."; exit 1 }

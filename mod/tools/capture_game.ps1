# Starts Tod.exe windowed (Tod.ini backed up and restored), saves screenshots of the game window itself
# (PrintWindow) at the given seconds, then closes the game. Keys: -Keys "Enter@8,Escape@10" presses keys at those
# seconds; -EnterFrom/-EnterUntil/-EnterEvery presses Enter repeatedly (from launch Enter leads into Arcade
# gameplay); -TypeFrom/-TypeUntil/-TypeEvery presses A repeatedly (with [debug] autotype=1 that types the words).
# A key is sent only while the game window is the foreground window; otherwise it is skipped.
# -Online starts a second copy as the online joiner (TODMOD_ONLINE=join; the first copy is the host, which gets the
# keys); screenshots of the joiner are saved as join_NNNs.png. -JoinKeys "Enter@45" presses keys in the joiner's window.
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [int[]]$Seconds = @(10, 20, 30),
    [string]$Keys = "",
    [double]$EnterFrom = 0,
    [double]$EnterUntil = 0,
    [double]$EnterEvery = 2,
    [double]$TypeFrom = 0,
    [double]$TypeUntil = 0,
    [double]$TypeEvery = 0.15,
    [switch]$Online,
    [string]$JoinKeys = ""
)
$ErrorActionPreference = "Stop"
$game = Join-Path $PSScriptRoot "..\..\EX Installation\TODEX"
$todIni = Join-Path $env:APPDATA "Sega\TODEX\Tod.ini"
$backup = "$todIni.capture_backup"
Copy-Item $todIni $backup -Force
$cp932 = [Text.Encoding]::GetEncoding(932)
$text = $cp932.GetString([IO.File]::ReadAllBytes($todIni))
$text = $text -replace '(?m)^Window=.*$', 'Window=true' -replace '(?m)^Active=.*$', 'Active=true'
[IO.File]::WriteAllBytes($todIni, $cp932.GetBytes($text))

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CaptureNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    public static bool Focus(IntPtr hwnd) {
        if (GetForegroundWindow() == hwnd) return true;
        uint foreground = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
        uint mine = GetCurrentThreadId();
        AttachThreadInput(mine, foreground, true);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        AttachThreadInput(mine, foreground, false);
        return GetForegroundWindow() == hwnd;
    }
}
"@

New-Item -ItemType Directory -Force $OutDir | Out-Null
try {
    $windows = @()
    if ($Online) { $env:TODMOD_ONLINE = "host" }
    $process = Start-Process -FilePath (Join-Path $game "Tod.exe") -WorkingDirectory $game -PassThru
    $windows += [pscustomobject]@{ Process = $process; Prefix = "shot" }
    if ($Online) {
        Start-Sleep -Seconds 2
        $env:TODMOD_ONLINE = "join"
        $joiner = Start-Process -FilePath (Join-Path $game "Tod.exe") -WorkingDirectory $game -PassThru
        $windows += [pscustomobject]@{ Process = $joiner; Prefix = "join" }
        Remove-Item Env:TODMOD_ONLINE
    }
    $events = @()
    foreach ($shot in $Seconds) { $events += [pscustomobject]@{ At = $shot; Kind = "shot"; Key = "" } }
    if ($Keys) {
        foreach ($entry in $Keys.Split(",")) {
            $name, $at = $entry.Split("@")
            $events += [pscustomobject]@{ At = [double]$at; Kind = "key"; Key = $name }
        }
    }
    if ($JoinKeys) {
        foreach ($entry in $JoinKeys.Split(",")) {
            $name, $at = $entry.Split("@")
            $events += [pscustomobject]@{ At = [double]$at; Kind = "joinkey"; Key = $name }
        }
    }
    if ($EnterUntil -gt $EnterFrom) {
        for ($at = $EnterFrom; $at -le $EnterUntil; $at += $EnterEvery) {
            $events += [pscustomobject]@{ At = $at; Kind = "key"; Key = "Enter" }
        }
    }
    if ($TypeUntil -gt $TypeFrom) {
        for ($at = $TypeFrom; $at -le $TypeUntil; $at += $TypeEvery) {
            $events += [pscustomobject]@{ At = $at; Kind = "key"; Key = "A" }
        }
    }
    $skipped = 0
    $scancodes = @{ Enter = 0x1C; Escape = 0x01; Down = 0x50; Up = 0x48; Left = 0x4B; Right = 0x4D; Space = 0x39; A = 0x1E; F12 = 0x58 }
    $virtualKeys = @{ Enter = 0x0D; Escape = 0x1B; Down = 0x28; Up = 0x26; Left = 0x25; Right = 0x27; Space = 0x20; A = 0x41; F12 = 0x7B }
    $start = Get-Date
    foreach ($event in ($events | Sort-Object At)) {
        $wait = $event.At - ((Get-Date) - $start).TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
        $keyProcess = if ($event.Kind -eq "joinkey" -and $joiner) { $joiner } else { $process }
        $keyProcess.Refresh()
        $window = $keyProcess.MainWindowHandle
        if ($event.Kind -in @("key", "joinkey")) {
            if ($window -eq [IntPtr]::Zero) { continue }
            if (-not [CaptureNative]::Focus($window)) { $skipped++; continue }
            if (-not $virtualKeys.ContainsKey($event.Key) -and $event.Key.Length -eq 1) {
                $virtualKeys[$event.Key] = [int][char]$event.Key.ToUpper()
                $scancodes[$event.Key] = [int][CaptureNative]::MapVirtualKey([uint32]$virtualKeys[$event.Key], 0)
            }
            $extended = if ($event.Key -in @("Down", "Up", "Left", "Right")) { 1 } else { 0 }
            [CaptureNative]::keybd_event($virtualKeys[$event.Key], $scancodes[$event.Key], $extended, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 40
            [CaptureNative]::keybd_event($virtualKeys[$event.Key], $scancodes[$event.Key], 2 -bor $extended, [UIntPtr]::Zero)
            continue
        }
        foreach ($target in $windows) {
            $target.Process.Refresh()
            $window = $target.Process.MainWindowHandle
            if ($window -eq [IntPtr]::Zero) { continue }
            $rect = New-Object CaptureNative+RECT
            [CaptureNative]::GetClientRect($window, [ref]$rect) | Out-Null
            $bitmap = New-Object System.Drawing.Bitmap ([Math]::Max(1, $rect.Right)), ([Math]::Max(1, $rect.Bottom))
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $hdc = $graphics.GetHdc()
            [CaptureNative]::PrintWindow($window, $hdc, 3) | Out-Null
            $graphics.ReleaseHdc($hdc)
            $path = Join-Path $OutDir ("{0}_{1:000}s.png" -f $target.Prefix, $event.At)
            $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
            $graphics.Dispose(); $bitmap.Dispose()
            "saved $path"
        }
    }
    if ($skipped) { "skipped $skipped key presses (game window not in front)" }
} finally {
    & (Join-Path $PSScriptRoot "close_game.ps1") -TimeoutSeconds 30
    Start-Sleep -Seconds 2
    Copy-Item $backup $todIni -Force
    Remove-Item $backup
    "Tod.ini restored"
}

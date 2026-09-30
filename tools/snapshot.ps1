<#
.SYNOPSIS
    Play a movie in Ruffle and save what it shows as a PNG: a look at the turn bar without the game.

.DESCRIPTION
    Outside the game the turn bar draws a sample fight (ui/turnorder/frame_1/DoAction.as),
    so a Flash player on its own shows what the bar looks like. This opens the movie in
    Ruffle (https://ruffle.rs, an open-source Flash player; installed in C:\Modding on
    this machine 2026-09-30), waits for it to draw, captures the window's client area,
    and closes it. It needs a desktop session: the window must be on screen.

    Ruffle draws with the GPU, which plain PrintWindow captures as black; the capture
    asks for the full content (PW_RENDERFULLCONTENT) and falls back to copying the
    window's place on the screen.

.EXAMPLE
    .\tools\snapshot.ps1                                    # build\ui\turnorder.swf -> build\ui\turnorder.png
    .\tools\snapshot.ps1 -Movie other.swf -Out other.png -Seconds 5
#>
[CmdletBinding()]
param(
    [string] $Movie = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\ui\turnorder.swf'),
    [string] $Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\ui\turnorder.png'),
    [string] $Ruffle = 'C:\Modding\ruffle_2026_09_30\ruffle.exe',
    [int] $Width = 1280,
    [int] $Height = 720,
    [int] $Seconds = 3
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'console.ps1')

if (-not (Test-Path $Movie)) { throw "No $Movie; run python tools\movie.py first" }
if (-not (Test-Path $Ruffle)) { throw "No Ruffle at $Ruffle" }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SnapshotWin32 {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lp);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    public static IntPtr Find(uint pid, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            var text = new StringBuilder(512); GetWindowText(h, text, 512);
            if (p == pid && text.ToString() == title) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

function Test-Blank([System.Drawing.Bitmap] $Image) {
    # All one colour: a capture that caught nothing.
    $first = $Image.GetPixel(0, 0)
    for ($y = 0; $y -lt $Image.Height; $y += 17) {
        for ($x = 0; $x -lt $Image.Width; $x += 17) {
            if ($Image.GetPixel($x, $y) -ne $first) { return $false }
        }
    }
    return $true
}

$process = Start-Process -FilePath $Ruffle -PassThru -ArgumentList @(
    '--width', $Width, '--height', $Height, '--storage', 'memory', '--filesystem-access-mode', 'allow',
    '-Psample=1', $Movie)
try {
    $title = "Ruffle - $(Split-Path -Leaf $Movie)"
    $window = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds(20)
    while ($window -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $window = [SnapshotWin32]::Find([uint32]$process.Id, $title)
    }
    if ($window -eq [IntPtr]::Zero) { throw "Ruffle opened no window titled '$title'" }
    [void][SnapshotWin32]::SetForegroundWindow($window)
    Start-Sleep -Seconds $Seconds

    $client = New-Object SnapshotWin32+RECT
    [void][SnapshotWin32]::GetClientRect($window, [ref]$client)
    $w = $client.Right - $client.Left
    $h = $client.Bottom - $client.Top
    if ($w -le 0 -or $h -le 0) { throw "the window has no client area ($w x $h)" }
    $image = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($image)
    $hdc = $graphics.GetHdc()
    [void][SnapshotWin32]::PrintWindow($window, $hdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    $graphics.ReleaseHdc($hdc)
    if (Test-Blank $image) {
        $corner = New-Object SnapshotWin32+POINT
        [void][SnapshotWin32]::ClientToScreen($window, [ref]$corner)
        $graphics.CopyFromScreen($corner.X, $corner.Y, 0, 0, (New-Object System.Drawing.Size $w, $h))
        Show-Info 'PrintWindow caught nothing; copied the screen instead'
    }
    $graphics.Dispose()
    New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
    $image.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $image.Dispose()
    Show-Ok "$Out ($w x $h)"
}
finally {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
}

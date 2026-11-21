# Starts the built Standalone app, waits for the WebView2 UI to come up, screenshots its window
# into tests/screenshots/standalone.png and closes it. A smoke test of the real editor + bridge.
# Usage: powershell -ExecutionPolicy Bypass -File vst\tests\run-standalone-check.ps1
$ErrorActionPreference = 'Stop'
$exe = Join-Path $env:LOCALAPPDATA 'ResoniqueVST\build\ResoniqueVST_artefacts\Release\Standalone\Resonique.exe'
if (-not (Test-Path $exe)) { throw "Not built: $exe (run vst\build.bat)" }
$out = Join-Path $PSScriptRoot 'screenshots'
New-Item -ItemType Directory -Force $out | Out-Null
$shot = Join-Path $out 'standalone.png'

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
}
"@

$p = Start-Process -FilePath $exe -PassThru
try {
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline -and $p.MainWindowHandle -eq 0) { Start-Sleep -Milliseconds 250; $p.Refresh() }
    if ($p.MainWindowHandle -eq 0) { throw 'The Standalone never showed a window' }
    Start-Sleep -Seconds 6     # WebView2 start-up + the first UI frames
    [Win32]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
    [Win32]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 800
    $r = New-Object Win32+RECT
    [Win32]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    if ($w -lt 100 -or $h -lt 100) { throw "Window too small: $w x $h" }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    # a blank (all-background) window means WebView2 did not come up: check the pixel spread
    $bmp2 = [System.Drawing.Bitmap]::FromFile($shot)
    $samples = @()
    for ($y = 40; $y -lt $h - 40; $y += [Math]::Max(1, [int]($h / 24))) {
        for ($x = 40; $x -lt $w - 40; $x += [Math]::Max(1, [int]($w / 24))) { $samples += $bmp2.GetPixel($x, $y).GetBrightness() }
    }
    $bmp2.Dispose()
    $spread = ($samples | Measure-Object -Maximum -Minimum)
    Write-Host ("window {0}x{1}, brightness spread {2:N2}..{3:N2}" -f $w, $h, $spread.Minimum, $spread.Maximum)
    if ($spread.Maximum - $spread.Minimum -lt 0.2) { Write-Host 'FAIL the window looks blank (no UI)'; exit 1 }
    Write-Host "ok   $shot"
}
finally {
    if (-not $p.HasExited) { $p.CloseMainWindow() | Out-Null; Start-Sleep -Seconds 2; if (-not $p.HasExited) { $p.Kill() } }
}

# Renders the Play Store graphics with headless Edge:
#   icon-512.png, feature-1024x500.png        from graphics.html
#   screen-1..4-*.png                         the web build (web/index.html) in the phone layout,
#                                             three portrait pages (1080 x 2160) and one landscape (2160 x 1080)
# Usage: powershell -ExecutionPolicy Bypass -File android\store\make-graphics.ps1
# Run vst\webhost\build-web.ps1 first so web/index.html is current.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$here = $PSScriptRoot
$repo = Split-Path (Split-Path $here -Parent) -Parent
$page = Join-Path $repo 'web\index.html'
if (-not (Test-Path $page)) { throw "web\index.html not found: run vst\webhost\build-web.ps1 first" }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Edge not found' }
$tmp = Join-Path $env:TEMP 'rq-store'; New-Item -ItemType Directory -Force $tmp | Out-Null

function Shoot([string]$url, [int]$w, [int]$h, [int]$scale, [string]$out) {
    if (Test-Path $out) { Remove-Item $out }
    Start-Process -FilePath $edge -Wait -WindowStyle Hidden -ArgumentList @('--headless=new', '--disable-gpu', '--hide-scrollbars', "--user-data-dir=$tmp\profile",
        "--window-size=$w,$h", "--force-device-scale-factor=$scale", '--virtual-time-budget=3000', "--screenshot=$out", "`"$url`"")
    if (-not (Test-Path $out)) { throw "no screenshot: $out" }
}

# ---- icon + feature graphic ----
$full = Join-Path $tmp 'full.png'
Shoot ('file:///' + ((Join-Path $here 'graphics.html') -replace '\\', '/')) 1024 1012 1 $full
$img = [System.Drawing.Image]::FromFile($full)
function Save-Crop($x, $y, $w, $h, $out) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, $w, $h), (New-Object System.Drawing.Rectangle $x, $y, $w, $h), [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "wrote $out"
}
Save-Crop 0 0 512 512 (Join-Path $here 'icon-512.png')
Save-Crop 0 512 1024 500 (Join-Path $here 'feature-1024x500.png')
$img.Dispose()

# ---- phone screenshots: the real page in its compact layout (?layout=... forces it, ?screenshot=<page> opens that page
#      without the start overlay). CSS size x device scale factor 2 = 1080 x 2160 / 2160 x 1080 (Play: long side <= 2x short). ----
$base = 'file:///' + ($page -replace '\\', '/')
$shots = @(
    @{ name = 'screen-1-play.png';      page = 'main';    w = 540;  h = 1080; layout = 'portrait' },
    @{ name = 'screen-2-physics.png';   page = 'physics'; w = 540;  h = 1080; layout = 'portrait' },
    @{ name = 'screen-3-mics.png';      page = 'mics';    w = 540;  h = 1080; layout = 'portrait' },
    @{ name = 'screen-4-landscape.png'; page = 'main';    w = 1080; h = 540;  layout = 'landscape' }
)
foreach ($s in $shots) {
    $out = Join-Path $here $s.name
    Shoot "$base`?screenshot=$($s.page)&layout=$($s.layout)" $s.w $s.h 2 $out
    $i = [System.Drawing.Image]::FromFile($out); Write-Host "wrote $out ($($i.Width) x $($i.Height))"; $i.Dispose()
}
Write-Host 'STORE GRAPHICS OK'

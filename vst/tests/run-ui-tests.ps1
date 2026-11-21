# Opens the UI in headless Edge with a stand-in bridge (tests/ui-preview.js) and
# screenshots every page into tests/screenshots/. Also fails on JavaScript errors.
# Usage: powershell -ExecutionPolicy Bypass -File vst\tests\run-ui-tests.ps1
$ErrorActionPreference = 'Stop'
$vst = Split-Path $PSScriptRoot -Parent
$ui = Join-Path $vst 'ui'
$out = Join-Path $PSScriptRoot 'screenshots'
New-Item -ItemType Directory -Force $out | Out-Null

$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
          "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe", "$env:ProgramFiles\Google\Chrome\Application\chrome.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Edge or Chrome not found' }

# a preview page: the real index.html with the stand-in bridge and the factory presets injected
$presets = Get-Content (Join-Path $vst 'presets\factory_presets.json') -Raw | ConvertFrom-Json
$list = @()
$i = 0
foreach ($cat in $presets.categories) {
    foreach ($p in $cat.presets) {
        $list += [ordered]@{ id = "factory:$i"; name = $p.name; category = $cat.name; description = $p.description; factory = $true; params = $p.params }
        $i++
    }
}
$json = $list | ConvertTo-Json -Depth 5 -Compress
$html = Get-Content (Join-Path $ui 'index.html') -Raw -Encoding UTF8
$pages = 'main', 'physics', 'mics', 'perf', 'tuning'
$fails = 0
foreach ($page in $pages) {
    # the preview page lives in tests/screenshots (never inside ui/, which the build globs)
    $inject = "<script>window.__PREVIEW_PRESETS__ = $json; try { localStorage.setItem('resonique.page', '$page'); } catch (e) {}</script><script src=`"../ui-preview.js`"></script>"
    $preview = $html -replace '<script src="js/params.js"></script>', "$inject`n  <script src=`"js/params.js`"></script>"
    $preview = $preview -replace 'href="css/', 'href="../../ui/css/' -replace 'src="js/', 'src="../../ui/js/'
    $file = Join-Path $out "preview-$page.html"
    Set-Content -Path $file -Value $preview -Encoding utf8
    $shot = Join-Path $out "ui-$page.png"
    if (Test-Path $shot) { Remove-Item $shot }
    $url = 'file:///' + ($file -replace '\\', '/')
    $p = Start-Process -FilePath $edge -ArgumentList @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1210,900',
                       '--virtual-time-budget=2500', "--screenshot=`"$shot`"", "`"$url`"") -PassThru -Wait -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { Write-Host "edge exit $($p.ExitCode)" }
    Remove-Item $file
    if (Test-Path $shot) { Write-Host "ok   $shot" } else { Write-Host "FAIL no screenshot for $page"; $fails++ }
}
if ($fails -eq 0) { Write-Host 'UI SCREENSHOTS OK' } else { exit 1 }

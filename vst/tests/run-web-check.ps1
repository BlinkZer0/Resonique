# Checks the web build: runs the WebAssembly engine from web/index.html in Node
# (tests/web-check.js), opens the page in headless Edge and screenshots it
# (tests/screenshots/web.png), checks the DOM the web host built, then serves the
# page over HTTP and lets its ?selftest hook play a note through the AudioWorklet.
# Usage: powershell -ExecutionPolicy Bypass -File vst\tests\run-web-check.ps1
$ErrorActionPreference = 'Stop'
$vst = Split-Path $PSScriptRoot -Parent
$repo = Split-Path $vst -Parent
$page = Join-Path $repo 'web\index.html'
if (-not (Test-Path $page)) { throw "web\index.html not found: run vst\webhost\build-web.ps1 first" }

Write-Host "== web engine (Node) =="
& node (Join-Path $PSScriptRoot 'web-check.js') $page
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "== page render (headless Edge) =="
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
          "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe", "$env:ProgramFiles\Google\Chrome\Application\chrome.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Edge or Chrome not found' }
$shot = Join-Path $PSScriptRoot 'screenshots\web.png'
if (Test-Path $shot) { Remove-Item $shot }
$url = 'file:///' + ($page -replace '\\', '/')
$p = Start-Process -FilePath $edge -ArgumentList @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--window-size=1210,900',
                   '--virtual-time-budget=3000', "--screenshot=`"$shot`"", "`"$url`"") -PassThru -Wait -WindowStyle Hidden
if (-not (Test-Path $shot)) { Write-Host 'FAIL no screenshot'; exit 1 }
Write-Host "ok   $shot"
# the DOM after the scripts ran (Start-Process: Edge chatters on stderr, which would stop the script)
function Get-Dom([string]$target, [string[]]$extra) {
    $tmp = Join-Path $env:TEMP 'resonique-web-dom.html'
    $err = Join-Path $env:TEMP 'resonique-web-dom.err'
    $argList = @('--headless=new', '--disable-gpu', '--virtual-time-budget=6000') + $extra + @('--dump-dom', "`"$target`"")
    Start-Process -FilePath $edge -ArgumentList $argList -Wait -WindowStyle Hidden -RedirectStandardOutput $tmp -RedirectStandardError $err | Out-Null
    return [IO.File]::ReadAllText($tmp)
}
$dom = Get-Dom $url @()
$checks = [ordered]@{ 'MIDI picker' = 'id="midi-select"'; 'start overlay' = 'class="rq-overlay"'; 'web body class' = '<body class="[^"]*web'; 'preset list filled' = 'Concert Grand' }
$fails = 0
foreach ($k in $checks.Keys) { if ($dom -match $checks[$k]) { Write-Host "ok   $k" } else { Write-Host "FAIL $k"; $fails++ } }
if ($fails -ne 0) { exit 1 }

Write-Host "== audio path (headless Edge over HTTP: AudioWorklet) =="
# tests/web-audio-check.js serves the page, opens it with autoplay allowed and reads the ?selftest result
$json = & node (Join-Path $PSScriptRoot 'web-audio-check.js') $page
$code = $LASTEXITCODE
$r = $null
try { $r = $json | ConvertFrom-Json } catch { }
if ($r) { Write-Host ("     mode {0}, state {1}, {2} Hz, voices {3}, partials {4}, peak {5:N3}, error '{6}'" -f $r.mode, $r.state, $r.sampleRate, $r.voices, $r.partials, $r.maxPeak, $r.error) }
else { Write-Host "     $json" }
if ($code -eq 0) { Write-Host 'ok   a note played through the AudioWorklet'; Write-Host 'WEB PAGE OK' } else { Write-Host 'FAIL audio self-test'; exit 1 }

# Compares the C++ parameter table (ResoniqueTests --dump-params) with ui/js/params.js.
# Usage: powershell -ExecutionPolicy Bypass -File vst\tests\check-params.ps1
$ErrorActionPreference = 'Stop'
$vst = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $env:LOCALAPPDATA 'ResoniqueVST\build\ResoniqueTests_artefacts\Release\ResoniqueTests.exe'
if (-not (Test-Path $exe)) { throw "Build ResoniqueTests first: $exe" }
$cpp = & $exe --dump-params | ConvertFrom-Json

# evaluate params.js in node (the file plus one line that prints the table) and read it back as JSON
$src = Get-Content (Join-Path $vst 'ui\js\params.js') -Raw -Encoding UTF8
$tmp = Join-Path $env:TEMP 'resonique-params-check.js'
Set-Content -Path $tmp -Value ($src + "`nconsole.log(JSON.stringify(PARAM_DEFS));`n") -Encoding utf8
$jsTable = & node $tmp | ConvertFrom-Json

$fails = 0
if ($cpp.Count -ne $jsTable.Count) { Write-Host "FAIL count: C++ $($cpp.Count) vs JS $($jsTable.Count)"; $fails++ }
$n = [Math]::Min($cpp.Count, $jsTable.Count)
for ($i = 0; $i -lt $n; $i++) {
    $c = $cpp[$i]; $j = $jsTable[$i]
    if ($c.id -ne $j.id) { Write-Host "FAIL #$i id: $($c.id) vs $($j.id)"; $fails++; continue }
    if ([Math]::Abs($c.min - $j.min) -gt 1e-9) { Write-Host "FAIL $($c.id) min $($c.min) vs $($j.min)"; $fails++ }
    if ([Math]::Abs($c.max - $j.max) -gt 1e-9) { Write-Host "FAIL $($c.id) max $($c.max) vs $($j.max)"; $fails++ }
    if ([Math]::Abs($c.def - $j.def) -gt 1e-9) { Write-Host "FAIL $($c.id) def $($c.def) vs $($j.def)"; $fails++ }
    $jlog = ($j.skew -eq 'log')
    if ([bool]$c.log -ne $jlog) { Write-Host "FAIL $($c.id) log $($c.log) vs $jlog"; $fails++ }
    if ($c.kind -ne $j.type) { Write-Host "FAIL $($c.id) kind $($c.kind) vs $($j.type)"; $fails++ }
    if ([bool]$c.noPreset -ne [bool]$j.noPreset) { Write-Host "FAIL $($c.id) noPreset"; $fails++ }
    if ($c.choices) {
        $cc = ($c.choices -join '|'); $jc = ($j.choices -join '|')
        if ($cc -ne $jc) { Write-Host "FAIL $($c.id) choices: $cc vs $jc"; $fails++ }
    }
}
if ($fails -eq 0) { Write-Host "PARAMS OK: $n parameters match" } else { Write-Host "$fails difference(s)"; exit 1 }

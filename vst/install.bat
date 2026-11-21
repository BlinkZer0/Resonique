@echo off
rem Copies the built Resonique.vst3 into the system VST3 folder (asks for admin).
rem Usage: install.bat [Debug|Release]
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "SRC=%LOCALAPPDATA%\ResoniqueVST\build\ResoniqueVST_artefacts\%CONFIG%\VST3\Resonique.vst3"
set "DST=%CommonProgramFiles%\VST3\Resonique.vst3"

if not exist "%SRC%" (
  echo Not built yet: %SRC%
  echo Run build.bat first.
  exit /b 1
)

echo Installing to %DST%  (close your DAW first)
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-Command','New-Item -ItemType Directory -Force ''%CommonProgramFiles%\VST3'' | Out-Null; Remove-Item -Recurse -Force ''%DST%'' -ErrorAction SilentlyContinue; Copy-Item -Recurse -Force ''%SRC%'' ''%DST%'''"
if exist "%DST%\Contents\x86_64-win\Resonique.vst3" (echo Installed. Rescan plugins in your DAW.) else (echo Install failed or was cancelled.)

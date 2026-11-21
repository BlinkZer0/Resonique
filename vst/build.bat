@echo off
rem ==========================================================================
rem  Builds Resonique VST3 + Standalone (Release) and the test tool.
rem  Usage:  build.bat [Debug|Release]
rem  Build output and the JUCE download go to %LOCALAPPDATA%\ResoniqueVST
rem  so OneDrive doesn't try to sync ~1 GB of intermediate files.
rem  An existing JUCE 8.0.17 download from a sibling project is reused.
rem ==========================================================================
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "WORK=%LOCALAPPDATA%\ResoniqueVST"
set "BUILD=%WORK%\build"

rem --- find CMake (PATH first, then the copy bundled with Visual Studio) ---
set "CMAKE=cmake"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

rem --- git is needed to fetch JUCE ---
where git >nul 2>nul
if errorlevel 1 if exist "%ProgramFiles%\Git\cmd\git.exe" set "PATH=%ProgramFiles%\Git\cmd;%PATH%"

rem --- reuse an existing JUCE checkout instead of downloading again ---
set "JUCE_ARG="
if not exist "%WORK%\deps\juce-src\CMakeLists.txt" (
  for %%p in (EPianoVST EightBallVST TheHamVST NoStringsVST BrassKnucklesVST IWubYouVST) do (
    if not defined JUCE_ARG if exist "%LOCALAPPDATA%\%%p\deps\juce-src\CMakeLists.txt" set "JUCE_ARG=-DRESONIQUE_JUCE_DIR=%LOCALAPPDATA%\%%p\deps\juce-src"
  )
)

echo [Resonique] configuring...
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -DFETCHCONTENT_BASE_DIR="%WORK%\deps" %JUCE_ARG% || goto :fail

echo [Resonique] building %CONFIG%...
"%CMAKE%" --build "%BUILD%" --config %CONFIG% --target ResoniqueVST_VST3 ResoniqueVST_Standalone ResoniqueTests --parallel || goto :fail

echo.
echo [Resonique] Done.
echo   VST3       : %BUILD%\ResoniqueVST_artefacts\%CONFIG%\VST3\Resonique.vst3
echo   Standalone : %BUILD%\ResoniqueVST_artefacts\%CONFIG%\Standalone\Resonique.exe
echo   Tests      : %BUILD%\ResoniqueTests_artefacts\%CONFIG%\ResoniqueTests.exe
echo   Run install.bat to copy the VST3 into C:\Program Files\Common Files\VST3
exit /b 0

:fail
echo [Resonique] BUILD FAILED
exit /b 1

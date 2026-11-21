@echo off
rem ===========================================================================
rem  Builds the Android app: web build first (vst\webhost\build-web.ps1), then
rem  the signed release App Bundle (Google Play) and APK (sideloading).
rem    build.bat            release .aab + .apk
rem    build.bat debug      debug .apk only
rem  Needs a JDK 17+ (JAVA_HOME, or Android Studio's / the Android openjdk) and the
rem  Android SDK (local.properties sdk.dir=..., or ANDROID_HOME).
rem ===========================================================================
setlocal
cd /d "%~dp0"

if not defined JAVA_HOME (
  for %%J in ("%ProgramFiles%\Android\Android Studio\jbr" "%ProgramFiles%\Android\openjdk\jdk-21.0.8") do (
    if exist "%%~J\bin\java.exe" set "JAVA_HOME=%%~J"
  )
)
if not defined JAVA_HOME (
  for /d %%J in ("%ProgramFiles%\Android\openjdk\jdk-*") do if exist "%%~J\bin\java.exe" set "JAVA_HOME=%%~J"
)
if not defined JAVA_HOME ( echo JAVA_HOME is not set and no JDK was found. & exit /b 1 )

if not exist local.properties if defined ANDROID_HOME (
  >local.properties echo sdk.dir=%ANDROID_HOME:\=/%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "..\vst\webhost\build-web.ps1" || exit /b 1

if /i "%~1"=="debug" (
  call "%~dp0gradlew.bat" assembleDebug || exit /b 1
  echo.
  echo APK: app\build\outputs\apk\debug\app-debug.apk
  exit /b 0
)

if not exist keystore.properties (
  echo keystore.properties is missing: release builds would be unsigned. See README.md, "Signing".
  exit /b 1
)
call "%~dp0gradlew.bat" bundleRelease assembleRelease || exit /b 1
echo.
echo Play upload: app\build\outputs\bundle\release\app-release.aab
echo Sideload:    app\build\outputs\apk\release\app-release.apk

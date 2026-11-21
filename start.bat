@echo off
rem Serves the web build at http://localhost:8080 (see serve.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0serve.ps1" %*

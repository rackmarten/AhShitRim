@echo off
rem Checks both games, starts San Andreas minimized, then Skyrim through SKSE. See AhShitRim.ps1.
rem Windows PowerShell rebuilds its own module path; one inherited from PowerShell 7 breaks it.
set PSModulePath=
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AhShitRim.ps1" %*
if errorlevel 1 pause

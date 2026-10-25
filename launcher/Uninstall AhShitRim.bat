@echo off
rem Removes the Skyrim part the launcher installed, AhShitRim.ini and San Andreas's AhShitRim.asi (AhShitRim.ps1 -Uninstall).
rem Windows PowerShell rebuilds its own module path; one inherited from PowerShell 7 breaks it.
set PSModulePath=
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AhShitRim.ps1" -Uninstall
if errorlevel 1 pause

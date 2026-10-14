@echo off
rem Runs uninstall.ps1 as Administrator, bypassing the execution policy for this run only.
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0uninstall.ps1\"'"

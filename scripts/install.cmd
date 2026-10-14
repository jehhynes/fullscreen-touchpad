@echo off
rem Runs install.ps1 as Administrator, bypassing the execution policy for this run only
rem (the system policy is left unchanged). Pass -StartWithWindows to launch at sign-in.
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File \"%~dp0install.ps1\" %*'"

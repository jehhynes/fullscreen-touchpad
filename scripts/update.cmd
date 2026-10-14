@echo off
rem Deploys the current build (sign, copy to Program Files, restart). Asks for admin rights.
rem Requires a one-time scripts\install.cmd. Waits for completion; exit code 0 means success.
powershell -NoProfile -Command "$p = Start-Process powershell -Verb RunAs -Wait -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \"%~dp0deploy.ps1\"'; exit $p.ExitCode"
if errorlevel 1 (
  echo Deploy failed. See build\deploy.log
  exit /b 1
)
echo Deployed.

@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run Benchmark.ps1"
echo.
if errorlevel 1 (
  echo Benchmark or upload failed. Check the message above.
) else (
  echo Done. Results are available at https://marks.seven-labs.com
)
pause

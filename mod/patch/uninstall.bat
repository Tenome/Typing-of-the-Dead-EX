@echo off
setlocal
cd /d "%~dp0"
if not exist "Common_orig.dll" (
  echo The patch is not installed.
  pause
  exit /b 0
)
del "Common.dll"
move "Common_orig.dll" "Common.dll" >nul
echo The patch is removed and the original Common.dll is back.
echo todmod.ini, todmod_config.exe, the todmod folder and your word packs in dict\ were kept; delete them if you like.
pause

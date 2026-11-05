@echo off
setlocal
set "GAME=%~dp0..\EX Installation\TODEX"
if not exist "%GAME%\Common_orig.dll" (
  echo Nothing to uninstall.
  pause
  exit /b 0
)
del "%GAME%\Common.dll"
move "%GAME%\Common_orig.dll" "%GAME%\Common.dll" >nul
if exist "%GAME%\todmod_config.exe" del "%GAME%\todmod_config.exe"
if exist "%GAME%\play_two_copies.bat" del "%GAME%\play_two_copies.bat"
if exist "%GAME%\online_play.txt" del "%GAME%\online_play.txt"
if exist "%GAME%\gameplay_options.txt" del "%GAME%\gameplay_options.txt"
echo todmod removed; the original Common.dll is restored. todmod.ini and dict\ are kept.
pause

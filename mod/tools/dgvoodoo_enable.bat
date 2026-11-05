@echo off
setlocal
set "GAME=%~dp0..\..\EX Installation\TODEX"
set "PARKED=%GAME%\dgVoodoo_disabled"
for %%F in (DDraw.dll D3DImm.dll) do (
  if exist "%PARKED%\%%F" move /y "%PARKED%\%%F" "%GAME%\%%F" >nul
)
if exist "%PARKED%" rmdir "%PARKED%" 2>nul
echo dgVoodoo DDraw.dll/D3DImm.dll restored.

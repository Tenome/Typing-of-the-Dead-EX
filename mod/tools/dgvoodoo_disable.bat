@echo off
setlocal
set "GAME=%~dp0..\..\EX Installation\TODEX"
set "PARKED=%GAME%\dgVoodoo_disabled"
if not exist "%PARKED%" mkdir "%PARKED%"
for %%F in (DDraw.dll D3DImm.dll) do (
  if exist "%GAME%\%%F" move /y "%GAME%\%%F" "%PARKED%\%%F" >nul
)
echo dgVoodoo DDraw.dll/D3DImm.dll moved to "%PARKED%". Run dgvoodoo_enable.bat to restore.

@echo off
setlocal
set "BASE=%~dp0.."
set "RE=%~dp0"
set "GAME=%BASE%\EX Installation\TODEX"
set "GHIDRA=%BASE%\ghidra_12.2_PUBLIC"

where java >nul 2>nul || (
  echo Java was not found on PATH. Ghidra 12.2 needs JDK 21 or newer ^(e.g. Eclipse Temurin 21^).
  echo If it is installed, Ghidra will ask for its folder below.
)

if not exist "%RE%bin" mkdir "%RE%bin"
copy /y "%GAME%\Tod.exe.bak"       "%RE%bin\Tod.exe"       >nul
copy /y "%GAME%\HglCore.dll.orig"  "%RE%bin\HglCore.dll"   >nul
for %%F in (HglHab.dll HglNa.dll Table.dll Common.dll RfuDrv.dll Configurater.exe) do copy /y "%GAME%\%%F" "%RE%bin\%%F" >nul

if not exist "%RE%project" mkdir "%RE%project"
if not exist "%RE%export" mkdir "%RE%export"

call "%GHIDRA%\support\analyzeHeadless.bat" "%RE%project" TODEX ^
  -import "%RE%bin\*" -overwrite ^
  -scriptPath "%RE%scripts" ^
  -postScript ExportAll.java "%RE%export" ^
  -log "%RE%ghidra.log"

echo.
echo Done. Results are in "%RE%export". You can close this window.
pause

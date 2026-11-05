@echo off
rem Usage: decompile_at.bat <program, e.g. Tod.exe> <output file> <hex address>...
rem Decompiles (and creates, if needed) functions at the addresses; the project is not saved (-readOnly).
setlocal
set "RE=%~dp0"
set "GHIDRA=%~dp0..\ghidra_12.2_PUBLIC"
set "PROGRAM=%~1"
set "OUT=%~2"
shift
shift
set "ADDRESSES="
:collect
if "%~1"=="" goto run
set "ADDRESSES=%ADDRESSES% %~1"
shift
goto collect
:run
call "%GHIDRA%\support\analyzeHeadless.bat" "%RE%project" TODEX -process "%PROGRAM%" -readOnly -noanalysis ^
  -scriptPath "%RE%scripts" -postScript DecompileAt.java "%OUT%" %ADDRESSES% -log "%RE%ghidra_decompile_at.log"

@echo off
setlocal

rem ================= EDIT THESE =================
rem No trailing backslash is needed on folders (it is removed if present).
set "PYTHON=python"
set "TOOLS_DIR=H:\Games\The House - Typing of the Dead COLLECTION"
set "ENGLISH_WORD_DIR=E:\Claude Typing of the Dead\The Typing of The Dead\word"
set "JAPANESE_WORD_HAB=H:\Games\The House - Typing of the Dead COLLECTION\word.HAB"
set "JAPANESE_EXE=E:\Claude Typing of the Dead\EX Installation\TODEX\Tod.exe"
set "WORK_DIR=H:\Games\The House - Typing of the Dead COLLECTION\out"
set "OUTPUT_HAB=H:\Games\The House - Typing of the Dead COLLECTION\word_english.HAB"
rem Longest line (in full-width characters) allowed. 24 is the Japanese game's longest. 0 = no limit.
set "MAX_DISPLAY=24"
rem Leave empty for full-width text. Set to --halfwidth only after the exe is patched for single-byte text.
set "HALFWIDTH="
rem ==============================================

call :strip TOOLS_DIR
call :strip ENGLISH_WORD_DIR
call :strip WORK_DIR

if not exist "%TOOLS_DIR%\hab_tool.py" goto :missing_tools
if not exist "%TOOLS_DIR%\word_tool.py" goto :missing_tools
if not exist "%TOOLS_DIR%\transplant_tool.py" goto :missing_tools
if not exist "%ENGLISH_WORD_DIR%" goto :missing_english
if not exist "%JAPANESE_WORD_HAB%" goto :missing_hab

set "IMPORT_EXTRA="
if defined HALFWIDTH set "IMPORT_EXTRA=--halfwidth-display"

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"

if exist "%TOOLS_DIR%\romaji_table.json" goto :have_romaji
echo Building romaji_table.json from the Japanese exe...
"%PYTHON%" "%TOOLS_DIR%\hab_tool.py" romaji-table "%JAPANESE_EXE%"
if errorlevel 1 goto :fail
:have_romaji

echo.
echo [1/4] Extracting the Japanese word.HAB...
if exist "%WORK_DIR%\jp_words" rmdir /s /q "%WORK_DIR%\jp_words"
"%PYTHON%" "%TOOLS_DIR%\hab_tool.py" extract "%JAPANESE_WORD_HAB%" "%WORK_DIR%\jp_words"
if errorlevel 1 goto :fail

echo.
echo [2/4] Converting the English word files...
"%PYTHON%" "%TOOLS_DIR%\transplant_tool.py" convert "%ENGLISH_WORD_DIR%" "%WORK_DIR%\jp_words" "%WORK_DIR%\patch.tsv" --max-display %MAX_DISPLAY% %HALFWIDTH%
if errorlevel 1 goto :fail

echo.
echo [3/4] Importing into the Japanese word files...
"%PYTHON%" "%TOOLS_DIR%\hab_tool.py" words-import-all "%WORK_DIR%\patch.tsv" "%WORK_DIR%\jp_words" %IMPORT_EXTRA%
if errorlevel 1 goto :fail

echo.
echo [4/4] Packing the new archive...
"%PYTHON%" "%TOOLS_DIR%\hab_tool.py" pack "%WORK_DIR%\jp_words" "%OUTPUT_HAB%"
if errorlevel 1 goto :fail

echo.
echo Done. Back up your original word.HAB, then copy this over it:
echo   %OUTPUT_HAB%
goto :end

:strip
call set "_value=%%%1%%"
if "%_value:~-1%"=="\" set "%1=%_value:~0,-1%"
exit /b

:missing_tools
echo ERROR: hab_tool.py, word_tool.py and transplant_tool.py must all be in TOOLS_DIR:
echo   %TOOLS_DIR%
goto :fail

:missing_english
echo ERROR: ENGLISH_WORD_DIR was not found:
echo   %ENGLISH_WORD_DIR%
goto :fail

:missing_hab
echo ERROR: JAPANESE_WORD_HAB was not found:
echo   %JAPANESE_WORD_HAB%
goto :fail

:fail
echo.
echo FAILED - see the message above.
pause
exit /b 1

:end
pause
endlocal

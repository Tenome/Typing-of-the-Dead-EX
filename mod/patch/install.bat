@echo off
setlocal
cd /d "%~dp0"
if not exist "Tod.exe" (
  echo Tod.exe was not found. Extract this patch into your Typing of the Dead EX folder, next to Tod.exe.
  pause
  exit /b 1
)
if not exist "Common_orig.dll" (
  for %%A in ("Common.dll") do if not "%%~zA"=="4608" (
    echo Common.dll is not the game's original file and Common_orig.dll is missing. Nothing was changed.
    pause
    exit /b 1
  )
  move "Common.dll" "Common_orig.dll" >nul
)
copy /y "todmod\Common.dll" "Common.dll" >nul
if not exist "dict" mkdir "dict"
copy /y "todmod\word_packs.txt" "dict\README.txt" >nul
if not exist "textures" mkdir "textures"
copy /y "todmod\texture_packs.txt" "textures\README.txt" >nul
if not exist "music" mkdir "music"
copy /y "todmod\music_packs.txt" "music\README.txt" >nul
if not exist "models" mkdir "models"
copy /y "todmod\model_packs.txt" "models\README.txt" >nul
if not exist "subtitles" mkdir "subtitles"
copy /y "todmod\subtitles.txt" "subtitles\README.txt" >nul
if not exist "voices" mkdir "voices"
copy /y "todmod\voice_packs.txt" "voices\README.txt" >nul
echo The Modern Community Patch is installed.
echo Run todmod_config.exe to choose resolution, word packs and other options.
pause

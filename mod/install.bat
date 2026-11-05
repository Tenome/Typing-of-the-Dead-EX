@echo off
setlocal
set "GAME=%~dp0..\EX Installation\TODEX"
if not exist "%GAME%\Tod.exe" (
  echo Tod.exe not found in "%GAME%".
  pause
  exit /b 1
)
if not exist "%GAME%\Common_orig.dll" (
  for %%A in ("%GAME%\Common.dll") do if not "%%~zA"=="4608" (
    echo "%GAME%\Common.dll" is not the original 4608-byte file and Common_orig.dll is missing. Stopping.
    pause
    exit /b 1
  )
  move "%GAME%\Common.dll" "%GAME%\Common_orig.dll" >nul
)
copy /y "%~dp0build\Common.dll" "%GAME%\Common.dll" >nul
copy /y "%~dp0build\todmod_config.exe" "%GAME%\todmod_config.exe" >nul
copy /y "%~dp0tools\play_two_copies.bat" "%GAME%\play_two_copies.bat" >nul
copy /y "%~dp0docs\online_play.txt" "%GAME%\online_play.txt" >nul
copy /y "%~dp0docs\gameplay_options.txt" "%GAME%\gameplay_options.txt" >nul
if not exist "%GAME%\dict" mkdir "%GAME%\dict"
copy /y "%~dp0docs\word_packs.txt" "%GAME%\dict\README.txt" >nul
if not exist "%GAME%\textures" mkdir "%GAME%\textures"
copy /y "%~dp0docs\texture_packs.txt" "%GAME%\textures\README.txt" >nul
if not exist "%GAME%\music" mkdir "%GAME%\music"
copy /y "%~dp0docs\music_packs.txt" "%GAME%\music\README.txt" >nul
if not exist "%GAME%\models" mkdir "%GAME%\models"
copy /y "%~dp0docs\model_packs.txt" "%GAME%\models\README.txt" >nul
if not exist "%GAME%\subtitles" mkdir "%GAME%\subtitles"
copy /y "%~dp0docs\subtitles.txt" "%GAME%\subtitles\README.txt" >nul
if not exist "%GAME%\tools\hotd" mkdir "%GAME%\tools\hotd"
for %%F in (obj_to_character gltf_to_character model_formats texture_formats character_kinds) do copy /y "%~dp0tools\%%F.py" "%GAME%\tools\" >nul
for %%F in (rbh vmc exe_view skeletons pose assemble to_tod) do copy /y "%~dp0..\hotd\tools\%%F.py" "%GAME%\tools\hotd\" >nul
if not exist "%GAME%\voices" mkdir "%GAME%\voices"
copy /y "%~dp0docs\voice_packs.txt" "%GAME%\voices\README.txt" >nul
echo todmod installed. Run todmod_config.exe for settings; the log is "%GAME%\todmod.log".
pause

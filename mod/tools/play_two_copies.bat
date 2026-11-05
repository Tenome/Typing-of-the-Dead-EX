@echo off
rem Starts two copies of the game kept in lockstep (todmod [online]): the first is the host (player 1),
rem the second joins (player 2). Set windowed mode in todmod_config.exe first. Each copy reads the keyboard
rem only while its window is in front. Logs: todmod.log (host) and todmod_join.log (join).
setlocal
cd /d "%~dp0"
set TODMOD_ONLINE=host
start "" Tod.exe
timeout /t 2 /nobreak >nul
set TODMOD_ONLINE=join
start "" Tod.exe

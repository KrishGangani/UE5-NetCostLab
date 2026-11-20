@echo off
rem P05 NetCostLab - smoke test: start the game, load the default map, quit.
rem Usage: smoke_game.bat [extra args...]    uncooked: the editor binary in -game mode, no GPU (-nullrhi), no cook needed
rem        smoke_game.bat packaged           the packaged Development build (run package.bat first), headless (-nullrhi),
rem                                          as the benchmark clients run. It starts the REAL game exe
rem                                          (Build\Windows\NetCostLab\Binaries\Win64), not the bootstrap in Build\Windows,
rem                                          so the exit code is the game's own.
rem Both: no sound, no Insights auto-trace (-traceautostart=0), and the log goes to a fixed file.
rem The multi-process network checks (editor -server + headless clients) are Tools\probe_net.py.
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\NetCostLab\NetCostLab.uproject"
if /I "%~1"=="packaged" goto packaged

set "LOG=%~dp0..\NetCostLab\Saved\Logs\SmokeGame.log"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -game -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%" %*
exit /b %ERRORLEVEL%

:packaged
set "LOG=%~dp0..\Build\SmokePackaged.log"
"%~dp0..\Build\Windows\NetCostLab\Binaries\Win64\NetCostLab.exe" -nullrhi -nosound -unattended -traceautostart=0 -ExecCmds="quit" -log -abslog="%LOG%"
exit /b %ERRORLEVEL%

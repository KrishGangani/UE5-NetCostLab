@echo off
rem P05 NetCostLab - package a standalone Win64 game build (BuildCookRun: build, cook, stage, pak, archive).
rem Usage: package.bat [Configuration]   Configuration: Development (default) | Shipping (no CSV profiler; not used)
rem The packaged game is used for the headless CLIENTS only. The server is the editor binary with -server, because the
rem launcher engine cannot build a Server target and its packaged games have no push model (01b_Research_Findings.md #1-#3).
rem Output: ..\Build\Windows\NetCostLab\Binaries\Win64\NetCostLab.exe   (Build\ is git-ignored)
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\NetCostLab\NetCostLab.uproject"
set "ARCHIVE=%~dp0..\Build"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory="%ARCHIVE%"
exit /b %ERRORLEVEL%

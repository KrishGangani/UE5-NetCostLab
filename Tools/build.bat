@echo off
rem P05 NetCostLab - build one target from the command line (reproducible; no IDE needed).
rem Usage: build.bat [Target] [Configuration]
rem   Target:        NetCostLabEditor (default) | NetCostLab
rem   Configuration: Development (default) | DebugGame | Shipping
rem There is no Server target: the launcher engine cannot build one (01b_Research_Findings.md #1).
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\NetCostLab\NetCostLab.uproject"
set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=NetCostLabEditor"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Development"

call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE
exit /b %ERRORLEVEL%

@echo off
rem P05 NetCostLab - run the automation tests headless in the editor binary and exit with their result.
rem Usage: run_tests.bat [Filter] [LogPath]   (default filter: NetCostLab, i.e. every test of this project;
rem        LogPath is relative to this Tools folder; default ..\NetCostLab\Saved\Logs\Tests.log. check_job.py
rem        passes a log inside its batch folder. Relative, because "cmd /c" mangles a command line that has two
rem        quoted arguments with spaces, and the repository path contains a space.)
rem Exit code 0 = all passed; non-zero = a failure. Needs the Development editor target (build.bat).
rem Run it through the machine-wide queue (tools\ue_queue.py), never directly.
setlocal
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "PROJECT=%~dp0..\NetCostLab\NetCostLab.uproject"
set "FILTER=%~1"
if "%FILTER%"=="" set "FILTER=NetCostLab"
set "REPORT=%~dp0..\NetCostLab\Saved\Automation\Reports"
set "LOG=%~dp0..\NetCostLab\Saved\Logs\Tests.log"
if not "%~2"=="" set "LOG=%~dp0%~2"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJECT%" -ExecCmds="Automation RunTests %FILTER%;Quit" -unattended -nullrhi -nosound -nosplash -nopause -traceautostart=0 -log -abslog="%LOG%" -ReportExportPath="%REPORT%"
exit /b %ERRORLEVEL%

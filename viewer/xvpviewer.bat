@echo off
REM
REM xvpviewer wrapper script to run Java VNC Viewer for XVP
REM
REM Copyright (c) 2009 Colin Dean.
REM
REM This program is free software; you can redistribute it and/or modify it
REM under the terms of the GNU General Public License as published by the
REM Free Software Foundation; either version 2 of the License, or (at your
REM option) any later version.
REM
REM This program is distributed in the hope that it will be useful, but
REM WITHOUT ANY WARRANTY; without even the implied warranty of
REM MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
REM General Public License for more details.
REM

setlocal

IF "%~1" == "" GOTO usage

IF "%~1" == "-vm" GOTO vm
IF NOT "%~2" == "" GOTO usage

SET _xvp_vm=
SET _xvp_arg=%~1
GOTO port

:vm
IF NOT "%~4" == "" GOTO usage
SET _xvp_vm=%~2
SET _xvp_arg=%~3

IF "%~2" == "" GOTO USAGE
IF "%~3" == "" GOTO USAGE

:port

SET _xvp_port=%_xvp_arg:*:=%
CALL SET _xvp_host=%%_xvp_arg::%_xvp_port%=%%
IF "%_xvp_port%" == "%_xvp_host%" SET _xvp_port=

SET _xvp_colon2=%_xvp_port:~0,1%
IF "%_xvp_colon2%" == ":" SET _xvp_port=%_xvp_port:~1%


IF "%_xvp_host%" == "" SET _xvp_host=localhost
IF "%_xvp_port%" == "" SET _xvp_port=0
IF "%_xvp_colon2%" == ":" GOTO parsed

SET /A _xvp_port=%_xvp_port%+5900

:parsed

IF "%_xvp_vm%" == "" GOTO novm
java -jar VncViewer.jar HOST %_xvp_host% PORT %_xvp_port% VM "%_xvp_vm%"
GOTO end

:novm
java -jar VncViewer.jar HOST %_xvp_host% PORT %_xvp_port%
GOTO end

:usage

ECHO Usage: xvpviewer [ -vm [pool:]vm ] host[:display]
ECHO        xvpviewer [ -vm [pool:]vm ] host[::port]

:end

@echo off
setlocal
"%~dp0bin\music.exe" %*
exit /b %errorlevel%

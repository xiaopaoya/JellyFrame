@echo off
setlocal
python "%~dp0jellyframe_device.py" %*
exit /b %ERRORLEVEL%

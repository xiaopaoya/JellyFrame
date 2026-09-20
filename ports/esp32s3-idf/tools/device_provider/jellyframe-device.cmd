@echo off
setlocal
if defined JELLYFRAME_PYTHON (
    "%JELLYFRAME_PYTHON%" "%~dp0jellyframe_device.py" %*
) else (
    python "%~dp0jellyframe_device.py" %*
)
exit /b %ERRORLEVEL%

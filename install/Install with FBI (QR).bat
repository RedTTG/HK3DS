@echo off
cd /d "%~dp0"
python install_with_fbi.py
if errorlevel 1 py install_with_fbi.py
pause

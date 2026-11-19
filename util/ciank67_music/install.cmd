@echo off
cd /d "%~dp0"
py -3.12 -m venv .venv
if errorlevel 1 goto failed
.venv\Scripts\python.exe -m pip install -r requirements.txt
if errorlevel 1 goto failed
echo Installation complete. Connect USB and run start-usb.cmd.
pause
exit /b 0
:failed
echo Installation failed. Install Python 3.12 64-bit with the Python Launcher, then retry.
pause
exit /b 1

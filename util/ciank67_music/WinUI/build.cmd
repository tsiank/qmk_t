@echo off
cd /d "%~dp0"
where dotnet >nul 2>&1
if errorlevel 1 (
    echo Install the .NET 10 SDK first.
    pause
    exit /b 1
)
dotnet run --project Ciank67.Music.Tests -c Release
if errorlevel 1 goto failed
dotnet run --project Ciank67.Midi.Tests -c Release
if errorlevel 1 goto failed
dotnet publish Ciank67.Music.App -c Release -r win-x64 --self-contained false -o publish\win-x64
if errorlevel 1 goto failed
powershell -NoProfile -ExecutionPolicy Bypass -File verify-publish.ps1 -PublishDirectory publish\win-x64
if errorlevel 1 goto failed
echo Ready: publish\win-x64\Qmknrf5.exe
pause
exit /b 0
:failed
echo Build failed. Check the messages above.
pause
exit /b 1

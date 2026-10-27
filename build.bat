@echo off
cd /d "%~dp0"

if /I "%~1"=="dev" (
    cmake --preset x64-dev || exit /b 1
    cmake --build --preset x64-dev
    exit /b
)

cmake --preset x64-release || exit /b 1
cmake --build --preset x64-release || exit /b 1

set "OUT=build\x64-release\Release"
for /f "delims=" %%v in ('powershell -NoProfile -Command "(Get-Item '%OUT%\thread-sim.exe').VersionInfo.FileVersion"') do set "VERSION=%%v"
set "ZIP=build\dist\thread-sim-Portable-%VERSION%-x64.zip"
if exist "%OUT%\thread-sim.exe.sig" del "%OUT%\thread-sim.exe.sig"
if exist "%ZIP%.sig" del "%ZIP%.sig"
powershell -NoProfile -ExecutionPolicy Bypass -File sign.ps1 "%OUT%\thread-sim.exe" || exit /b 1
if exist "%OUT%\examples" rmdir /s /q "%OUT%\examples"
xcopy /i /q /y assets\examples\*.jsonc "%OUT%\examples\" >nul || exit /b 1
set "FILES=thread-sim.exe examples"
if exist "%OUT%\thread-sim.exe.sig" set "FILES=thread-sim.exe thread-sim.exe.sig examples"
if not exist build\dist mkdir build\dist
tar -a -cf "%ZIP%" -C "%OUT%" %FILES% || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File sign.ps1 "%ZIP%"
exit /b %errorlevel%

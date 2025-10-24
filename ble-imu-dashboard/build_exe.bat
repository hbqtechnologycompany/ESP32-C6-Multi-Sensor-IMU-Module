@echo off
REM ESP32-C6 IMU Dashboard - Build to EXE
REM This script creates a standalone .exe file using PyInstaller

echo.
echo ========================================
echo  Building ESP32-C6 IMU Dashboard EXE
echo ========================================
echo.

REM Activate virtual environment
echo Activating virtual environment...
call .\.venv\Scripts\activate.bat

REM Install PyInstaller if not present
echo Checking PyInstaller...
pip show pyinstaller >nul 2>&1
if errorlevel 1 (
    echo Installing PyInstaller...
    pip install pyinstaller
)

REM Clean previous builds
echo.
echo Cleaning previous builds...
if exist build rmdir /s /q build
if exist dist rmdir /s /q dist
if exist *.spec del /q *.spec

REM Build executable
echo.
echo Building executable...
echo This may take a few minutes...
echo.

pyinstaller --name="ESP32-IMU-Dashboard" ^
    --onefile ^
    --windowed ^
    --icon=NONE ^
    --add-data="ui/style_dark.qss;ui" ^
    --add-data="ui/style_light.qss;ui" ^
    --add-data="ui/imgs/logo-HBQ-1.png;ui/imgs" ^
    --hidden-import="PyQt6.QtCore" ^
    --hidden-import="PyQt6.QtGui" ^
    --hidden-import="PyQt6.QtWidgets" ^
    --hidden-import="bleak" ^
    --hidden-import="pyqtgraph" ^
    --hidden-import="numpy" ^
    --collect-all="bleak" ^
    --collect-all="pyqtgraph" ^
    main.py

REM Check if build succeeded
if exist "dist\ESP32-IMU-Dashboard.exe" (
    echo.
    echo ========================================
    echo  Build Successful!
    echo ========================================
    echo.
    echo Executable created at:
    echo    dist\ESP32-IMU-Dashboard.exe
    echo.
    echo You can now distribute this .exe file!
    echo No Python installation needed on target PC.
) else (
    echo.
    echo Build failed! Check errors above.
    pause
    exit /b 1
)

call deactivate
pause

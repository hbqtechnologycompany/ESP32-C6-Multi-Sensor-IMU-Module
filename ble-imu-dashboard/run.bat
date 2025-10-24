@echo off
REM ESP32-C6 Multi-Sensor IMU Dashboard Launcher
REM Batch script for Windows

echo.
echo ========================================
echo  ESP32-C6 IMU Dashboard Launcher
echo ========================================
echo.

REM Check if .venv exists
if not exist ".\.venv\Scripts\activate.bat" (
    echo [ERROR] Virtual environment not found!
    echo Creating .venv...
    python -m venv .venv
    
    echo Installing dependencies...
    call .\.venv\Scripts\activate.bat
    pip install -r requirements.txt
)

REM Activate virtual environment
echo Activating virtual environment...
call .\.venv\Scripts\activate.bat

REM Run the application
echo Launching dashboard...
python main.py

REM Deactivate when done
call deactivate

pause

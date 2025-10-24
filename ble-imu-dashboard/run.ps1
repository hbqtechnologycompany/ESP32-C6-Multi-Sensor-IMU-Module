# ESP32-C6 Multi-Sensor IMU Dashboard Launcher
# PowerShell script to activate venv and run the application

Write-Host "🚀 Starting ESP32-C6 IMU Dashboard..." -ForegroundColor Cyan

# Check if .venv exists
if (-not (Test-Path ".\.venv\Scripts\Activate.ps1")) {
    Write-Host "❌ Virtual environment not found!" -ForegroundColor Red
    Write-Host "Creating .venv..." -ForegroundColor Yellow
    python -m venv .venv
    
    Write-Host "Installing dependencies..." -ForegroundColor Yellow
    .\.venv\Scripts\Activate.ps1
    pip install -r requirements.txt
}

# Activate virtual environment
Write-Host "🔧 Activating virtual environment..." -ForegroundColor Green
.\.venv\Scripts\Activate.ps1

# Run the application
Write-Host "📊 Launching dashboard..." -ForegroundColor Green
python main.py

# Deactivate when done
deactivate

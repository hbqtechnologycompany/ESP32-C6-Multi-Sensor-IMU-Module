# ESP32-C6 IMU Dashboard - Setup Script
# Run this once to set up the development environment

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " ESP32-C6 IMU Dashboard - Initial Setup" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check Python version
Write-Host "🔍 Checking Python version..." -ForegroundColor Yellow
$pythonVersion = python --version 2>&1
Write-Host "   Found: $pythonVersion" -ForegroundColor Green

if ($pythonVersion -notmatch "Python 3\.(1[1-9]|[2-9]\d)") {
    Write-Host "❌ Python 3.11+ is required!" -ForegroundColor Red
    Write-Host "   Download from: https://www.python.org/downloads/" -ForegroundColor Yellow
    exit 1
}

# Create virtual environment
Write-Host ""
Write-Host "📦 Creating virtual environment (.venv)..." -ForegroundColor Yellow
python -m venv .venv

# Activate venv
Write-Host ""
Write-Host "🔧 Activating virtual environment..." -ForegroundColor Yellow
.\.venv\Scripts\Activate.ps1

# Upgrade pip
Write-Host ""
Write-Host "⬆️  Upgrading pip..." -ForegroundColor Yellow
python -m pip install --upgrade pip

# Install dependencies
Write-Host ""
Write-Host "📥 Installing dependencies from requirements.txt..." -ForegroundColor Yellow
pip install -r requirements.txt

# Verify installation
Write-Host ""
Write-Host "✅ Verifying installation..." -ForegroundColor Yellow
python -c "import PyQt6, bleak, pyqtgraph, numpy; print('   All packages imported successfully!')"

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host " ✅ Setup Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "To run the dashboard:" -ForegroundColor Cyan
Write-Host "   1. PowerShell: .\run.ps1" -ForegroundColor White
Write-Host "   2. CMD:        run.bat" -ForegroundColor White
Write-Host "   3. Manual:     .\.venv\Scripts\Activate.ps1 ; python main.py" -ForegroundColor White
Write-Host ""

deactivate

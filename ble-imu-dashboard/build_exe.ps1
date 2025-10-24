# ESP32-C6 IMU Dashboard - Build to EXE
# This script creates a standalone .exe file using PyInstaller

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Building ESP32-C6 IMU Dashboard EXE" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Activate virtual environment
Write-Host "🔧 Activating virtual environment..." -ForegroundColor Yellow
.\.venv\Scripts\Activate.ps1

# Install PyInstaller if not present
Write-Host "📦 Checking PyInstaller..." -ForegroundColor Yellow
pip show pyinstaller > $null 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "   Installing PyInstaller..." -ForegroundColor Green
    pip install pyinstaller
}

# Clean previous builds
Write-Host ""
Write-Host "🧹 Cleaning previous builds..." -ForegroundColor Yellow
if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" }
if (Test-Path "*.spec") { Remove-Item -Force "*.spec" }

# Build executable
Write-Host ""
Write-Host "🔨 Building executable..." -ForegroundColor Green
Write-Host "   This may take a few minutes..." -ForegroundColor Yellow

pyinstaller --name="ESP32-IMU-Dashboard" `
    --onefile `
    --windowed `
    --icon=NONE `
    --add-data="ui/style_dark.qss;ui" `
    --add-data="ui/style_light.qss;ui" `
    --add-data="ui/imgs/logo-HBQ-1.png;ui/imgs" `
    --hidden-import="PyQt6.QtCore" `
    --hidden-import="PyQt6.QtGui" `
    --hidden-import="PyQt6.QtWidgets" `
    --hidden-import="bleak" `
    --hidden-import="pyqtgraph" `
    --hidden-import="numpy" `
    --collect-all="bleak" `
    --collect-all="pyqtgraph" `
    main.py

# Check if build succeeded
if (Test-Path "dist\ESP32-IMU-Dashboard.exe") {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host " ✅ Build Successful!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "📦 Executable created at:" -ForegroundColor Cyan
    Write-Host "   dist\ESP32-IMU-Dashboard.exe" -ForegroundColor White
    Write-Host ""
    
    $exeSize = (Get-Item "dist\ESP32-IMU-Dashboard.exe").Length / 1MB
    Write-Host "   Size: $([math]::Round($exeSize, 2)) MB" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "You can now distribute this .exe file!" -ForegroundColor Green
    Write-Host "No Python installation needed on target PC." -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "❌ Build failed! Check errors above." -ForegroundColor Red
    exit 1
}

deactivate

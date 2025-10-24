# Building Executable Guide

## Quick Build

### PowerShell
```powershell
.\build_exe.ps1
```

### Command Prompt
```cmd
build_exe.bat
```

## Output
The executable will be created at:
```
dist\ESP32-IMU-Dashboard.exe
```

## Distribution
You can copy this single `.exe` file to any Windows PC:
- ✅ No Python installation needed
- ✅ No dependencies to install
- ✅ Just double-click to run
- ⚠️ Requires Windows 10/11 with Bluetooth support

## Build Options

### Using Spec File (Advanced)
```powershell
.\.venv\Scripts\Activate.ps1
pyinstaller ESP32-IMU-Dashboard.spec
```

### Custom Build
```powershell
pyinstaller --name="MyCustomName" `
    --onefile `
    --windowed `
    --icon="path/to/icon.ico" `
    main.py
```

## Build Parameters

| Parameter | Description |
|-----------|-------------|
| `--onefile` | Create single .exe (vs folder) |
| `--windowed` | No console window |
| `--icon` | Add custom icon |
| `--add-data` | Include resource files |
| `--hidden-import` | Force include modules |
| `--collect-all` | Include all submodules |

## Troubleshooting

### Build fails with import errors
Add missing modules to `hiddenimports` in `.spec` file

### Executable size too large (~100MB+)
This is normal for PyQt6 apps. Options:
- Use `--onedir` instead of `--onefile` (smaller, but multiple files)
- Exclude unused libraries with `--exclude-module`

### Antivirus flags executable
Add exclusion in Windows Defender:
```
Settings → Privacy & Security → Windows Security → Virus & threat protection
→ Manage settings → Add exclusion
```

### Bluetooth not working in exe
Ensure you run build on Windows 10/11 with working Bluetooth

## File Size Comparison
- **--onefile**: ~50-100 MB (single .exe)
- **--onedir**: ~150-200 MB (folder with multiple files)

## Notes
- First build takes 2-5 minutes
- Subsequent builds are faster (~1 minute)
- `build/` and `dist/` folders are created automatically
- `.spec` file can be edited for advanced customization

# ColourBot Build Instructions

## Project
- Solution: `BlueFire.sln`
- Main C++ project: `BlueFireColorbot/BlueFireColorbot.vcxproj`
- Output (Release x64): `x64/Release/word.exe`

## Prerequisites
- Windows 10/11
- Visual Studio with **Desktop development with C++**
- MSBuild with C++ targets (`Microsoft.Cpp.*`)
- Windows SDK (10.0 or newer)

## Build From Command Line (Recommended)
Use **Developer PowerShell for Visual Studio** (or any shell where `msbuild` is available):

```powershell
msbuild "BlueFire.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /m /nologo
```

Debug build:

```powershell
msbuild "BlueFire.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo
```

## Build From Visual Studio
1. Open `BlueFire.sln`.
2. Set configuration to `Release` and platform to `x64`.
3. Build the solution (`Build > Build Solution`).

## Runtime Controls
- Use the **Capture ScreenGrab** button in the UI to write a frame to `Test/`.
- Captures are saved as binary PPM files named like `screengrab_YYYYMMDD_HHMMSS_N.ppm`.
- Sorting now includes **GPU Fast Detector** (compute-shader path). If GPU init fails, it automatically falls back to CPU detection.

## Project Structure
- `BlueFireColorbot/main.cpp`: UI, app loop, D3D9 window/rendering, and high-level orchestration.
- `BlueFireColorbot/ConfigIO.h/.cpp`: typed config load/save (`config.txt`).
- `BlueFireColorbot/DetectionCommon.h`: shared math/types and color predicates.
- `BlueFireColorbot/CpuDetector.h/.cpp`: CPU scanning/sorting modes.
- `BlueFireColorbot/GpuColorDetector.h/.cpp`: compute-shader color detection pipeline.

## Notes
- This is a native C++ (`.vcxproj`) solution. `dotnet build` is not the correct tool for this project and can fail with missing `Microsoft.Cpp.Default.props`.
- The project currently builds successfully with 0 warnings in `Debug|x64` and `Release|x64`.

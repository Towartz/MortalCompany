@echo off
cd /d "%~dp0"
echo Building libfbk.so...
"S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29\ndk-build.cmd"
if %errorlevel% equ 0 (
    echo Build succeeded - libs/arm64-v8a/libfbk.so
) else (
    echo Build failed!
)
pause

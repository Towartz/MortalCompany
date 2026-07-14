@echo off
cd /d "%~dp0"
echo Building libfbk.so...
call "S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29\ndk-build.cmd"
if %errorlevel% equ 0 (
    echo Build succeeded - libs/arm64-v8a/libfbk.so
    echo Pushing to device: /storage/emulated/0/_66XZD/apks/libfbk.so
    adb push "libs\arm64-v8a\libfbk.so" "/storage/emulated/0/_66XZD/apks/"
    if %errorlevel% equ 0 (
        echo Push complete.
    ) else (
        echo Push failed! Make sure a device is connected and adb is in PATH.
    )
) else (
    echo Build failed!
)

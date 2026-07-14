@echo off
REM Build only the native libraries (libfbk.so) for armeabi-v7a + arm64-v8a.
setlocal
set ANDROID_NDK_HOME=S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29
set PATH=%ANDROID_NDK_HOME%;%PATH%

cd /d "%~dp0app\src\main\jni" || exit /b 1

call "%ANDROID_NDK_HOME%\ndk-build.cmd" ^
    NDK_PROJECT_PATH=. ^
    APP_BUILD_SCRIPT=./Android.mk ^
    NDK_APPLICATION_MK=./Application.mk ^
    -j%NUMBER_OF_PROCESSORS% ^
    %*

if errorlevel 1 (
    echo [!] ndk-build failed
    exit /b 1
)

echo [+] libs built: app\src\main\jni\libs
endlocal

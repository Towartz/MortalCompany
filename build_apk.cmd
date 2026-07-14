@echo off
REM Build the debug APK (assembles + signs with the project debug.keystore).
setlocal
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot
set ANDROID_HOME=S:\Projects\Android
set ANDROID_SDK_ROOT=S:\Projects\Android
set ANDROID_NDK_HOME=S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29
set PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\cmdline-tools\latest\bin;%ANDROID_HOME%\platform-tools;%ANDROID_NDK_HOME%;%PATH%

cd /d "%~dp0" || exit /b 1

call gradlew.bat assembleDebug %*
if errorlevel 1 (
    echo [!] gradle assembleDebug failed
    exit /b 1
)

echo [+] APK: app\build\outputs\apk\debug\app-debug.apk
endlocal

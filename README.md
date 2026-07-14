# MortalCompany

A mod-menu toolkit for the Android (Unity) build of **Mortal Company / Lethal Company**.
It ships in two menu flavors plus a backup of the second:

| Branch          | Flavor            | What it is                                                        |
|-----------------|-------------------|-------------------------------------------------------------------|
| `main`          | **LGL version**   | Android Studio app module: Java overlay menu + native GL menu.    |
| `imgui`         | **IMGUI version** | Standalone native `libfbk.so` built with [Dear ImGui](https://github.com/ocornut/imgui). |
| `imgui-backup`  | **IMGUI (backup)**| A backup snapshot of the IMGUI project (`cheat_bak/`).            |

Clone a single branch to get just that variant, e.g.:

```bash
git clone -b main  https://github.com/Towartz/MortalCompany.git   # LGL
git clone -b imgui https://github.com/Towartz/MortalCompany.git   # IMGUI
```

---

## Project layout

### `main` — LGL version (`app/`)
A normal Android app that launches the game's Unity activity and injects a native
mod menu. The native library `libfbk.so` is loaded from the Java side:

```java
System.loadLibrary("fbk");   // app/src/main/java/com/android/support/{Launcher,Main}.java
```

- Native source: `app/src/main/jni/` — Dobby, Keystone, KittyMemory, xDL,
  an IL2CPP resolver (`Resolver/`), and the cheat logic (`cheats.cpp`, `esp.cpp`,
  `config.cpp`).
- Java overlay UI: `app/src/main/java/com/android/support/` (`Menu.java`,
  `MainActivity.java`, `Launcher.java`, `Preferences.java`, `CrashHandler.java`).
- On launch, `MainActivity` starts `com.unity3d.player.UnityPlayerActivity` and
  shows the mod menu overlay on top.

### `imgui` / `imgui-backup` — IMGUI version (`cheat/` or `cheat_bak/`)
A self-contained `ndk-build` project that produces `libfbk.so` from an ImGui-based
menu. It pulls in:

- `imgui/imgui-master` — Dear ImGui
- `shadowhook` — inline hook framework
- `Dobby` — hooking
- `KittyMemory` (+ Keystone assembler) — memory read/write/patch
- `And64InlineHook` — ARM64 inline hook
- `il2cpp_src/Il2cpp_Resolver_Android` — IL2CPP API resolver (referenced by
  `cheat/jni/Android.mk` as `../../il2cpp_src/Il2cpp_Resolver_Android`)

Config lives in `cheat.cfg` / `cheat_bak/cheat.cfg` (e.g. `infinite_stamina=1`,
`god_mode=0`, `esp_objects=1`). The shared library is pushed to the game's
native library path on the device.

---

## Requirements

- **Android SDK** (platform-tools, build-tools) — tested with SDK 36.
- **Android NDK r29** (`android-ndk-r29`).
- **JDK 21+** (build scripts were authored against Adoptium JDK 25).
- A rooted/debuggable Android device or emulator running **API 23+**
  (minSdk 23, targetSdk 36), ABIs `arm64-v8a` and `armeabi-v7a`.

Set these before building (the scripts assume them, adjust to your paths):

```bat
set ANDROID_HOME=S:\Projects\Android
set ANDROID_NDK_HOME=S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-25.0.3.9-hotspot
```

---

## Building

### LGL version (`main`)

Build the native library, then the APK:

```bat
build.cmd        :: ndk-build -> app/src/main/jni/libs/{arm64-v8a,armeabi-v7a}/libfbk.so
build_apk.cmd    :: gradlew assembleDebug -> app/build/outputs/apk/debug/app-debug.apk
```

Or from a shell with the env vars set:

```bash
./gradlew assembleDebug
```

### IMGUI version (`imgui` / `imgui-backup`)

```bat
cd cheat            (or cheat_bak)
build.cmd           :: ndk-build -> libs/arm64-v8a/libfbk.so
```

`cheat/build.cmd` additionally `adb push`es the built `.so` to
`/storage/emulated/0/_66XZD/apks/` on a connected device.

---

## What is NOT in this repo

To keep the repository small and fast, the following are **git-ignored** (they are
regeneratable or huge reference dumps and are not compiled into the binary):

- Build artifacts: `*.so`, `*.o`, `*.a`, `build/`, `obj/`, generated `libs/`.
- IL2CPP reverse-engineering dumps in `Dump0/` (`dump.cs`, `il2cpp.h`,
  `il2cpp-functions.h`, `script.json`, `generics_dump.txt`, …).
- Large standalone toolkits kept locally only: `references/`, `il2cpp_src/`
  (except the `Il2cpp_Resolver_Android` dependency used by the IMGUI build),
  `imgui_src/`, and the separate `lglteam-menu/` checkout.

If you need the full IL2CPP dumps or the complete toolchain committed, do it on a
separate branch (Git-LFS recommended for the multi-MB files).

---

## Disclaimer

This project is for **educational and research purposes** only. Modifying or
intercepting a game you do not own may violate its Terms of Service. Use at your
own risk.

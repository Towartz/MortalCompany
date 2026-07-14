# MortalCompany

A mod-menu toolkit for the Android (Unity / IL2CPP) build of **Mortal Company / Lethal Company**.
It ships in **two menu flavors plus a backup of the second**, each on its own branch:

| Branch          | Flavor              | What it is                                                                 |
|-----------------|---------------------|----------------------------------------------------------------------------|
| `main`          | **LGL version**     | Android Studio app module: a Java overlay menu + native GLES menu. Builds an installable `app-debug.apk`. |
| `imgui`         | **IMGUI version**   | Standalone native `libfbk.so` built with [Dear ImGui](https://github.com/ocornut/imgui). |
| `imgui-backup`  | **IMGUI (backup)**  | A backup snapshot of the IMGUI project (`cheat_bak/`).                      |

> The three variants live on **separate branches**, not folders — clone the branch you want.

```bash
git clone -b main    https://github.com/Towartz/MortalCompany.git   # LGL app (APK)
git clone -b imgui   https://github.com/Towartz/MortalCompany.git   # IMGUI native .so
git clone -b imgui-backup https://github.com/Towartz/MortalCompany.git   # IMGUI backup
```

---

## Table of contents

- [Project layout](#project-layout)
  - [`main` — LGL version](#main--lgl-version)
  - [`imgui` / `imgui-backup` — IMGUI version](#imgui--imgui-backup--imgui-version)
- [Features](#features)
- [Requirements](#requirements)
- [Building locally](#building-locally)
  - [LGL version (`main`)](#lgl-version-main)
  - [IMGUI version (`imgui` / `imgui-backup`)](#imgui-version-imgui--imgui-backup)
- [Continuous Integration (GitHub Actions)](#continuous-integration-github-actions)
- [Device installation](#device-installation)
- [Configuration](#configuration)
- [Repository hygiene — what is NOT committed](#repository-hygiene--what-is-not-committed)
- [License](#license)
- [Disclaimer](#disclaimer)

---

## Project layout

### `main` — LGL version (`app/`)

A normal Android app that launches the game's Unity activity and injects a native mod menu.
The native library `libfbk.so` is loaded from the Java side:

```java
System.loadLibrary("fbk");   // app/src/main/java/com/android/support/{Launcher,Main}.java
```

- **Native source** — `app/src/main/jni/`
  - `Dobby/` — function hooking
  - `KittyMemory/` + `Deps/Keystone/` — memory read/write/patch + Keystone assembler
  - `xDL/` — Android dynamic-linker helper
  - `Resolver/` — IL2CPP API resolver (self-contained, no `il2cpp_src` dependency)
  - Cheat logic: `cheats.cpp`, `esp.cpp`, `config.cpp`, `Main.cpp`
  - Prebuilt link libraries are committed: `Dobby/**/libdobby.a`, `KittyMemory/Deps/Keystone/libs-android/**/libkeystone.a`
- **Java overlay UI** — `app/src/main/java/com/android/support/`
  - `Menu.java` — the LGL-mod menu rendering
  - `MainActivity.java` — starts `com.unity3d.player.UnityPlayerActivity` and shows the overlay
  - `Launcher.java`, `Preferences.java`, `CrashHandler.java`, `DialogHelper.java`
- On launch, `MainActivity` starts the Unity player activity and draws the mod-menu overlay on top.

### `imgui` / `imgui-backup` — IMGUI version (`cheat/` or `cheat_bak/`)

A self-contained `ndk-build` project that produces `libfbk.so` from a Dear ImGui-based menu. It pulls in:

| Component | Role |
|-----------|------|
| `imgui/imgui-master` | Dear ImGui (menu UI) |
| `shadowhook` | Inline hook framework (Android) |
| `Dobby` | Hooking |
| `KittyMemory` (+ `Keystone` assembler) | Memory read / write / patch |
| `And64InlineHook` | ARM64 inline hook |
| `il2cpp_src/Il2cpp_Resolver_Android` | IL2CPP API resolver — referenced by `cheat/jni/Android.mk` as `../../il2cpp_src/Il2cpp_Resolver_Android` |

Config lives in `cheat.cfg` / `cheat_bak/cheat.cfg` (e.g. `infinite_stamina=1`, `god_mode=0`, `esp_objects=1`).
The shared library is pushed to the game's native library path on the device.

---

## Features

Both native variants implement the same cheat surface against the game's IL2CPP runtime:

- **Player** — infinite stamina, god mode, no-clip, speed, jump multiplier.
- **ESP** — box / skeleton / name / health ESP for players, enemies, scrap, and map objects, drawn over the game view.
- **Item / scrap** — item dupe, infinite charge, item ESP, value hacks.
- **World** — mask SV (sanity/value) edits, quota skip, ship-at-door shortcuts.
- **Misc** — auto-reconnect, config persistence, runtime field/method caching via the IL2CPP resolver (offsets auto-heal as scenes load).

The cheat code resolves IL2CPP fields and methods **at runtime** through `libil2cpp.so`
(see `cheat/jni/il2cpp.h` / `app/src/main/jni/il2cpp.h`), so it does not depend on a fixed
`dump.cs` — it heals itself as classes become available per scene.

---

## Requirements

| Tool | Version |
|------|---------|
| **Android SDK** (platform-tools, build-tools) | tested with **SDK 36** |
| **Android NDK** | **r29** (`android-ndk-r29`, Pkg.Revision `29.0.14206865`) |
| **JDK** | **21+** (AGP 9.2 / Gradle 9.6.1 require JDK 21) |
| **Device** | rooted/debuggable Android, **API 23+** (minSdk 23, targetSdk 36) |
| **ABIs** | `arm64-v8a` and `armeabi-v7a` |

Set these before building locally (adjust to your paths):

```bat
set ANDROID_HOME=S:\Projects\Android
set ANDROID_NDK_HOME=S:\Projects\Android\NDK\android-ndk-r29-windows\android-ndk-r29
set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.x-hotspot
```

> The repo's `app/build.gradle` hardcodes a Windows `ndkPath` for local builds. On CI (Linux)
> the workflow neutralizes it automatically (see [Continuous Integration](#continuous-integration-github-actions)).
> For local Linux builds, delete the `ndkPath` line or set `android.ndkVersion`/`local.properties`.

---

## Building locally

### LGL version (`main`)

Build the native library, then the APK:

```bat
build.cmd        :: ndk-build -> app/src/main/jni/libs/{arm64-v8a,armeabi-v7a}/libfbk.so
build_apk.cmd    :: gradlew assembleDebug -> app/build/outputs/apk/debug/app-debug.apk
```

Or, with the env vars set, directly:

```bash
./gradlew assembleDebug
```

The Gradle build uses AGP's `ndkBuild` integration (`app/build.gradle` → `externalNativeBuild`),
so the native `.so` is compiled as part of the APK build — no separate step required.

### IMGUI version (`imgui` / `imgui-backup`)

```bat
cd cheat            (or cheat_bak)
build.cmd           :: ndk-build -> libs/arm64-v8a/libfbk.so
```

`cheat/build.cmd` additionally `adb push`es the built `.so` to
`/storage/emulated/0/_66XZD/apks/` on a connected device.

> The IMGUI build targets **`arm64-v8a` only** (`Application.mk` → `APP_ABI := arm64-v8a`).

---

## Continuous Integration (GitHub Actions)

Every push to any of the three branches triggers [`.github/workflows/build.yml`](.github/workflows/build.yml),
which builds that branch's variant on `ubuntu-latest`:

| Branch | Build | Produces |
|--------|-------|----------|
| `main` | Gradle `assembleDebug` (AGP `ndkBuild`) | `app-debug.apk` |
| `imgui` | `ndk-build` (`cheat/jni`) | `libfbk-...-arm64-v8a.so` |
| `imgui-backup` | `ndk-build` (`cheat_bak/jni`) | `libfbk-...-arm64-v8a.so` |

The workflow:

1. Checks out the pushed branch.
2. Installs the Android SDK, **NDK r29 (`29.0.14206865`)**, platform 36 and build-tools 36.0.0.
3. For `main`: sets up Temurin JDK 21 and **strips the hardcoded Windows `ndkPath`**,
   writing a `local.properties` that points at the CI SDK/NDK.
4. For the IMGUI branches: verifies `il2cpp_src/Il2cpp_Resolver_Android` is present, then runs `ndk-build`.
5. Uploads the built artifact (APK or `.so`) for download from the Actions run.

You can also trigger a build manually from the **Actions** tab → *Build* → *Run workflow*
(choose the branch). Each branch's run is isolated via `concurrency` so overlapping pushes
don't clobber each other's artifacts.

---

## Device installation

**LGL (`main`)** — install the APK:

```bat
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Launch the app; it starts the Unity game activity and overlays the mod menu.

**IMGUI (`imgui` / `imgui-backup`)** — push the native library into the game's native lib dir:

```bat
adb push cheat\libs\arm64-v8a\libfbk.so /storage/emulated/0/_66XZD/apks/
```

(Path matches `cheat/build.cmd`. Adjust to your loader / game package.)

> A rooted device is required for the IMGUI variant, since it patches the running
> game process's native libraries.

---

## Configuration

Toggle features in `cheat.cfg` (IMGUI) — example keys:

```ini
infinite_stamina=1
god_mode=0
esp_objects=1
; ...see the file in each branch for the full list
```

The LGL variant persists toggles through `Preferences.java` (Android `SharedPreferences`).

---

## Repository hygiene — what is NOT committed

To keep the repo small and fast, the following are **git-ignored** (regeneratable or huge
reference dumps, not compiled into the binary):

- Build artifacts: `*.so`, `*.o`, `*.a` (except the prebuilt `libdobby.a` / `libkeystone.a` link libs),
  `build/`, `obj/`, generated `libs/`.
- IL2CPP reverse-engineering dumps in `Dump0/` (`dump.cs`, `il2cpp.h`, `il2cpp-functions.h`,
  `script.json`, `generics_dump.txt`, …).
- Large standalone toolkits kept locally only: `references/`, `il2cpp_src/` (except the
  `Il2cpp_Resolver_Android` dependency the IMGUI build needs), `imgui_src/`, and the separate
  `lglteam-menu/` checkout.

If you need the full IL2CPP dumps or the complete toolchain committed, do it on a separate
branch (Git-LFS recommended for the multi-MB files).

---

## License

Distributed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

> GPLv3 is copyleft: if you distribute a **binary** build (APK / `.so`), you must also make
> the corresponding source available under the GPL. The full source for every variant is in
> this repository, so publishing from these branches satisfies that obligation.

---

## Disclaimer

This project is for **educational and research purposes** only. Modifying or intercepting a
game you do not own may violate its Terms of Service and applicable law. Use at your own risk.

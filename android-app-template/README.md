# Android App Template - RowingSync

This directory contains template files for the RowingSync Android app that syncs rowing sessions from the ESP32 to Samsung Health via Health Connect.

## Moving to a New Repository

To create a standalone Android app repository:

1. Create a new repository on GitHub (e.g., `RowingSync`)
2. Copy all files from this directory to the new repository
3. Open the project in Android Studio
4. Update `settings.gradle.kts` with your project name
5. Update `app/build.gradle.kts` with your package name
6. Sync and build

## Project Structure

```
RowingSync/
├── settings.gradle.kts
├── build.gradle.kts
├── gradle.properties
├── app/
│   ├── build.gradle.kts
│   ├── src/main/
│   │   ├── AndroidManifest.xml
│   │   ├── res/
│   │   │   ├── values/
│   │   │   │   ├── strings.xml
│   │   │   │   ├── health_permissions.xml
│   │   │   │   └── themes.xml
│   │   │   └── xml/
│   │   │       └── network_security_config.xml
│   │   └── java/com/example/rowingsync/
│   │       ├── MainActivity.kt
│   │       ├── RowingSyncApplication.kt
│   │       ├── data/
│   │       │   ├── Models.kt
│   │       │   ├── Esp32Api.kt
│   │       │   └── ApiClient.kt
│   │       ├── health/
│   │       │   └── HealthConnectManager.kt
│   │       └── ui/
│   │           ├── MainViewModel.kt
│   │           └── screens/
│   │               └── SessionListScreen.kt
│   └── proguard-rules.pro
└── gradle/
    └── wrapper/
        └── gradle-wrapper.properties
```

## Quick Start

1. Install Android Studio (latest stable version)
2. Open this directory as a project
3. Let Gradle sync dependencies
4. Connect an Android device or start an emulator
5. Run the app

## Features

- Connects to ESP32 rowing monitor via HTTP
- Lists workout sessions stored on ESP32
- Syncs sessions to Health Connect (which syncs to Samsung Health)
- Marks sessions as synced to prevent duplicates

## Requirements

- Android 9+ (API 28+)
- Health Connect app installed (comes with Android 14+)
- Same WiFi network as ESP32

## Configuration

Edit the ESP32 address in `ApiClient.kt`:
```kotlin
private var currentBaseUrl: String = "http://192.168.4.1/"  // AP mode
// or "http://rowing.local/"  // STA mode with mDNS
```

## Health Connect Permissions

The app requests these permissions on first launch:
- Read/Write Exercise Sessions
- Read/Write Heart Rate
- Read/Write Distance
- Read/Write Calories Burned

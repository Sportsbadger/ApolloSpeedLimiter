# Phantom 15 mph Android app

Open this `android` folder in Android Studio. Let Studio install the requested SDK and Gradle components, then build a debug APK using **Build → Generate Bundle(s) / APK(s) → Build APK(s)**. The included Gradle wrapper provides the build version. The `releases` folder contains a prebuilt debug APK; rebuild and replace it when sharing a changed app.

The app requests 15 mph, rounded to 24 km/h using the same inferred conversion as the ESP32 firmware. It targets the Bluetooth address `CD:00:01:03:66:80`; change `ADDRESS` in `MainActivity.java` and rebuild for another scooter. Turn on the scooter and close other connected BLE apps. The app scans and retries while open, then closes after receiving a matching response. A matching response reports a setting; it does not prove enforcement. Verify the limit while stationary.

Android requires Bluetooth permissions and, when Bluetooth is off, asks the user to turn it on. The app leaves Bluetooth in its existing state if already on and does not switch it off afterward. On Android 6–11, Bluetooth scanning also requires location permission and may require device location to be enabled. An emulator can test the interface, but test scooter communication with a physical Android phone.

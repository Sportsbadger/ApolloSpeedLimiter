# Phantom 15 mph Android app

Open the `android` folder in Android Studio and let it install the requested Android SDK and Gradle components. Create a virtual device in Device Manager and run the app to check its interface. A physical Android phone is needed to test communication with the scooter.

The app starts the request when opened, after any required Bluetooth permissions. Turn on the scooter and close other apps connected to it. It targets `CD:00:01:03:66:80`, sends the same frames as the ESP32 firmware, and requests 15 mph (rounded to 24 km/h). A matching response indicates the reported setting, not proof of enforcement. Verify the limit while stationary.

This is an initial implementation and has not yet been built or tested against hardware.

# ClassicHATE
ClassicHate is a Classic Minecraft-like custom client compatible with CSP and CPE. The code is from ClassiCube and will be changed and customized in the future.

Idea by Ludinko23

Main Programmer: dawidg81

Cross-Compiling on Linux!

How to build for Linux:
```
make linux
```
Dependencies
```
sudo apt install build-essential libx11-dev libxi-dev libgl1-mesa-dev libopenal-dev libcurl4-openssl-dev
```

How to build for Bindoj x64:
```
make mingw CC=x86_64-w64-mingw32-gcc
```
You will need MinGw32
```
sudo apt install gcc-mingw-w64-i686 g++-mingw-w64-i686
```
How to build for Bindoj 32biz:
```
make mingw CC=i686-w64-mingw32-gcc
```
You will need MinGw32
```
sudo apt install gcc-mingw-w64-i686 g++-mingw-w64-i686
```

How to build an APK installation file for Android:
```
cd android
./gradlew
```
Dependencies
```
sudo apt install git cmake ninja-build openjdk-11-jdk build-essential
```
Android NDK
```
mkdir -p ~/Android
cd ~/Android
wget https://dl.google.com/android/repository/android-ndk-r20b-linux-x86_64.zip
unzip android-ndk-r20b-linux-x86_64.zip
```
Android SDK
```
cd ~/Android
wget https://dl.google.com/android/repository/commandlinetools-linux-9477386_latest.zip
mkdir -p sdk/cmdline-tools
unzip commandlinetools-linux-9477386_latest.zip -d sdk/cmdline-tools/latest
```
To set enviroment variables
```
export ANDROID_NDK_HOME=~/Android/ndk/android-ndk-r23
export PATH=$ANDROID_NDK_HOME:$PATH
```

The 3DS
```
make 3ds
```
You need the Devkit for the 3dS!

*may not be 100% accurate :P*



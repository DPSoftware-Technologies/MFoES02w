# alpine-rpi-cmake

C++17 CMake project template for **Alpine Linux on Raspberry Pi Zero 2W** (aarch64 musl).  
Statically linked by default — drop the binary, run it, no runtime deps needed.

---

## Project layout

```
alpine-rpi-cmake/
├── CMakeLists.txt                  # Root CMake
├── cmake/
│   └── aarch64-alpine-musl.cmake  # Cross-compile toolchain file
├── include/
│   └── app.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   └── app.cpp
└── scripts/
    ├── build-cross.sh             # Cross-compile from x86_64 Linux host
    └── build-native.sh            # Native build on Alpine RPi
```

---

## Option A — Cross-compile (x86_64 Linux host)

### 1. Install the cross-toolchain

```sh
# Debian/Ubuntu
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake

# Arch
sudo pacman -S aarch64-linux-gnu-gcc cmake
```

> For a true musl cross-compiler use [musl-cross-make](https://github.com/richfelker/musl-cross-make)  
> and update `CROSS_TRIPLE` in `cmake/aarch64-alpine-musl.cmake` to `aarch64-linux-musl`.

### 2. Build

```sh
chmod +x scripts/build-cross.sh
./scripts/build-cross.sh Release
```

### 3. Deploy

```sh
scp build-aarch64/bin/alpine_rpi_app root@<rpi-ip>:/usr/local/bin/
```

---

## Option B — Native build on Alpine RPi

### 1. Install build tools (once)

```sh
apk add cmake make g++ musl-dev
```

### 2. Build

```sh
chmod +x scripts/build-native.sh
./scripts/build-native.sh Release
```

---

## CMake options

| Option           | Default | Description                              |
|------------------|---------|------------------------------------------|
| `STATIC_LINK`    | `ON`    | Link statically (musl-compatible)        |
| `ENABLE_TESTS`   | `OFF`   | Build unit tests                         |
| `ENABLE_LOGGING` | `OFF`   | Enable `[DBG]` log output at runtime     |

Example:

```sh
cmake -B build \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-alpine-musl.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_LOGGING=ON
```

---

## Alpine diskless / lbu notes

If deploying to a diskless Alpine setup (e.g. MFOES-style):

```sh
# On the RPi after deploying the binary
lbu add /usr/local/bin/alpine_rpi_app
lbu commit
```

To autostart via `local.d`:

```sh
cat > /etc/local.d/alpine_rpi_app.start << 'EOF'
#!/bin/sh
/usr/local/bin/alpine_rpi_app &
EOF
chmod +x /etc/local.d/alpine_rpi_app.start
rc-update add local default
lbu commit
```

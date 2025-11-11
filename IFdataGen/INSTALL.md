# IFdataGen - Quick Installation Guide

## One-Line Installation

```bash
cd IFdataGen && ./build_and_install.sh
```

This will automatically:
1. Detect your OS (Ubuntu/Debian/Fedora/Arch)
2. Install all required dependencies
3. Build IFdataGen in Release mode (maximum speed)
4. Show usage examples

## Options

```bash
./build_and_install.sh [options]

Options:
  -h, --help          Show help message
  -d, --debug         Build in Debug mode (no optimisation)
  -r, --relwithdeb    Build in RelWithDebInfo mode (optimised + symbols)
  --skip-deps         Skip dependency installation
```

## Manual Installation (if script fails)

### 1. Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libomp-dev binutils-gold
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc gcc-c++ cmake ninja-build libomp-devel binutils
```

**Arch/Manjaro:**
```bash
sudo pacman -S base-devel cmake ninja openmp binutils
```

### 2. Build

```bash
cmake -S . -B out/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_NATIVE_OPT=ON
cmake --build out/build/release -j$(nproc)
```

## Quick Test

```bash
./out/build/release/IFdataGen -c configs/GPS_BDS_GAL_L1CA_L1C_B1C_E1.json -t
```

## Troubleshooting

**Problem:** `sudo: error initializing audit plugin`
**Solution:** Run with `--skip-deps` flag:
```bash
./build_and_install.sh --skip-deps
```

**Problem:** Dependencies not found
**Solution:** Install manually (see Manual Installation above)

**Problem:** Build fails
**Solution:** Try Debug build:
```bash
./build_and_install.sh --debug
```

## For More Information

See the full [README.md](README.md) for detailed documentation.

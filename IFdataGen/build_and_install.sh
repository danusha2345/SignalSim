#!/bin/bash
#
# IFdataGen - Automatic Build and Installation Script
# This script automates the complete setup process for IFdataGen
#

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored message
print_msg() {
    local color=$1
    shift
    echo -e "${color}$@${NC}"
}

print_header() {
    echo ""
    echo "================================================================================"
    print_msg "$BLUE" "  $1"
    echo "================================================================================"
    echo ""
}

print_success() {
    print_msg "$GREEN" "✓ $1"
}

print_warning() {
    print_msg "$YELLOW" "⚠ $1"
}

print_error() {
    print_msg "$RED" "✗ $1"
}

# Check if we're in the right directory
check_directory() {
    if [[ ! -f "CMakeLists.txt" ]]; then
        print_error "CMakeLists.txt not found!"
        print_error "Please run this script from the IFdataGen directory"
        exit 1
    fi

    if [[ ! -d "../EphData" ]]; then
        print_warning "EphData directory not found in parent directory"
        print_warning "You may need to download ephemeris data separately"
    fi
}

# Detect OS
detect_os() {
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        OS=$ID
        OS_VERSION=$VERSION_ID
    else
        print_error "Cannot detect OS"
        exit 1
    fi

    print_msg "$BLUE" "Detected OS: $OS $OS_VERSION"
}

# Install dependencies
install_dependencies() {
    print_header "Installing Dependencies"

    case $OS in
        ubuntu|debian)
            print_msg "$BLUE" "Updating package list..."
            sudo apt update

            print_msg "$BLUE" "Installing required packages..."
            sudo apt install -y \
                build-essential \
                cmake \
                ninja-build \
                libomp-dev \
                binutils-gold

            print_success "Dependencies installed successfully"
            ;;

        fedora|rhel|centos)
            print_msg "$BLUE" "Installing required packages..."
            sudo dnf install -y \
                gcc gcc-c++ \
                cmake \
                ninja-build \
                libomp-devel \
                binutils

            print_success "Dependencies installed successfully"
            ;;

        arch|manjaro)
            print_msg "$BLUE" "Installing required packages..."
            sudo pacman -S --needed --noconfirm \
                base-devel \
                cmake \
                ninja \
                openmp \
                binutils

            print_success "Dependencies installed successfully"
            ;;

        *)
            print_warning "Unsupported OS: $OS"
            print_warning "Please install the following packages manually:"
            print_msg "$YELLOW" "  - build-essential / gcc, g++"
            print_msg "$YELLOW" "  - cmake"
            print_msg "$YELLOW" "  - ninja-build"
            print_msg "$YELLOW" "  - libomp-dev / openmp"
            print_msg "$YELLOW" "  - binutils-gold"
            read -p "Continue anyway? [y/N] " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                exit 1
            fi
            ;;
    esac
}

# Build IFdataGen
build_ifdatagen() {
    local build_type=$1
    local build_dir="out/build/${build_type}"

    print_header "Building IFdataGen (${build_type})"

    case $build_type in
        release)
            print_msg "$BLUE" "Configuring Release build (maximum speed)..."
            cmake -S . -B "$build_dir" \
                -G Ninja \
                -DCMAKE_BUILD_TYPE=Release \
                -DUSE_NATIVE_OPT=ON
            ;;

        relwithdeb)
            print_msg "$BLUE" "Configuring RelWithDebInfo build (optimised + debug symbols)..."
            cmake -S . -B "$build_dir" \
                -G Ninja \
                -DCMAKE_BUILD_TYPE=RelWithDebInfo
            ;;

        debug)
            print_msg "$BLUE" "Configuring Debug build (no optimisation)..."
            cmake -S . -B "$build_dir" \
                -G Ninja \
                -DCMAKE_BUILD_TYPE=Debug
            ;;

        *)
            print_error "Unknown build type: $build_type"
            exit 1
            ;;
    esac

    print_msg "$BLUE" "Building with $(nproc) parallel jobs..."
    cmake --build "$build_dir" -j$(nproc)

    if [[ -f "$build_dir/IFdataGen" ]]; then
        print_success "Build completed successfully!"
        print_msg "$GREEN" "Executable: $(pwd)/$build_dir/IFdataGen"
    else
        print_error "Build failed - executable not found"
        exit 1
    fi
}

# Show usage examples
show_examples() {
    print_header "Usage Examples"

    local exe="./out/build/release/IFdataGen"

    print_msg "$BLUE" "Basic usage:"
    echo "  $exe -c configs/GPS_BDS_GAL_L1CA_L1C_B1C_E1.json -t"
    echo ""

    print_msg "$BLUE" "Generate multi-constellation signals:"
    echo "  1. GPS + BeiDou + Galileo (L1):"
    echo "     $exe -c configs/GPS_BDS_GAL_L1CA_L1C_B1C_E1.json -t"
    echo ""
    echo "  2. GPS + BeiDou + Galileo (L1 + B1I):"
    echo "     $exe -c configs/GPS_BDS_GAL_L1CA_L1C_B1C_B1I_E1.json -t"
    echo ""
    echo "  3. GPS + BeiDou + Galileo + GLONASS (L1/G1):"
    echo "     $exe -c configs/GPS_BDS_GAL_GLO_L1CA_L1C_B1C_B1I_E1_G1.json -t"
    echo ""
    echo "  4. GPS + BeiDou + Galileo + GLONASS (L2/B2/G2):"
    echo "     $exe -c configs/GPS_BDS_GAL_GLO_L2C_B2I_B2b_E5b_G2.json -t"
    echo ""
    echo "  5. GPS + BeiDou + Galileo (L5/E5a/B2a):"
    echo "     $exe -c configs/GPS_BDS_GAL_L5_B2a_E5a.json -t"
    echo ""
    echo "  6. BeiDou + Galileo (B3I/E6):"
    echo "     $exe -c configs/BDS_GAL_B3I_E6.json -t"
    echo ""

    print_msg "$BLUE" "Transmit with HackRF:"
    echo "  hackrf_transfer -t output.bin -f 1575420000 -s 4092000 -a 1 -x 47 -R"
    echo ""

    print_msg "$BLUE" "Transmit with USRP B210:"
    echo "  tx_samples_from_file --file output.bin --spb 20000 --rate 4091000 \\"
    echo "    --freq 1575420000 --wirefmt sc8 --gain 70"
    echo ""
}

# Main installation flow
main() {
    print_header "IFdataGen - Automatic Build and Installation"

    # Parse command line arguments
    BUILD_TYPE="release"
    INSTALL_DEPS=1
    SHOW_HELP=0

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                SHOW_HELP=1
                shift
                ;;
            -d|--debug)
                BUILD_TYPE="debug"
                shift
                ;;
            -r|--relwithdeb)
                BUILD_TYPE="relwithdeb"
                shift
                ;;
            --skip-deps)
                INSTALL_DEPS=0
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                SHOW_HELP=1
                shift
                ;;
        esac
    done

    if [[ $SHOW_HELP -eq 1 ]]; then
        echo "Usage: $0 [options]"
        echo ""
        echo "Options:"
        echo "  -h, --help          Show this help message"
        echo "  -d, --debug         Build in Debug mode (no optimisation)"
        echo "  -r, --relwithdeb    Build in RelWithDebInfo mode (optimised + symbols)"
        echo "  --skip-deps         Skip dependency installation"
        echo ""
        echo "Default: Release build with dependency installation"
        exit 0
    fi

    # Check directory
    check_directory

    # Detect OS
    detect_os

    # Install dependencies
    if [[ $INSTALL_DEPS -eq 1 ]]; then
        install_dependencies
    else
        print_warning "Skipping dependency installation"
    fi

    # Build
    build_ifdatagen "$BUILD_TYPE"

    # Show examples
    show_examples

    print_header "Installation Complete!"
    print_success "IFdataGen is ready to use"

    print_msg "$BLUE" "Next steps:"
    echo "  1. Check configs/*.json files for signal configurations"
    echo "  2. Ensure ephemeris data is in ../EphData/"
    echo "  3. Run: ./out/build/${BUILD_TYPE}/IFdataGen -c configs/<config>.json -t"
    echo ""
}

# Run main
main "$@"

#!/bin/bash
# =============================================================================
# AVM CTC Framework - Virtual Environment Setup Script
# =============================================================================
# This script creates a Python virtual environment and installs all required
# dependencies for the AVM CTC testing framework.
#
# Usage:
#   ./setup_env.sh                    # Create venv in default location (./venv)
#   ./setup_env.sh /path/to/env       # Create venv in custom location
#   ./setup_env.sh --perceptual       # Also install the optional perceptual
#                                     # metric extras (LPIPS/DISTS/ColorVideoVDP)
#   ./setup_env.sh /path/to/env --perceptual
#
# After running this script, activate the environment with:
#   source venv/bin/activate    # Linux/macOS
#   or
#   source /path/to/env/bin/activate  # Custom location
# =============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Parse arguments: an optional venv path and an optional --perceptual flag,
# in either order.
INSTALL_PERCEPTUAL=false
VENV_PATH=""
for arg in "$@"; do
    case "$arg" in
        --perceptual) INSTALL_PERCEPTUAL=true ;;
        -h|--help)
            grep '^#' "$0" | sed -n '2,25p' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        -*)
            echo "Unknown option: $arg" >&2
            echo "Usage: $0 [/path/to/venv] [--perceptual]" >&2
            exit 1
            ;;
        *) VENV_PATH="$arg" ;;
    esac
done
VENV_PATH="${VENV_PATH:-$SCRIPT_DIR/venv}"

# Minimum Python version required
MIN_PYTHON_VERSION="3.8"

echo -e "${GREEN}==============================================================================${NC}"
echo -e "${GREEN}AVM CTC Framework - Virtual Environment Setup${NC}"
echo -e "${GREEN}==============================================================================${NC}"

# Check if Python 3 is available
check_python() {
    if command -v python3 &> /dev/null; then
        PYTHON_CMD="python3"
    elif command -v python &> /dev/null; then
        # Check if python is Python 3
        if python --version 2>&1 | grep -q "Python 3"; then
            PYTHON_CMD="python"
        else
            echo -e "${RED}Error: Python 3 is required but not found.${NC}"
            echo "Please install Python 3.8 or later."
            exit 1
        fi
    else
        echo -e "${RED}Error: Python is not installed.${NC}"
        echo "Please install Python 3.8 or later."
        exit 1
    fi

    # Check Python version
    PYTHON_VERSION=$($PYTHON_CMD -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
    echo -e "Found Python: ${GREEN}$PYTHON_CMD${NC} (version $PYTHON_VERSION)"

    # Compare versions
    if [ "$(printf '%s\n' "$MIN_PYTHON_VERSION" "$PYTHON_VERSION" | sort -V | head -n1)" != "$MIN_PYTHON_VERSION" ]; then
        echo -e "${RED}Error: Python $MIN_PYTHON_VERSION or later is required, but found $PYTHON_VERSION${NC}"
        exit 1
    fi
}

# Create virtual environment
create_venv() {
    echo -e "\n${YELLOW}Creating virtual environment at: $VENV_PATH${NC}"

    if [ -d "$VENV_PATH" ]; then
        echo -e "${YELLOW}Virtual environment already exists. Removing old one...${NC}"
        rm -rf "$VENV_PATH"
    fi

    $PYTHON_CMD -m venv "$VENV_PATH"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Virtual environment created successfully.${NC}"
    else
        echo -e "${RED}Failed to create virtual environment.${NC}"
        exit 1
    fi
}

# Activate virtual environment and install dependencies
install_dependencies() {
    echo -e "\n${YELLOW}Activating virtual environment...${NC}"
    source "$VENV_PATH/bin/activate"

    echo -e "\n${YELLOW}Installing dependencies from requirements.txt...${NC}"
    REQUIREMENTS_FILE="$SCRIPT_DIR/requirements.txt"

    if [ -f "$REQUIREMENTS_FILE" ]; then
        pip install --require-hashes -r "$REQUIREMENTS_FILE"

        if [ $? -eq 0 ]; then
            echo -e "${GREEN}All dependencies installed successfully.${NC}"
        else
            echo -e "${RED}Failed to install some dependencies.${NC}"
            exit 1
        fi
    else
        echo -e "${RED}Error: requirements.txt not found at $REQUIREMENTS_FILE${NC}"
        exit 1
    fi
}

# Install the optional perceptual metric extras (LPIPS, DISTS, ColorVideoVDP).
# Kept separate from the hash-pinned core requirements because torch ships
# platform-specific wheels that cannot share a portable hashed lock.
install_perceptual() {
    echo -e "\n${YELLOW}Installing optional perceptual metric dependencies...${NC}"
    PERCEPTUAL_FILE="$SCRIPT_DIR/requirements-perceptual.txt"

    if [ ! -f "$PERCEPTUAL_FILE" ]; then
        echo -e "${RED}Error: requirements-perceptual.txt not found at $PERCEPTUAL_FILE${NC}"
        exit 1
    fi

    echo -e "${YELLOW}Note: pyiqa is PolyForm Noncommercial licensed (non-commercial use only).${NC}"
    echo -e "${YELLOW}      This pulls in torch and may download >1 GB.${NC}"

    # Not --require-hashes: see the header of requirements-perceptual.txt.
    pip install -r "$PERCEPTUAL_FILE"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Perceptual metric dependencies installed.${NC}"
        echo -e "${YELLOW}Enable them with 'perceptual_metrics.enabled: true' in src/config.yaml${NC}"
    else
        echo -e "${RED}Failed to install perceptual metric dependencies.${NC}"
        exit 1
    fi
}

# Print summary and instructions
print_summary() {
    echo -e "\n${GREEN}==============================================================================${NC}"
    echo -e "${GREEN}Setup Complete!${NC}"
    echo -e "${GREEN}==============================================================================${NC}"
    echo -e "\nTo activate the virtual environment, run:"
    echo -e "  ${YELLOW}source $VENV_PATH/bin/activate${NC}"
    echo -e "\nTo deactivate when done:"
    echo -e "  ${YELLOW}deactivate${NC}"
    echo -e "\nTo run the CTC tests:"
    echo -e "  ${YELLOW}cd $SCRIPT_DIR/src${NC}"
    echo -e "  ${YELLOW}python AV2CTCTest.py -f encode ...${NC}         # Regular CTC (LD/RA/AI/STILL)"
    echo -e "  ${YELLOW}python ConvexHullTest.py -f convexhull ...${NC}  # Adaptive Streaming (AS)"
    echo -e "\nFor ECF (Extended Chroma Format) testing:"
    echo -e "  Set ${YELLOW}ecf.enabled: true${NC} in ${YELLOW}src/config.yaml${NC}"
    echo -e "  See ${YELLOW}USER_GUIDE.md${NC} for ECF configuration details"
    if [ "$INSTALL_PERCEPTUAL" = true ]; then
        echo -e "\nFor perceptual metrics (LPIPS / DISTS / ColorVideoVDP):"
        echo -e "  Set ${YELLOW}perceptual_metrics.enabled: true${NC} in ${YELLOW}src/config.yaml${NC}"
    else
        echo -e "\nPerceptual metrics (LPIPS / DISTS / ColorVideoVDP) were NOT installed."
        echo -e "  Re-run with ${YELLOW}--perceptual${NC} if you need them."
    fi
    echo -e "\n${GREEN}==============================================================================${NC}"
}

# Main execution
check_python
create_venv
install_dependencies
if [ "$INSTALL_PERCEPTUAL" = true ]; then
    install_perceptual
fi
print_summary

#!/bin/bash
# =============================================================================
# AVM CTC Framework - Virtual Environment Setup Script
# =============================================================================
# This script creates a Python virtual environment and installs all required
# dependencies for the AVM CTC testing framework.
#
# Usage:
#   ./setup_env.sh              # Create venv in default location (./venv)
#   ./setup_env.sh /path/to/env # Create venv in custom location
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

# Virtual environment path (default or custom)
VENV_PATH="${1:-$SCRIPT_DIR/venv}"

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
    echo -e "  ${YELLOW}python AV2CTCTest.py -f encode ...${NC}"
    echo -e "  ${YELLOW}python ConvexHullTest.py -f convexhull ...${NC}"
    echo -e "\n${GREEN}==============================================================================${NC}"
}

# Main execution
check_python
create_venv
install_dependencies
print_summary

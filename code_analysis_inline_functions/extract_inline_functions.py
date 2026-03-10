#!/usr/bin/env python3
"""
Extract all inline functions from the av2/ directory and generate a complete inventory.

This script:
1. Scans av2/ for .h and .c files (excluding SIMD subdirectories)
2. Extracts inline function definitions (static INLINE, static AVM_INLINE, AVM_FORCE_INLINE)
3. Counts lines by matching braces
4. Builds a header include map to count TUs per header
5. Assigns severity based on rubric
6. Generates markdown inventory file
"""

import os
import re
from collections import defaultdict
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List, Set, Tuple

# Base directory for the av2 source
AVM_ROOT = Path(__file__).parent.parent
AV2_DIR = AVM_ROOT / "av2"

# Directories to exclude (SIMD implementations)
EXCLUDE_DIRS = {"x86", "arm", "ppc", "mips"}

# Patterns for inline functions
INLINE_PATTERNS = [
    r"static\s+INLINE\b",
    r"static\s+AVM_INLINE\b",
    r"AVM_FORCE_INLINE\b",
    r"static\s+inline\b",
    r"static\s+__inline\b",
]


@dataclass
class InlineFunction:
    """Represents an inline function found in the codebase."""
    name: str
    file_path: str
    line_number: int
    line_count: int
    file_type: str  # 'header' or 'source'
    signature: str
    tu_count: int = 0
    severity: str = "Info"
    proposed_change: str = ""


def should_exclude_path(path: Path) -> bool:
    """Check if path should be excluded (SIMD directories)."""
    parts = path.parts
    for part in parts:
        if part in EXCLUDE_DIRS:
            return True
    return False


def find_source_files(directory: Path, extension: str) -> List[Path]:
    """Find all files with given extension, excluding SIMD directories."""
    files = []
    for path in directory.rglob(f"*{extension}"):
        if not should_exclude_path(path):
            files.append(path)
    return sorted(files)


def extract_function_name(signature: str) -> str:
    """Extract function name from signature."""
    # Remove inline qualifiers
    clean = re.sub(r"(static\s+)?(INLINE|AVM_INLINE|AVM_FORCE_INLINE|inline|__inline)\s+", "", signature)
    # Remove return type and find function name
    # Pattern: type function_name(
    match = re.search(r"(\w+)\s*\(", clean)
    if match:
        return match.group(1)
    return "unknown"


def count_function_lines(lines: List[str], start_idx: int) -> Tuple[int, str]:
    """
    Count the number of lines in a function starting from start_idx.
    Returns (line_count, full_signature).
    """
    # First, find the opening brace
    brace_count = 0
    found_open_brace = False
    end_idx = start_idx
    signature_lines = []

    for i in range(start_idx, len(lines)):
        line = lines[i]

        # Build signature until we find opening brace
        if not found_open_brace:
            signature_lines.append(line.strip())

        # Count braces
        for char in line:
            if char == '{':
                brace_count += 1
                found_open_brace = True
            elif char == '}':
                brace_count -= 1

        end_idx = i

        if found_open_brace and brace_count == 0:
            break

    line_count = end_idx - start_idx + 1
    signature = " ".join(signature_lines)

    # Clean up signature (keep just up to opening brace)
    if '{' in signature:
        signature = signature[:signature.index('{')].strip()

    return line_count, signature


def extract_inline_functions(file_path: Path) -> List[InlineFunction]:
    """Extract all inline functions from a file."""
    functions = []

    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            lines = content.split('\n')
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return functions

    file_type = 'header' if file_path.suffix == '.h' else 'source'
    rel_path = str(file_path.relative_to(AVM_ROOT))

    # Combined pattern
    combined_pattern = re.compile(
        r"^[^/]*\b(static\s+(?:INLINE|AVM_INLINE|inline|__inline)|AVM_FORCE_INLINE)\b",
        re.MULTILINE
    )

    # Find all inline function definitions
    for i, line in enumerate(lines):
        # Skip comments
        stripped = line.strip()
        if stripped.startswith('//') or stripped.startswith('/*'):
            continue

        # Check if line contains inline function definition
        if combined_pattern.search(line):
            # Make sure it's a function definition (has opening paren eventually)
            # and is not just a declaration
            remaining_lines = '\n'.join(lines[i:min(i+10, len(lines))])
            if '(' in remaining_lines and '{' in remaining_lines:
                # Check it's not just a declaration (semicolon before brace)
                paren_close = remaining_lines.find(')')
                brace_open = remaining_lines.find('{')
                semi = remaining_lines.find(';')

                if brace_open != -1 and (semi == -1 or brace_open < semi):
                    line_count, signature = count_function_lines(lines, i)
                    func_name = extract_function_name(signature)

                    func = InlineFunction(
                        name=func_name,
                        file_path=rel_path,
                        line_number=i + 1,
                        line_count=line_count,
                        file_type=file_type,
                        signature=signature[:200]  # Truncate long signatures
                    )
                    functions.append(func)

    return functions


def build_include_map(source_files: List[Path]) -> Dict[str, Set[str]]:
    """
    Build a mapping from header files to source files that include them.
    Returns dict: header_path -> set of source_paths
    """
    include_map = defaultdict(set)
    include_pattern = re.compile(r'#include\s*[<"]([^>"]+)[">]')

    for src_file in source_files:
        try:
            with open(src_file, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception:
            continue

        rel_src = str(src_file.relative_to(AVM_ROOT))

        for match in include_pattern.finditer(content):
            include_path = match.group(1)

            # Normalize include path
            if include_path.startswith('av2/') or include_path.startswith('avm/'):
                header_path = include_path
            else:
                # Try to resolve relative path
                src_dir = src_file.parent
                possible_paths = [
                    src_dir / include_path,
                    AV2_DIR / include_path,
                    AV2_DIR / "common" / include_path,
                    AV2_DIR / "encoder" / include_path,
                    AV2_DIR / "decoder" / include_path,
                ]

                header_path = None
                for p in possible_paths:
                    if p.exists():
                        try:
                            header_path = str(p.relative_to(AVM_ROOT))
                            break
                        except ValueError:
                            continue

                if not header_path:
                    # Try direct path from av2/
                    if include_path.endswith('.h'):
                        for subdir in ['common', 'encoder', 'decoder']:
                            test_path = f"av2/{subdir}/{include_path}"
                            if (AVM_ROOT / test_path).exists():
                                header_path = test_path
                                break

                if not header_path:
                    continue

            include_map[header_path].add(rel_src)

    return include_map


def assign_severity(func: InlineFunction) -> Tuple[str, str]:
    """
    Assign severity and proposed change based on rubric.

    Severity criteria:
    - Critical: >100 lines, in .h, ≥3 TUs, contains malloc/free/loops
    - High: 50-100 lines in .h, OR contains loops/memory ops, ≥2 TUs
    - Medium: 30-50 lines in .h, OR moderate complexity
    - Low: 15-30 lines in .h, simple logic
    - Info: <15 lines OR in .c file (appropriate usage)
    """
    lines = func.line_count
    is_header = func.file_type == 'header'
    tu_count = func.tu_count

    # Check for complexity indicators in function name
    complex_indicators = ['alloc', 'free', 'malloc', 'init', 'setup', 'process',
                          'encode', 'decode', 'search', 'compute', 'calculate']
    is_complex = any(ind in func.name.lower() for ind in complex_indicators)

    if not is_header:
        # Source file inline functions are appropriate
        return "Info", "Keep as-is (local)"

    # Header file severity
    if lines > 100 and tu_count >= 3:
        return "Critical", "Move to .c file"
    elif lines > 100 and tu_count >= 2:
        return "Critical", "Move to .c file"
    elif lines > 100:
        return "High", "Move to .c file"
    elif lines >= 50 and tu_count >= 2:
        return "High", "Move to .c file"
    elif lines >= 50:
        return "High", "Consider moving to .c file"
    elif lines >= 30 and tu_count >= 3:
        return "Medium", "Remove INLINE keyword"
    elif lines >= 30:
        return "Medium", "Consider removing INLINE"
    elif lines >= 15 and tu_count >= 5:
        return "Low", "Consider removing INLINE"
    elif lines >= 15:
        return "Low", "Review for optimization"
    else:
        return "Info", "Keep as-is (small utility)"


def generate_inventory_report(functions: List[InlineFunction], include_map: Dict[str, Set[str]]) -> str:
    """Generate the complete inventory markdown report."""

    # Calculate TU counts for header functions
    for func in functions:
        if func.file_type == 'header':
            tu_set = include_map.get(func.file_path, set())
            func.tu_count = len(tu_set)
        else:
            func.tu_count = 1  # Source files only affect their own TU

    # Assign severity
    for func in functions:
        func.severity, func.proposed_change = assign_severity(func)

    # Sort by severity, then by file path
    severity_order = {"Critical": 0, "High": 1, "Medium": 2, "Low": 3, "Info": 4}
    functions.sort(key=lambda f: (severity_order.get(f.severity, 5), f.file_path, f.line_number))

    # Generate statistics
    total = len(functions)
    header_funcs = [f for f in functions if f.file_type == 'header']
    source_funcs = [f for f in functions if f.file_type == 'source']

    severity_counts = defaultdict(int)
    for func in functions:
        severity_counts[func.severity] += 1

    # Files with most inline functions
    file_counts = defaultdict(int)
    for func in functions:
        file_counts[func.file_path] += 1
    top_files = sorted(file_counts.items(), key=lambda x: -x[1])[:20]

    # Directory breakdown
    dir_counts = defaultdict(lambda: {"header": 0, "source": 0})
    for func in functions:
        dir_name = "/".join(func.file_path.split("/")[:2])
        dir_counts[dir_name][func.file_type] += 1

    # Generate report
    report = []
    report.append("# Complete Inline Functions Inventory")
    report.append("")
    report.append("This document contains a complete inventory of all inline functions in the `av2/` directory.")
    report.append("")
    report.append("## Summary Statistics")
    report.append("")
    report.append(f"- **Total Inline Functions:** {total}")
    report.append(f"- **Header Files (.h):** {len(header_funcs)}")
    report.append(f"- **Source Files (.c):** {len(source_funcs)}")
    report.append("")
    report.append("### Severity Distribution")
    report.append("")
    report.append("| Severity | Count | Percentage |")
    report.append("|----------|-------|------------|")
    for sev in ["Critical", "High", "Medium", "Low", "Info"]:
        count = severity_counts.get(sev, 0)
        pct = (count / total * 100) if total > 0 else 0
        report.append(f"| {sev} | {count} | {pct:.1f}% |")
    report.append("")

    report.append("### Directory Breakdown")
    report.append("")
    report.append("| Directory | Header (.h) | Source (.c) | Total |")
    report.append("|-----------|-------------|-------------|-------|")
    for dir_name in sorted(dir_counts.keys()):
        counts = dir_counts[dir_name]
        total_dir = counts["header"] + counts["source"]
        report.append(f"| {dir_name} | {counts['header']} | {counts['source']} | {total_dir} |")
    report.append("")

    report.append("### Top 20 Files by Inline Function Count")
    report.append("")
    report.append("| Rank | File | Count | TUs |")
    report.append("|------|------|-------|-----|")
    for i, (file_path, count) in enumerate(top_files, 1):
        tu_count = len(include_map.get(file_path, set()))
        report.append(f"| {i} | {file_path} | {count} | {tu_count} |")
    report.append("")

    report.append("### Severity Rubric")
    report.append("")
    report.append("| Severity | Criteria |")
    report.append("|----------|----------|")
    report.append("| Critical | >100 lines in .h, ≥2 TUs |")
    report.append("| High | 50-100 lines in .h, OR >100 lines with 1 TU |")
    report.append("| Medium | 30-50 lines in .h |")
    report.append("| Low | 15-30 lines in .h |")
    report.append("| Info | <15 lines OR in .c file (appropriate usage) |")
    report.append("")

    report.append("---")
    report.append("")
    report.append("## Complete Function Inventory")
    report.append("")
    report.append("| ID | Severity | Function | Location | Lines | TU Count | Proposed Change |")
    report.append("|----|----------|----------|----------|-------|----------|-----------------|")

    for i, func in enumerate(functions, 1):
        location = f"{func.file_path}:{func.line_number}"
        # Escape pipe characters in function names
        name = func.name.replace("|", "\\|")
        report.append(f"| {i} | {func.severity} | `{name}` | {location} | {func.line_count} | {func.tu_count} | {func.proposed_change} |")

    report.append("")
    report.append("---")
    report.append("")
    report.append("## Critical and High Severity Functions (Detail)")
    report.append("")
    report.append("These functions have the highest impact on binary size and should be prioritized for optimization.")
    report.append("")

    critical_high = [f for f in functions if f.severity in ("Critical", "High")]
    for func in critical_high:
        report.append(f"### {func.name}")
        report.append("")
        report.append(f"- **Location:** `{func.file_path}:{func.line_number}`")
        report.append(f"- **Lines:** {func.line_count}")
        report.append(f"- **TU Count:** {func.tu_count}")
        report.append(f"- **Severity:** {func.severity}")
        report.append(f"- **Proposed Change:** {func.proposed_change}")
        report.append(f"- **Signature:** `{func.signature[:150]}...`" if len(func.signature) > 150 else f"- **Signature:** `{func.signature}`")
        report.append("")

    return "\n".join(report)


def main():
    print("Extracting inline functions from av2/ directory...")

    # Find all source files
    header_files = find_source_files(AV2_DIR, ".h")
    source_files = find_source_files(AV2_DIR, ".c")

    print(f"Found {len(header_files)} header files and {len(source_files)} source files")

    # Extract inline functions
    all_functions = []

    print("Processing header files...")
    for hfile in header_files:
        funcs = extract_inline_functions(hfile)
        all_functions.extend(funcs)

    print("Processing source files...")
    for sfile in source_files:
        funcs = extract_inline_functions(sfile)
        all_functions.extend(funcs)

    print(f"Extracted {len(all_functions)} inline functions")

    # Build include map
    print("Building include map...")
    # Include all .c files in the project for include analysis
    all_c_files = list(AVM_ROOT.rglob("*.c"))
    all_c_files = [f for f in all_c_files if not should_exclude_path(f)]
    include_map = build_include_map(all_c_files)

    # Generate report
    print("Generating inventory report...")
    report = generate_inventory_report(all_functions, include_map)

    # Write report
    output_path = Path(__file__).parent / "inline_functions_complete_inventory.md"
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(report)

    print(f"Report written to: {output_path}")

    # Print summary
    header_funcs = [f for f in all_functions if f.file_type == 'header']
    source_funcs = [f for f in all_functions if f.file_type == 'source']

    print(f"\nSummary:")
    print(f"  Total: {len(all_functions)}")
    print(f"  Header files: {len(header_funcs)}")
    print(f"  Source files: {len(source_funcs)}")


if __name__ == "__main__":
    main()

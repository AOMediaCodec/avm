#!/usr/bin/env python3
"""Analyze all inline functions in av2/ directory."""
import re
import os
import json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AV2_DIR = os.path.join(ROOT, 'av2')

def find_files(root, exclude_dir='tflite_models'):
    result = []
    for dirpath, dirnames, filenames in os.walk(root):
        if exclude_dir in dirpath:
            continue
        for fn in filenames:
            if fn.endswith('.h') or fn.endswith('.c'):
                result.append(os.path.join(dirpath, fn))
    return sorted(result)

inline_pattern = re.compile(
    r'(?:static\s+INLINE\b|static\s+inline\b|AOM_FORCE_INLINE\b|AVM_FORCE_INLINE\b|'
    r'(?<!AOM_)(?<!AVM_)FORCE_INLINE\b|static\s+__inline__\b|'
    r'__attribute__\s*\(\s*\(\s*always_inline\s*\)\s*\))'
)

def get_inline_type(line):
    if 'AOM_FORCE_INLINE' in line:
        return 'AOM_FORCE_INLINE'
    if 'AVM_FORCE_INLINE' in line:
        return 'AVM_FORCE_INLINE'
    if re.search(r'(?<!AOM_)(?<!AVM_)FORCE_INLINE', line):
        return 'FORCE_INLINE'
    if re.search(r'static\s+INLINE\b', line):
        return 'static INLINE'
    if re.search(r'static\s+inline\b', line):
        return 'static inline'
    if re.search(r'static\s+__inline__\b', line):
        return 'static __inline__'
    if 'always_inline' in line:
        return 'always_inline'
    return 'unknown'

def categorize(filepath):
    if '/common/' in filepath:
        return 'common'
    elif '/encoder/' in filepath:
        return 'encoder'
    elif '/decoder/' in filepath:
        return 'decoder'
    return 'other'

all_funcs = []

files = find_files(AV2_DIR)
print(f"Scanning {len(files)} files...")

for filepath in files:
    try:
        with open(filepath, 'r', errors='replace') as f:
            lines = f.readlines()
    except:
        continue

    rel_path = os.path.relpath(filepath, ROOT)
    i = 0
    while i < len(lines):
        line = lines[i]
        if not inline_pattern.search(line):
            i += 1
            continue

        stripped = line.strip()
        if stripped.startswith('//') or stripped.startswith('*') or stripped.startswith('#define') or stripped.startswith('/*'):
            i += 1
            continue

        decl_start = i
        inline_type = get_inline_type(line)

        # Collect full declaration until we find '{'
        decl_text = ''
        j = i
        brace_found = False
        while j < len(lines):
            decl_text += lines[j]
            if '{' in lines[j]:
                brace_found = True
                break
            j += 1
            if j - i > 15:
                break

        if not brace_found:
            i += 1
            continue

        # Extract function name
        func_name = 'unknown'
        clean_decl = decl_text.replace('\n', ' ')
        paren_idx = clean_decl.find('(')
        if paren_idx > 0:
            before_paren = clean_decl[:paren_idx].strip()
            match = re.search(r'(\w+)\s*$', before_paren)
            if match:
                candidate = match.group(1)
                skip_kw = {'INLINE', 'inline', 'static', 'void', 'int', 'unsigned',
                           'FORCE_INLINE', 'AOM_FORCE_INLINE', 'AVM_FORCE_INLINE'}
                if candidate not in skip_kw:
                    func_name = candidate

        # Count function body by tracking braces
        brace_count = 0
        body_start = -1
        body_end = -1
        k = decl_start
        found_open = False
        while k < len(lines):
            for ch in lines[k]:
                if ch == '{':
                    if not found_open:
                        found_open = True
                        body_start = k
                    brace_count += 1
                elif ch == '}':
                    brace_count -= 1
                    if found_open and brace_count == 0:
                        body_end = k
                        break
            if body_end >= 0:
                break
            k += 1
            if k - decl_start > 2000:
                break

        if body_end < 0:
            i += 1
            continue

        func_line_count = body_end - body_start + 1
        body_text = ''.join(lines[body_start:body_end+1])

        has_for = bool(re.search(r'\bfor\s*\(', body_text))
        has_while = bool(re.search(r'\bwhile\s*\(', body_text))
        has_do = bool(re.search(r'\bdo\s*\{', body_text))
        has_loops = has_for or has_while or has_do
        has_switch = bool(re.search(r'\bswitch\s*\(', body_text))

        # Count function calls
        call_pattern_re = re.compile(r'\b(\w+)\s*\(')
        skip_kw2 = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'assert',
                    'do', 'else', 'INLINE', 'inline', 'static', 'typeof', 'offsetof',
                    'AOMMIN', 'AOMMAX', 'AOM_MIN', 'AOM_MAX'}
        calls = set()
        for m in call_pattern_re.finditer(body_text):
            w = m.group(1)
            if w not in skip_kw2 and w != func_name:
                calls.add(w)

        is_header = filepath.endswith('.h')

        all_funcs.append({
            'file': rel_path,
            'line': decl_start + 1,
            'name': func_name,
            'body_lines': func_line_count,
            'has_loops': has_loops,
            'has_for': has_for,
            'has_while': has_while,
            'has_switch': has_switch,
            'num_calls': len(calls),
            'is_header': is_header,
            'inline_type': inline_type,
            'category': categorize(filepath),
            'call_list': sorted(list(calls))[:10],  # top 10
        })

        i = body_end + 1
        continue

# Sort by body_lines desc
all_funcs.sort(key=lambda x: -x['body_lines'])

# Output
print(f"\n{'='*90}")
print(f"  INLINE FUNCTION ANALYSIS SUMMARY")
print(f"{'='*90}")
print(f"Total inline functions found: {len(all_funcs)}")
print(f"In header files (.h):        {sum(1 for f in all_funcs if f['is_header'])}")
print(f"In source files (.c):        {sum(1 for f in all_funcs if not f['is_header'])}")
print(f"Functions > 30 lines:        {sum(1 for f in all_funcs if f['body_lines'] > 30)}")
print(f"Functions > 50 lines:        {sum(1 for f in all_funcs if f['body_lines'] > 50)}")
print(f"Functions > 100 lines:       {sum(1 for f in all_funcs if f['body_lines'] > 100)}")
print(f"Functions > 200 lines:       {sum(1 for f in all_funcs if f['body_lines'] > 200)}")
print(f"Functions with loops:        {sum(1 for f in all_funcs if f['has_loops'])}")
print(f"Functions with switch:       {sum(1 for f in all_funcs if f['has_switch'])}")
print(f"Header funcs with loops:     {sum(1 for f in all_funcs if f['has_loops'] and f['is_header'])}")
print(f"Header funcs with switch:    {sum(1 for f in all_funcs if f['has_switch'] and f['is_header'])}")
print(f"Header funcs > 30 lines:     {sum(1 for f in all_funcs if f['body_lines'] > 30 and f['is_header'])}")

from collections import Counter
type_counts = Counter(f['inline_type'] for f in all_funcs)
print(f"\nBy inline type:")
for t, c in type_counts.most_common():
    print(f"  {t}: {c}")

cats = {'common': [], 'encoder': [], 'decoder': [], 'other': []}
for f in all_funcs:
    cats[f['category']].append(f)

print(f"\nBy directory:")
for k in ['common', 'encoder', 'decoder', 'other']:
    h = sum(1 for f in cats[k] if f['is_header'])
    c = sum(1 for f in cats[k] if not f['is_header'])
    big = sum(1 for f in cats[k] if f['body_lines'] > 30)
    print(f"  av2/{k}: {len(cats[k])} total ({h} in .h, {c} in .c, {big} >30 lines)")

# Print top functions by category
for cat_name in ['common', 'encoder', 'decoder', 'other']:
    cat = cats[cat_name]
    if not cat:
        continue
    cat.sort(key=lambda x: -x['body_lines'])

    print(f"\n{'='*90}")
    print(f"  av2/{cat_name}/ - ALL FUNCTIONS >30 LINES (sorted by size)")
    print(f"{'='*90}")

    count = 0
    for f in cat:
        if f['body_lines'] <= 30:
            break
        count += 1
        flags = []
        if f['has_for']: flags.append('FOR')
        if f['has_while']: flags.append('WHILE')
        if f['has_switch']: flags.append('SWITCH')
        if f['num_calls'] > 3: flags.append(f'CALLS={f["num_calls"]}')
        if f['is_header']: flags.append('HEADER')
        flag_str = ' [' + ', '.join(flags) + ']' if flags else ''
        print(f"  {f['body_lines']:4d} lines | {f['file']}:{f['line']} | {f['name']}(){flag_str}")

    if count == 0:
        print("  (none)")

# Also print all header functions with loops or switch > 15 lines
print(f"\n{'='*90}")
print(f"  HEADER FUNCTIONS WITH LOOPS OR SWITCH (>15 lines)")
print(f"{'='*90}")
header_complex = [f for f in all_funcs if f['is_header'] and (f['has_loops'] or f['has_switch']) and f['body_lines'] > 15]
header_complex.sort(key=lambda x: -x['body_lines'])
for f in header_complex:
    flags = []
    if f['has_for']: flags.append('FOR')
    if f['has_while']: flags.append('WHILE')
    if f['has_switch']: flags.append('SWITCH')
    if f['num_calls'] > 0: flags.append(f'CALLS={f["num_calls"]}')
    flag_str = ' [' + ', '.join(flags) + ']'
    print(f"  {f['body_lines']:4d} lines | {f['file']}:{f['line']} | {f['name']}(){flag_str}")

# Also print top 50 largest overall
print(f"\n{'='*90}")
print(f"  TOP 50 LARGEST INLINE FUNCTIONS (overall)")
print(f"{'='*90}")
for i, f in enumerate(all_funcs[:50]):
    flags = []
    if f['has_for']: flags.append('FOR')
    if f['has_while']: flags.append('WHILE')
    if f['has_switch']: flags.append('SWITCH')
    if f['num_calls'] > 0: flags.append(f'CALLS={f["num_calls"]}')
    if f['is_header']: flags.append('HEADER')
    flag_str = ' [' + ', '.join(flags) + ']'
    print(f"  {i+1:3d}. {f['body_lines']:4d} lines | {f['file']}:{f['line']} | {f['name']}() | {f['inline_type']}{flag_str}")

# Save full data as JSON
output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'inline_functions_data.json')
with open(output_path, 'w') as jf:
    json.dump(all_funcs, jf, indent=2)
print(f"\nFull data saved to: {output_path}")

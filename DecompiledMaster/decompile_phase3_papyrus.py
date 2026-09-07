"""Phase 3: Papyrus Script Ecosystem & Native Engine API Registry
Scans all 10,307 extracted .psc files to catalog:
- Every Native C++ Function exposed to Papyrus (the engine bridge)
- Every Engine Event hook dispatched by the Creation Engine
- The complete Script Inheritance Tree (extends hierarchy)
"""

import os, sys, re, glob, json, collections

SOURCE_DIR = r'G:\Fallout 4\Data\Scripts\Source'
OUT_DIR = r'G:\Fallout 4\Tools\DecompiledMaster\data'

# Regex patterns for Papyrus syntax
RE_SCRIPT = re.compile(r'^\s*ScriptName\s+([\w:]+)(?:\s+extends\s+([\w:]+))?', re.IGNORECASE)
RE_NATIVE = re.compile(r'^\s*(?:(\w+)\s+)?Function\s+(\w+)\s*\((.*?)\)\s+Native(?:\s+Global)?', re.IGNORECASE)
RE_EVENT = re.compile(r'^\s*Event\s+(?:(?:On)?(\w+))\s*\((.*?)\)', re.IGNORECASE)

def scan_scripts():
    print(f"[-] [Phase 3.1] Scanning 10,307 Papyrus scripts in {SOURCE_DIR}...")
    files = glob.glob(os.path.join(SOURCE_DIR, '**', '*.psc'), recursive=True)
    print(f"    Found {len(files)} .psc source files.")

    native_funcs = collections.defaultdict(list)
    events = collections.defaultdict(set)
    inheritance = {}
    total_natives = 0
    total_events = 0

    for path in files:
        try:
            with open(path, 'r', encoding='utf-8', errors='replace') as f:
                current_script = None
                for line in f:
                    line_clean = line.strip()
                    if not line_clean or line_clean.startswith(';'):
                        continue

                    # Check ScriptName
                    m_script = RE_SCRIPT.match(line_clean)
                    if m_script:
                        current_script = m_script.group(1)
                        parent = m_script.group(2) or 'None'
                        inheritance[current_script] = parent
                        continue

                    if not current_script:
                        continue

                    # Check Native Function
                    m_nat = RE_NATIVE.match(line_clean)
                    if m_nat:
                        ret_type = m_nat.group(1) or 'None'
                        fn_name = m_nat.group(2)
                        params = m_nat.group(3).strip()
                        is_global = 'global' in line_clean.lower()
                        native_funcs[current_script].append({
                            'function': fn_name,
                            'return_type': ret_type,
                            'parameters': params,
                            'is_global': is_global
                        })
                        total_natives += 1
                        continue

                    # Check Event
                    m_evt = RE_EVENT.match(line_clean)
                    if m_evt:
                        evt_name = 'On' + m_evt.group(1)
                        params = m_evt.group(2).strip()
                        events[evt_name].add(params)
                        total_events += 1
        except Exception as e:
            continue

    print(f"    [+] Extracted {total_natives} Native Function declarations across {len(native_funcs)} scripts.")
    print(f"    [+] Cataloged {len(events)} unique Engine Events.")
    print(f"    [+] Mapped {len(inheritance)} script inheritance relationships.")

    # Save Native Functions
    out_nat = os.path.join(OUT_DIR, 'papyrus_native_functions.json')
    with open(out_nat, 'w', encoding='utf-8') as f:
        json.dump({
            'total_native_functions': total_natives,
            'scripts_with_native_bindings': len(native_funcs),
            'native_bindings_by_script': native_funcs
        }, f, indent=2)

    # Save Engine Events
    out_evt = os.path.join(OUT_DIR, 'papyrus_engine_events.json')
    evt_list = []
    for ename, param_sets in sorted(events.items()):
        evt_list.append({
            'event': ename,
            'signatures': sorted(list(param_sets))
        })
    with open(out_evt, 'w', encoding='utf-8') as f:
        json.dump({
            'total_unique_events': len(events),
            'events': evt_list
        }, f, indent=2)

    # Save Inheritance
    out_inh = os.path.join(OUT_DIR, 'papyrus_script_hierarchy.json')
    with open(out_inh, 'w', encoding='utf-8') as f:
        json.dump(inheritance, f, indent=2)

    print("    [+] Saved all Phase 3 datasets successfully.")

if __name__ == '__main__':
    print("=== STARTING PHASE 3: PAPYRUS SCRIPT ECOSYSTEM & NATIVE API REGISTRY ===")
    scan_scripts()
    print("=== PHASE 3 COMPLETE ===")

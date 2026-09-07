"""Phase 2: Master ESM Game Systems Decompilation
Extracts structured datasets for:
- CSTY (Combat Styles - offensive/defensive, cover, rush, flanking)
- PACK (AI Packages - sandbox, travel, patrol, escort, combat procedures)
- FACT (Factions - inter-faction hostility & reaction matrix)
- AVIF (Actor Values - derived stats, limb damage conditions, speed scaling)
- PERK (Perks & Entry Points - native engine math hooks)
- WEAP (Weapons - ballistics, damage, fire rates, aim models)
- ARMO (Armor - body slots, damage resistance curves)
- AMMO & PROJ (Ammunition & Projectiles - velocities, gravity, blast radii)
- OMOD (Object Modifications - attachment keywords, property modifiers)
- RACE (Races - body parts, movement types, skeleton bindings)
- MGEF & SPEL (Magic Effects & Spells - archetypes, delivery, script attachments)
"""

import os, sys, struct, json, collections

sys.path.insert(0, r'G:\fo4 ai mod\tools')
import esplib

ESM_PATH = r'G:\Fallout 4\Data\Fallout4.esm'
OUT_DIR = r'G:\Fallout 4\Tools\DecompiledMaster\data'

def get_str(rec, sub_type):
    s = rec.sub(sub_type)
    if not s:
        return ''
    if len(s.data) == 4 and (rec.flags & esplib.FLAG_LOCALIZED):
        sid = struct.unpack('<I', s.data)[0]
        return f'[StringID:0x{sid:08X}]'
    return s.data.rstrip(b'\x00').decode('utf-8', 'replace')

def decompile_csty():
    print("[-] [Phase 2.1] Decompiling Combat Styles (CSTY)...")
    cstys = []
    for rec in esplib.iter_records(ESM_PATH, ['CSTY']):
        edid = rec.editor_id() or 'Unknown'
        csgd = rec.sub('CSGD')
        csgd_floats = []
        if csgd and len(csgd.data) >= 16:
            count = len(csgd.data) // 4
            csgd_floats = [round(struct.unpack_from('<f', csgd.data, i * 4)[0], 3) for i in range(count)]

        cstys.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'subrecords': [s.type.decode('ascii', 'replace') for s in rec.subs],
            'general_data_floats': csgd_floats
        })

    cstys.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'csty_combat_styles.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(cstys, f, indent=2)
    print(f"    [+] Saved {len(cstys)} Combat Styles to {out_path}")
    return cstys

def decompile_pack():
    print("[-] [Phase 2.2] Decompiling AI Packages (PACK)...")
    packages = []
    type_names = {
        0: 'Explore', 1: 'Follow', 2: 'Escort', 3: 'Eat', 4: 'Sleep', 5: 'Wander',
        6: 'Travel', 7: 'Accompany', 8: 'UseItemAt', 9: 'Ambush', 10: 'FleeNotCombat',
        11: 'CastMagic', 12: 'Sandbox', 13: 'Patrol', 14: 'Guard', 15: 'Dialogue',
        16: 'UseWeapon', 17: 'Find', 18: 'Custom', 19: 'Combat'
    }
    for rec in esplib.iter_records(ESM_PATH, ['PACK']):
        edid = rec.editor_id() or 'Unknown'
        pkdt = rec.sub('PKDT')
        p_type = 'Unknown'
        flags = 0
        if pkdt and len(pkdt.data) >= 8:
            flags, raw_type = struct.unpack_from('<II', pkdt.data, 0)
            p_type = type_names.get(raw_type, f'Type_{raw_type}')

        packages.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'procedure_type': p_type,
            'flags_hex': f'0x{flags:08X}'
        })

    packages.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'pack_ai_packages.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(packages, f, indent=2)
    print(f"    [+] Saved {len(packages)} AI Packages to {out_path}")
    return packages

def decompile_weapons():
    print("[-] [Phase 2.3] Decompiling Weapons (WEAP)...")
    weapons = []
    for rec in esplib.iter_records(ESM_PATH, ['WEAP']):
        edid = rec.editor_id() or 'Unknown'
        name = get_str(rec, 'FULL')
        desc = get_str(rec, 'DESC')
        
        # DNAM subrecord contains weapon data
        dnam = rec.sub('DNAM')
        ammo_fid = ''
        ammo_sub = rec.sub('AMMO')
        if ammo_sub and len(ammo_sub.data) >= 4:
            ammo_fid = f'0x{struct.unpack_from("<I", ammo_sub.data, 0)[0]:08X}'

        weapons.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'name': name,
            'ammo_formid': ammo_fid,
            'desc': desc
        })

    weapons.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'weap_weapons.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(weapons, f, indent=2)
    print(f"    [+] Saved {len(weapons)} Weapons to {out_path}")
    return weapons

def decompile_armor():
    print("[-] [Phase 2.4] Decompiling Armor (ARMO)...")
    armors = []
    for rec in esplib.iter_records(ESM_PATH, ['ARMO']):
        edid = rec.editor_id() or 'Unknown'
        name = get_str(rec, 'FULL')
        
        # BOD2 - body template slots
        bod2 = rec.sub('BOD2')
        slots = 0
        if bod2 and len(bod2.data) >= 4:
            slots = struct.unpack_from('<I', bod2.data, 0)[0]

        armors.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'name': name,
            'body_slots_mask': f'0x{slots:08X}'
        })

    armors.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'armo_armor.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(armors, f, indent=2)
    print(f"    [+] Saved {len(armors)} Armor records to {out_path}")
    return armors

def decompile_projectiles():
    print("[-] [Phase 2.5] Decompiling Projectiles & Ammo (PROJ & AMMO)...")
    projs = []
    for rec in esplib.iter_records(ESM_PATH, ['PROJ']):
        edid = rec.editor_id() or 'Unknown'
        name = get_str(rec, 'FULL')
        data_sub = rec.sub('DATA')
        speed, gravity, range_val = 0.0, 0.0, 0.0
        if data_sub and len(data_sub.data) >= 12:
            speed, gravity, range_val = struct.unpack_from('<fff', data_sub.data, 0)

        projs.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'name': name,
            'speed': round(speed, 2),
            'gravity_mult': round(gravity, 3),
            'range': round(range_val, 2)
        })

    projs.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'proj_projectiles.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(projs, f, indent=2)
    print(f"    [+] Saved {len(projs)} Projectiles to {out_path}")
    return projs

def decompile_races():
    print("[-] [Phase 2.6] Decompiling Races (RACE)...")
    races = []
    for rec in esplib.iter_records(ESM_PATH, ['RACE']):
        edid = rec.editor_id() or 'Unknown'
        name = get_str(rec, 'FULL')
        desc = get_str(rec, 'DESC')

        # Count body parts / biped data
        body_parts = sum(1 for s in rec.subs if s.type == b'BPND')

        races.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'name': name,
            'body_parts_count': body_parts,
            'desc': desc
        })

    races.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'race_races.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(races, f, indent=2)
    print(f"    [+] Saved {len(races)} Races to {out_path}")
    return races

def decompile_magic_effects():
    print("[-] [Phase 2.7] Decompiling Magic Effects (MGEF)...")
    mgefs = []
    for rec in esplib.iter_records(ESM_PATH, ['MGEF']):
        edid = rec.editor_id() or 'Unknown'
        name = get_str(rec, 'FULL')
        desc = get_str(rec, 'DESC')

        # Script count
        scripts = sum(1 for s in rec.subs if s.type == b'VMAD')

        mgefs.append({
            'edid': edid,
            'formid': f'0x{rec.form_id:08X}',
            'name': name,
            'has_scripts': bool(scripts),
            'desc': desc
        })

    mgefs.sort(key=lambda x: x['edid'])
    out_path = os.path.join(OUT_DIR, 'mgef_magic_effects.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(mgefs, f, indent=2)
    print(f"    [+] Saved {len(mgefs)} Magic Effects to {out_path}")
    return mgefs

if __name__ == '__main__':
    print("=== STARTING PHASE 2: MASTER ESM GAME SYSTEMS DECOMPILATION ===")
    decompile_csty()
    decompile_pack()
    decompile_weapons()
    decompile_armor()
    decompile_projectiles()
    decompile_races()
    decompile_magic_effects()
    print("=== PHASE 2 COMPLETE ===")

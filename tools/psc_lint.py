#!/usr/bin/env python3
"""M7 · Papyrus linter (CK-free subset of the CK compile-time checks).

What it CAN verify without the Creation Kit:
  - ScriptName header + name/file match + known base type
  - block balance (Function/EndFunction, Event, If, While, For, Switch, Try)
  - per-line parenthesis / quote balance (string-aware)
  - Papyrus natives are case-sensitive: flag lower/misspelled calls of the
    native set the scripts use
  - statement-terminator misuse (Papyrus uses newlines, not semicolons)
  - tab characters, trailing whitespace, CRLF
  - parameter naming convention (a* prefix) — warning only

What it CANNOT verify: native signatures/arg order, property linkage,
compilation — that needs the CK (docs/INTEGRATION_CHECKLIST.md, step 5).

Usage: tools/psc_lint.py [files ...]   (default: scripts/**/*.psc)
Exit: 1 on errors, 0 otherwise (warnings allowed unless --warnings-fatal).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

KNOWN_BASES = {
    "Script", "ObjectReference", "Actor", "Player", "Quest", "Form",
    "Item", "MiscObject", "Weapon", "Armor", "Book", "Key", "Misc",
    "AlchemyItem", "Ammo", "MagicEffect", "Spell", "SpellList",
    "Enchantment", "Faction", "Action", "Projectile", "Sound",
    "Material", "TextureSet", "VoiceType", "Package", "LeveledList",
    "LeveledActor", "LeveledItem", "Alias", "ReferenceAlias",
    "Scene", "Shout", "Topic", "TopicType", "Voice", "CameraShot",
    "Camera", "PipboyNote", " perk".strip(), "Perk", "Ingredient",
    "AlchemicItem", "Flora", "Furniture", "Activator", "Doors", "Door",
    "Container", "Light", "Static", "StaticObject", "NPC", "NPCType",
    "Race", "HeadPart", "Hair", "Eyes", "FaceMorph", "Head",
    "VoiceType", "ArmedWeapon", "HandGrip", "Weapon", "Apparatus",
    "Hazard", "HazardType", "Pick", "PickSet", "Mouth", "SoundDescriptor",
    "SoundCategory", "SoundFile", "MusicType", "MusicTrack", "Playlist",
    "UI", "QuestScript", "MagicItem", "MagicSource", "ScriptObject",
    "PerkedObject", "Perk", "PapyrusFunction", "PapyrusType",
}

# Native call names the shipped scripts use (case-sensitive in Papyrus).
NATIVE_CALLS = {
    "SetINIFloat", "SetINIInt", "SetINIBool", "GetINIBool", "GetINIFloat",
    "GetINIInt", "IntToString", "FloatToString", "SendConsoleCommand",
    "RegisterForGameEvent", "UnregisterForGameEvent", "RegisterForModEvent",
    "GetPlayer", "PlayText", "PlaySound", "Notification", "GetGameSettingFloat",
    "GetGameSettingInt", "Wait", "IsInMenuMode", "GetSeconds", "GetDays",
    "SetINIBool", "Debug", "Utility", "Game", "PlayMusic", "MessageBox",
    "MessageMenu", "EnableInput", "DisableInput", "GetInputEnabled",
    "StartGame", "SaveGame", "LoadGame", "ShowOnScreenMessage",
    "SendSEAction", "GetSEActionPressed", "ShowConsole", "HideConsole",
    "LogMessage", "EnableDebugLog", "DisableDebugLog", "GetINIBoolean",
}

BLOCK_OPEN = {
    "function": "endfunction", "event": "endevent", "if": "endif",
    "while": "endwhile", "for": "endfor", "switch": "endswitch",
    "try": "endtry",
}
BLOCK_CLOSE = {"endfunction", "endevent", "endif", "endwhile", "endfor",
               "endswitch", "endtry", "catch", "endcatch", "finally", "endfinally"}

RESERVED = {
    "if", "else", "elseif", "endif", "while", "endwhile", "for", "endfor",
    "switch", "endswitch", "case", "default", "return", "break", "continue",
    "function", "endfunction", "event", "endevent", "try", "catch", "endcatch",
    "finally", "endfinally", "scriptname", "extends", "property", "global",
    "int", "float", "bool", "string", "objectreference", "actor", "quest",
    "form", "item", "sound", "none", "true", "false", "and", "or", "not",
}


def strip_comment(line: str) -> str:
    """Remove a trailing ; comment (string-aware)."""
    out = []
    in_str = False
    for ch in line:
        if ch == '"':
            in_str = not in_str
        if ch == ";" and not in_str:
            break
        out.append(ch)
    return "".join(out)


def lint_file(path: Path):
    errors, warnings = [], []
    err = lambda ln, m: errors.append(f"{path.name}:{ln}: {m}")
    warn = lambda ln, m: warnings.append(f"{path.name}:{ln}: {m}")

    text = path.read_text(encoding="utf-8-sig")
    if "\r\n" in text:
        warn(0, "CRLF line endings (use LF)")
    lines = text.splitlines()
    if not lines:
        err(0, "empty script")
        return errors, warnings

    m = re.match(r'^(?P<kw>ScriptName)\s+(?P<name>[A-Za-z_]\w*)\s+extends\s+(?P<base>[A-Za-z_]\w*)',
                 lines[0])
    if not m:
        err(1, "first line must be: ScriptName <Name> extends <Base>")
        return errors, warnings
    if m.group("name") != path.stem:
        err(1, f"ScriptName '{m.group('name')}' != file name '{path.stem}'")
    if m.group("base") not in KNOWN_BASES:
        warn(1, f"unusual base type '{m.group('base')}'")

    stack = []
    for i, raw in enumerate(lines, 1):
        if "\t" in raw:
            warn(i, "tab character (use 4 spaces)")
        if raw != raw.rstrip():
            warn(i, "trailing whitespace")
        line = strip_comment(raw).strip()
        if not line:
            continue

        # string balance
        if line.count('"') % 2 != 0:
            err(i, "unbalanced string quote")
        # paren balance (string-aware)
        depth, in_str = 0, False
        for ch in strip_comment(raw):
            if ch == '"':
                in_str = not in_str
            elif not in_str:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                if depth < 0:
                    err(i, "unbalanced ')'")
                    break
        if depth > 0:
            err(i, "unbalanced '('" )
        # semicolon statement terminator misuse
        if line.endswith(";"):
            err(i, "Papyrus statements end with newlines, not ';'")

        low = line.lower()
        words = low.split()
        kw = words[0] if words else ""
        # return-typed blocks: "float Function X" / "int Function Y"
        if kw not in BLOCK_OPEN and kw not in BLOCK_CLOSE and len(words) > 1 \
                and words[1] in BLOCK_OPEN:
            kw = words[1]

        # native case-sensitivity: look for `Xxx.` receiver calls and bare calls
        for token in re.findall(r"[A-Za-z_]\w*", strip_comment(raw)):
            tl = token.lower()
            if tl in {n.lower() for n in NATIVE_CALLS} and token not in NATIVE_CALLS:
                # only flag when it looks like a call (followed by '(') or a
                # receiver (followed by '.')
                idx = strip_comment(raw).find(token)
                rest = strip_comment(raw)[idx + len(token):].lstrip()
                if rest.startswith("(") or rest.startswith("."):
                    correct = next(n for n in NATIVE_CALLS if n.lower() == tl)
                    err(i, f"native '{token}' wrong case (Papyrus is case-sensitive) -> {correct}")

        # block structure
        if kw in BLOCK_OPEN:
            stack.append((kw, i))
        elif kw in BLOCK_CLOSE:
            if kw == "catch" or kw == "finally":
                continue  # matched by endtry; not stack-managed here
            expect = None
            for k in range(len(stack) - 1, -1, -1):
                if BLOCK_OPEN.get(stack[k][0]) == kw:
                    expect = stack[k]
                    del stack[k:]
                    break
            if expect is None:
                err(i, f"unexpected '{kw}' (no matching opener)")
        elif low.startswith("else") and kw == "elseif":
            if not stack or stack[-1][0] != "if":
                err(i, "elseif without if")
            else:
                stack.pop()
                stack.append(("if", i))
        elif kw == "else":
            if not stack or stack[-1][0] != "if":
                err(i, "else without if")
            else:
                stack.pop()
                stack.append(("if", i))

    for kw, ln in stack:
        err(0, f"unclosed '{kw}' opened at line {ln}")

    # parameter naming convention (warning)
    for i, raw in enumerate(lines, 1):
        m = re.match(r"^\s*(?:function|event)\s+\S+\s+\w+\s*\(([^)]*)\)", raw, re.I)
        if m and m.group(1).strip():
            for p in m.group(1).split(","):
                bits = p.strip().split()
                if len(bits) == 2 and bits[0].lower() not in RESERVED:
                    name = bits[1]
                    if not name.startswith("a"):
                        warn(i, f"parameter '{name}' should use a* prefix (Papyrus convention)")
    return errors, warnings


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    warnings_fatal = "--warnings-fatal" in argv
    files = [Path(a) for a in argv if not a.startswith("--")]
    if not files:
        files = sorted((ROOT / "scripts").rglob("*.psc"))
    if not files:
        print("no .psc files found")
        return 1
    total_e = total_w = 0
    for f in files:
        if not f.exists():
            print(f"ERROR {f}: not found")
            total_e += 1
            continue
        e, w = lint_file(f)
        total_e += len(e)
        total_w += len(w)
        for line in e:
            print("ERROR  " + line)
        for line in w:
            print("WARN   " + line)
        print(f"{'FAIL' if e else 'OK'}   {f.relative_to(ROOT)}  ({len(e)} err, {len(w)} warn)")
    print(f"\n{len(files)} file(s), {total_e} error(s), {total_w} warning(s)")
    if total_e:
        return 1
    if total_w and warnings_fatal:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

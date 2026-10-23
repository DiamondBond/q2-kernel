#!/usr/bin/env python3
import os
import sys

TOOLCHAIN_KEYS = [
    "CONFIG_CC_VERSION_TEXT",
    "CONFIG_GCC_VERSION",
    "CONFIG_LD_VERSION",
    "CONFIG_AS_VERSION",
    "CONFIG_CC_CAN_LINK",
    "CONFIG_CC_CAN_LINK_STATIC",
    "CONFIG_CC_IS_GCC",
    "CONFIG_CC_IS_CLANG",
    "CONFIG_LD_IS_LLD",
    "CONFIG_GCC_PLUGINS",
]

def extract_entries(path):
    entries = {}
    if not os.path.exists(path):
        return entries
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            key = None
            if line.startswith('CONFIG_'):
                key = line.split('=', 1)[0].strip()
            elif line.startswith('# CONFIG_') and line.rstrip().endswith('is not set'):
                key = line.split()[1]
            if key in TOOLCHAIN_KEYS:
                entries[key] = line.rstrip('\n')
    return entries

def save(old_cfg, cache):
    entries = extract_entries(old_cfg)
    if not entries:
        if os.path.exists(cache):
            os.remove(cache)
        return
    with open(cache, 'w', encoding='utf-8') as f:
        for key, line in entries.items():
            f.write(f"{key}::{line}\n")

def restore(new_cfg, cache):
    if not os.path.exists(cache) or not os.path.exists(new_cfg):
        return
    saved = {}
    with open(cache, 'r', encoding='utf-8') as f:
        for line in f:
            if '::' not in line:
                continue
            key, value = line.rstrip('\n').split('::', 1)
            saved[key] = value
    if not saved:
        return
    with open(new_cfg, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    def line_key(line):
        if line.startswith('CONFIG_'):
            return line.split('=', 1)[0].strip()
        if line.startswith('# CONFIG_') and line.rstrip().endswith('is not set'):
            return line.split()[1]
        return None
    for idx, line in enumerate(lines):
        key = line_key(line)
        if key in saved:
            lines[idx] = saved[key] + '\n'
            saved.pop(key)
    for key, line in saved.items():
        lines.append(line + '\n')
    with open(new_cfg, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    os.remove(cache)

def main():
    if len(sys.argv) < 4:
        sys.exit(0)
    mode, cfg, cache = sys.argv[1:4]
    if mode == 'save':
        save(cfg, cache)
    elif mode == 'restore':
        restore(cfg, cache)

if __name__ == '__main__':
    main()

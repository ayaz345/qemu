#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import json
import shlex
import subprocess

def find_command(src, target, compile_commands):
    for command in compile_commands:
        if command['file'] != src:
            continue
        if target != '' and command['command'].find(target) == -1:
            continue
        return command['command']
    return 'false'

def process_command(src, command):
    skip = False
    out = []
    for item in shlex.split(command):
        if skip:
            skip = False
            continue
        if item in ['-MF', '-MQ', '-o']:
            skip = True
            continue
        if item == '-c':
            skip = True
            continue
        out.append(item)
    out.extend(('-DQEMU_MODINFO', '-E', src))
    return out

def main(args):
    target = ''
    if args[0] == '--target':
        args.pop(0)
        target = args.pop(0)
        print(f"MODINFO_DEBUG target {target}")
        arch = target[:-8] # cut '-softmmu'
        print("MODINFO_START arch \"%s\" MODINFO_END" % arch)
    with open('compile_commands.json') as f:
        compile_commands = json.load(f)
    for src in args:
        if not src.endswith('.c'):
            print(f"MODINFO_DEBUG skip {src}")
            continue
        print(f"MODINFO_DEBUG src {src}")
        command = find_command(src, target, compile_commands)
        cmdline = process_command(src, command)
        print("MODINFO_DEBUG cmd", cmdline)
        result = subprocess.run(cmdline, stdout = subprocess.PIPE,
                                universal_newlines = True)
        if result.returncode != 0:
            sys.exit(result.returncode)
        for line in result.stdout.split('\n'):
            if line.find('MODINFO') != -1:
                print(line)

if __name__ == "__main__":
    main(sys.argv[1:])

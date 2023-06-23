#!/usr/bin/env python3
#
# check-dco.py: validate all commits are signed off
#
# Copyright (C) 2020 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import os.path
import sys
import subprocess

namespace = sys.argv[1] if len(sys.argv) >= 2 else "qemu-project"
cwd = os.getcwd()
reponame = os.path.basename(cwd)
repourl = f"https://gitlab.com/{namespace}/{reponame}.git"

subprocess.check_call(["git", "remote", "add", "check-dco", repourl])
subprocess.check_call(["git", "fetch", "check-dco", "master"],
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL)

ancestor = subprocess.check_output(["git", "merge-base",
                                    "check-dco/master", "HEAD"],
                                   universal_newlines=True)

ancestor = ancestor.strip()

subprocess.check_call(["git", "remote", "rm", "check-dco"])

errors = False

print("\nChecking for 'Signed-off-by: NAME <EMAIL>' " +
      "on all commits since %s...\n" % ancestor)

log = subprocess.check_output(
    ["git", "log", "--format=%H %s", f"{ancestor}..."], universal_newlines=True
)

if log == "":
    commits = []
else:
    commits = [[c[:40], c[41:]] for c in log.strip().split("\n")]

for sha, subject in commits:

    msg = subprocess.check_output(["git", "show", "-s", sha],
                                  universal_newlines=True)
    lines = msg.strip().split("\n")

    print(f"🔍 {sha} {subject}")
    sob = False
    for line in lines:
        if "Signed-off-by:" in line:
            sob = True
            if "localhost" in line:
                print(f"    ❌ FAIL: bad email in {line}")
                errors = True

    if not sob:
        print("    ❌ FAIL missing Signed-off-by tag")
        errors = True

if errors:
    print("""

❌ ERROR: One or more commits are missing a valid Signed-off-By tag.


This project requires all contributors to assert that their contributions
are provided in compliance with the terms of the Developer's Certificate
of Origin 1.1 (DCO):

  https://developercertificate.org/

To indicate acceptance of the DCO every commit must have a tag

  Signed-off-by: REAL NAME <EMAIL>

This can be achieved by passing the "-s" flag to the "git commit" command.

To bulk update all commits on current branch "git rebase" can be used:

  git rebase -i master -x 'git commit --amend --no-edit -s'

""")

    sys.exit(1)

sys.exit(0)

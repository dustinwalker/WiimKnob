# PlatformIO pre-build script: writes include/FwVersion.h from `git describe`, only when it changes,
# so an unchanged version doesn't force a rebuild.
import os
import subprocess

Import("env")  # noqa: F821

root = env["PROJECT_DIR"]  # noqa: F821
try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--dirty", "--always"], cwd=root, text=True).strip()
except (OSError, subprocess.CalledProcessError):
    version = "unknown"

path = os.path.join(root, "include", "FwVersion.h")
content = '#pragma once\n#define FW_VERSION "%s"\n' % version
try:
    with open(path) as f:
        current = f.read()
except OSError:
    current = None
if current != content:
    with open(path, "w") as f:
        f.write(content)
print("Firmware version:", version)

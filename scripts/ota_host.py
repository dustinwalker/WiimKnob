# PlatformIO pre-script for the OTA env: each knob answers to wiimknob-<last 4 MAC hex>.local, so
# the target comes from WIIMKNOB_HOST ("a2a8", "wiimknob-a2a8" or an IP) or --upload-port.
import os

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: F821

Import("env")  # noqa: F821

host = os.environ.get("WIIMKNOB_HOST", "").strip()
if host:
    if not host.startswith("wiimknob-") and "." not in host:
        host = "wiimknob-" + host
    if not host[0].isdigit() and not host.endswith(".local"):
        host += ".local"
    env.Replace(UPLOAD_PORT=host)  # noqa: F821
elif "upload" in COMMAND_LINE_TARGETS and not env.subst("$UPLOAD_PORT"):  # noqa: F821
    print("Set WIIMKNOB_HOST to the knob's name from its Network page, e.g. WIIMKNOB_HOST=a2a8")
    env.Exit(1)  # noqa: F821

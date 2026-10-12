"""Install a translation-only OpenRouter key from an interactive hidden prompt."""
from __future__ import annotations

import getpass
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ENVIRONMENT = Path("/etc/qssm-chat-translate/environment")
DISABLED_FILE = Path("/var/lib/qssm-chat-translate/translation.disabled")
SERVICE = "qssm-chat-translate.service"


def save_key(environment: Path, key: str):
    if not re.fullmatch(r"sk-or-[A-Za-z0-9_-]{24,200}", key):
        raise ValueError("Expected an OpenRouter API key, with no spaces or line breaks")
    if environment.is_symlink():
        raise ValueError("The private environment file must not be a symlink")
    original = environment.stat()
    lines = environment.read_text().splitlines()
    lines = [line for line in lines if not re.match(
        r"^\s*(?:export\s+)?OPENROUTER_API_KEY\s*=", line)]
    content = "\n".join([*lines, "OPENROUTER_API_KEY=" + key, ""])
    descriptor, temporary = tempfile.mkstemp(prefix=".environment-", dir=environment.parent)
    try:
        with os.fdopen(descriptor, "w") as handle:
            os.fchmod(handle.fileno(), 0o600)
            if os.geteuid() == 0:
                os.fchown(handle.fileno(), original.st_uid, original.st_gid)
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, environment)
        directory = os.open(environment.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    if os.geteuid() != 0:
        print("Run this installer with sudo.", file=sys.stderr)
        return 1
    if not sys.stdin.isatty() or not sys.stderr.isatty():
        print("An interactive terminal is required; the key will never be read visibly.", file=sys.stderr)
        return 1
    if not DISABLED_FILE.is_file():
        print("Keep translation disabled until the supplied key is verified.", file=sys.stderr)
        return 1
    try:
        key = getpass.getpass("OpenRouter API key (hidden): ").strip()
        save_key(ENVIRONMENT, key)
    except (EOFError, KeyboardInterrupt):
        print("\nInstallation canceled.", file=sys.stderr)
        return 1
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    except OSError:
        print("Could not update the private environment file.", file=sys.stderr)
        return 1
    result = subprocess.run(["systemctl", "restart", SERVICE], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, check=False)
    if result.returncode:
        print("Key saved privately, but the service restart needs attention.", file=sys.stderr)
        return 1
    print("OpenRouter key saved privately. Translation restarted and remains disabled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

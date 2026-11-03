"""Assert the integer mixer exports identical WAV bytes on every core CI host."""
import hashlib
from pathlib import Path
import sys

expected = "79e190e03151525c844b882cc2c3174fe6be7651d9fe29d7dc2584b7b72c301a"
actual = hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f"Rendered WAV differs: {actual} (expected {expected})")
print("Deterministic WAV SHA-256:", actual)

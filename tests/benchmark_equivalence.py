"""Verify benchmark modes agree and queued rejection cannot hang shutdown."""
import subprocess
import sys
import tempfile
from pathlib import Path

BENCHMARK, GENERATOR = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "generated.orders"
    subprocess.run([GENERATOR, str(source), "10000", "42"], check=True, timeout=10)
    expected = None
    for mode in ("direct", "queued"):
        for capacity in (1, 64, 4096):
            for reserve in ([], ["10000"]):
                result = subprocess.run([BENCHMARK, str(source), mode, str(capacity)] + reserve,
                                        check=True, capture_output=True, text=True, timeout=10)
                fields = dict(item.split("=", 1) for item in result.stdout.split())
                signature = tuple(fields[key] for key in ("commands", "trades", "resting_orders", "digest"))
                if expected is None:
                    expected = signature
                if signature != expected:
                    raise AssertionError("benchmark modes produce different results")
    source.write_text("ADD 1 BUY 1000 1\n" * 10000)
    for mode in ("direct", "queued"):
        result = subprocess.run([BENCHMARK, str(source), mode, "1"],
                                capture_output=True, text=True, timeout=5)
        if result.returncode != 1 or "duplicate live order ID" not in result.stderr:
            raise AssertionError("failed to propagate engine rejection")
print("PASS benchmark digest equivalence and queued error shutdown")

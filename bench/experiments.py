"""Repeat reproducible Linux experiments; reject any change in matching results."""
import argparse
import csv
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("file", type=Path)
parser.add_argument("--build", type=Path, default=Path("build"))
parser.add_argument("--runs", type=int, default=5)
parser.add_argument("--reserve", type=int, default=1000000)
parser.add_argument("--cpus", help="optional taskset CPU list, e.g. 2,3")
parser.add_argument("--perf", action="store_true", help="run perf stat for each trial")
parser.add_argument("--output", type=Path, default=Path("build/experiments.csv"))
args = parser.parse_args()
if args.runs < 1 or args.reserve < 1:
    parser.error("runs and reserve must be positive")
binary = (args.build / "benchmark").resolve()
workload = args.file.resolve()
args.output.parent.mkdir(parents=True, exist_ok=True)
metadata = {
    "platform": platform.platform(), "processor": platform.processor(),
    "workload": str(workload), "runs": args.runs, "cpus": args.cpus, "perf": args.perf,
}
with workload.open("rb") as source:
    checksum = hashlib.sha256()
    for chunk in iter(lambda: source.read(1024 * 1024), b""):
        checksum.update(chunk)
metadata["workload_sha256"] = checksum.hexdigest()
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    metadata["cpu_model"] = next((line.split(":", 1)[1].strip() for line in cpuinfo.read_text().splitlines()
                                 if line.startswith("model name")), "unknown")
cache = args.build / "CMakeCache.txt"
if cache.exists():
    metadata["cmake"] = [line for line in cache.read_text().splitlines() if line.startswith(
        ("CMAKE_CXX_COMPILER:", "CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS"))]
args.output.with_suffix(".metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
configs = [("direct", 4096, False), ("direct", 4096, True)]
configs += [("queued", capacity, reserve) for capacity in (1, 64, 4096) for reserve in (False, True)]
expected = None
with args.output.open("w", newline="") as output:
    writer = csv.DictWriter(output, fieldnames=["mode", "capacity", "reserve", "run", "processing_ms",
                                                "commands_per_second", "mean_ns_per_command", "digest"])
    writer.writeheader()
    for mode, capacity, reserve in configs:
        rates = []
        for run in range(args.runs):
            command = [str(binary), str(workload), mode, str(capacity)]
            if reserve:
                command.append(str(args.reserve))
            if args.cpus:
                command = ["taskset", "-c", args.cpus] + command
            if args.perf:
                perf_file = args.output.with_name(f"{args.output.stem}.{mode}.{capacity}.{int(reserve)}.{run}.perf")
                command = ["perf", "stat", "-o", str(perf_file), "-e",
                           "cycles,instructions,cache-misses,context-switches", "--"] + command
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            fields = dict(item.split("=", 1) for item in result.stdout.split())
            signature = tuple(fields[key] for key in ("commands", "trades", "resting_orders", "digest"))
            if expected is None:
                expected = signature
            if signature != expected:
                raise RuntimeError("matching output changed: " + repr(signature))
            writer.writerow({"mode": mode, "capacity": capacity, "reserve": int(reserve), "run": run,
                             **{key: fields[key] for key in ("processing_ms", "commands_per_second",
                                                              "mean_ns_per_command", "digest")}})
            output.flush()
            rates.append(float(fields["commands_per_second"]))
        print(f"{mode} capacity={capacity} reserve={int(reserve)} median_commands_per_second={statistics.median(rates):.3f}")

"""Linux loopback tests; invoked by CTest. Python standard library only."""
import selectors
import signal
import socket
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FEED, SENDER, REPLAY = sys.argv[1:]


def packet(sequence, kind=1, side=2, order_id=1, price=1000, quantity=100, symbol=7):
    return struct.pack("!4sIQBB6xQQQ", b"ME01", symbol, sequence, kind, side, order_id, price, quantity)


def start(directory, name, idle=500):
    output = Path(directory) / (name + ".trades")
    process = subprocess.Popen(
        [FEED, "0", "7", "4096", str(idle), str(output)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            if not selector.select(5):
                raise AssertionError("feed did not become ready")
        ready = process.stdout.readline().strip()
        if not ready.startswith("READY port="):
            raise AssertionError("unexpected startup: " + ready)
        return process, int(ready.split("=")[1]), output
    except BaseException:
        process.kill()
        process.communicate()
        raise


def finish(process, expected=0):
    try:
        stdout, stderr = process.communicate(timeout=6)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        raise AssertionError("feed failed to drain/shutdown")
    if process.returncode != expected:
        raise AssertionError((process.returncode, stdout, stderr))
    return dict(field.split("=", 1) for field in stdout.split())


def check(condition, message):
    if not condition:
        raise AssertionError(message)


with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "input.orders"
    source.write_text("ADD 1 SELL 1000 100\nADD 2 BUY 1005 40\nCANCEL 1\n")
    baseline = subprocess.run([REPLAY, str(source)], check=True, capture_output=True, text=True)
    expected_trades = "\n".join(line for line in baseline.stdout.splitlines() if line.startswith("TRADE")) + "\n"

    process, port, output = start(directory, "sender", 1500)
    try:
        subprocess.run([SENDER, str(source), "127.0.0.1", str(port), "7", "1000"], check=True, timeout=5)
        result = finish(process)
        check(result["processed"] == "3" and result["resting_orders"] == "0", result)
        check(output.read_text() == expected_trades, "UDP trades differ from replay")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    # Duplicate, malformed, oversized, wrong-symbol and other-publisher packets.
    process, port, output = start(directory, "validation", 1500)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as publisher:
            destination = ("127.0.0.1", port)
            publisher.sendto(packet(1), destination)
            publisher.sendto(packet(1), destination)
            publisher.sendto(b"bad", destination)
            publisher.sendto(packet(2) + b"oversize", destination)
            publisher.sendto(packet(2, symbol=8), destination)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                foreign.sendto(packet(2), destination)
            publisher.sendto(packet(2, side=1, order_id=2, price=1005, quantity=40), destination)
        result = finish(process)
        check(result["processed"] == "2" and result["duplicates"] == "1", result)
        check(result["malformed"] == "2" and result["foreign"] == "2", result)
        check(output.read_text() == expected_trades, "validated trade stream differs")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    process, port, _ = start(directory, "gap")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as publisher:
            publisher.sendto(packet(1), ("127.0.0.1", port))
            publisher.sendto(packet(3), ("127.0.0.1", port))
        result = finish(process, 1)
        check(result["gap"] == "1" and result["processed"] == "1", result)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    process, port, _ = start(directory, "rejection")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as publisher:
            for datagram in (packet(1), packet(2), packet(3, kind=2, side=0, price=0, quantity=0)):
                publisher.sendto(datagram, ("127.0.0.1", port))
        result = finish(process, 1)
        check(result["accepted"] == "3" and result["processed"] == "2" and result["rejected"] == "1", result)
        check(result["cancelled"] == "1" and result["resting_orders"] == "0", result)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    process, _, _ = start(directory, "signal", 10000)
    process.send_signal(signal.SIGTERM)
    result = finish(process)
    check(result["processed"] == "0", result)

print("PASS UDP replay equivalence, validation, duplicate/gap policy, publisher isolation, shutdown")

"""Run on physical AMIX; exercises CPython core and its GNU Pth backend."""
import sys
import os
import json
import queue
import threading
import time

assert sys.version_info[:3] == (3, 11, 18), sys.version
assert 2 ** 100 == 1267650600228229401496703205376
assert round(2.5) == 2 and round(3.5) == 4 and round(-2.5) == -2
text = "Amiga UNIX: \u00e4\u00f6\u00fc \u03bb \U0001f680"
assert text.encode("utf-8").decode("utf-8") == text
payload = {"text": text, "values": list(range(20))}
assert json.loads(json.dumps(payload)) == payload
name = "python3-smoke-utf8.txt"
with open(name, "w", encoding="utf-8") as output:
    output.write(text)
with open(name, encoding="utf-8") as source:
    assert source.read() == text
os.unlink(name)
print("PASS: integers, float rounding, Unicode, JSON and UTF-8 file I/O", flush=True)

clock = time.get_clock_info("monotonic")
assert clock.monotonic and not clock.adjustable
before = time.monotonic()
time.sleep(0.05)
assert time.monotonic() > before
assert time.gmtime(0)[:6] == (1970, 1, 1, 0, 0, 0)
print("PASS: monotonic clock and time conversion", clock, flush=True)

work = queue.Queue(maxsize=8)
local = threading.local()
failures = []
identities = []

def produce(number):
    try:
        local.number = number
        identities.append(threading.get_ident())
        for index in range(40):
            work.put(number * 40 + index, timeout=10)
            time.sleep(0.001)
            assert local.number == number
    except BaseException as error:
        failures.append(repr(error))

threads = [threading.Thread(target=produce, args=(n,)) for n in range(4)]
for thread in threads:
    thread.start()
received = [work.get(timeout=10) for _ in range(160)]
for thread in threads:
    thread.join(timeout=10)
    assert not thread.is_alive()
assert not failures, failures
assert len(set(identities)) == 4, identities
assert sorted(received) == list(range(160))
event = threading.Event()
assert not event.wait(timeout=0.05)
with threading.RLock():
    pass
print("PASS: four threads, bounded queue, thread-local data, joins and timed wait", flush=True)
print("PYTHON3_AMIX_SMOKE_PASS", sys.version, flush=True)

"""Generated board test: pass --download only on a matching test board."""
import argparse
from pathlib import Path
import time

C = {'schema': 1, 'bit': 'stream_add.bit', 'dma': 'axi_dma_0', 'ip': 'axi_gpio_0', 'read_only_offset': 0, 'dtype': 'uint32', 'samples': 16, 'reference': 'add-one'}


def lookup(ol, path):
    for part in path.split("/"):
        ol = getattr(ol, part)
    return ol


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("overlay_dir", type=Path)
    p.add_argument("--download", action="store_true")
    a = p.parse_args()

    import numpy as np
    from pynq import Overlay, allocate

    ol = Overlay(str((a.overlay_dir / C["bit"]).resolve()), download=False)
    print("IP:", sorted(ol.ip_dict))
    for key in ("dma", "ip"):
        if C[key] not in ol.ip_dict:
            raise SystemExit(f"Missing {C[key]}")
    if not a.download:
        return
    ol.download()
    ip, dma = lookup(ol, C["ip"]), lookup(ol, C["dma"])
    print("RO register:", hex(ip.read(C["read_only_offset"])))
    if hasattr(ip, "register_map"):
        print(ip.register_map)
    tx = allocate(shape=(C["samples"],), dtype=np.uint32)
    rx = allocate(shape=(C["samples"],), dtype=np.uint32)
    tx[:] = np.arange(C["samples"], dtype=np.uint32)
    tx[:4] = [0, 1, 0x7fffffff, 0xffffffff]
    rx[:] = 0xdeadbeef
    expected = np.array(tx)
    if C["reference"] == "add-one":
        expected += np.uint32(1)
    tx.sync_to_device()
    rx.sync_to_device()
    started = False
    done = False
    try:
        t0 = time.monotonic()
        started = True
        dma.recvchannel.transfer(rx)
        dma.sendchannel.transfer(tx)
        while not (dma.sendchannel.idle and dma.recvchannel.idle):
            if getattr(dma.sendchannel, "error", False) or getattr(dma.recvchannel, "error", False):
                raise RuntimeError("DMA reported an error")
            if time.monotonic() - t0 > 2:
                raise TimeoutError("DMA timeout; check TLAST, backpressure and status")
            time.sleep(0.001)
        dma.sendchannel.wait()
        dma.recvchannel.wait()
        done = True
        rx.sync_from_device()
        if dma.recvchannel.transferred != rx.nbytes:
            raise ValueError(f"received {dma.recvchannel.transferred} bytes; expected {rx.nbytes}")
        mismatches = np.flatnonzero(rx != expected)
        if len(mismatches):
            i = int(mismatches[0])
            raise ValueError(f"{len(mismatches)} mismatches; index {i}: {rx[i]} != {expected[i]}")
        print(f"PASS {len(expected)} words; host smoke-test latency {time.monotonic()-t0:.6f}s")
    finally:
        if done or not started:
            rx.freebuffer()
            tx.freebuffer()
        else:
            print("DMA state uncertain. Keep this process and buffers alive until the board is reset.", flush=True)
            while True:
                try:
                    time.sleep(1)
                except KeyboardInterrupt:
                    print("Reset the board before terminating the process.", flush=True)


if __name__ == "__main__":
    main()


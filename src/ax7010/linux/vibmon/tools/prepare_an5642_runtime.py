#!/usr/bin/env python3
"""Load the team's AN5642 camera/HDMI PL into the ALINX Linux image.

Run after stopping the desktop and vibmond, before starting an5642_board.json.
No bootloader or persistent PL flash is written. image.ub must already exclude
the top 32 MiB (prepare_an5642_image.py). Requires root and a PCAP .bit.bin
(tools/bit_to_bin.py). FCLK1 is set to 125 MHz (video) and FCLK2 to 40 MHz
(stereo core, its DMA and S_AXI_HP1 of pl/bd/team_bm_to_stereo.tcl), which
the ALINX FSBL does not.
The kernel display stays blanked: its device tree describes the ALINX PL, whose
VTC and dynclk addresses are swapped in the camera design, so vibmond drives
the video path itself (ui.display = pl_vdma).
"""
import argparse
import ctypes
import fcntl
import mmap
import os
import re
import time


def reg(addr, value=None):
    fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=addr & ~4095)
    r = ctypes.c_uint32.from_buffer(m, addr & 4095)
    if value is not None:
        r.value = value
    result = r.value
    del r
    m.close()
    os.close(fd)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bit_bin')
    args = ap.parse_args()
    region = '/sys/firmware/devicetree/base/reserved-memory/camera@1e000000/reg'
    if not os.path.exists(region) or open(region, 'rb').read() != b'\x1e\0\0\0\x02\0\0\0':
        raise RuntimeError('camera DDR is not reserved; prepare image.ub and reboot')
    bit = open(args.bit_bin, 'rb').read()
    if b'\x66\x55\x99\xaa' not in bit[:1024]:
        raise RuntimeError('expected word-swapped PCAP binary, not a .bit header')
    fb = open('/dev/fb0', 'r+b', buffering=0)
    fcntl.ioctl(fb, 0x4611, 4)  # FBIOBLANK powerdown: stop the kernel display DMA
    # The kernel gates the FCLKs its device tree does not use (FCLK2 here) at
    # boot; a PS access to logic on a stopped clock never completes and hangs
    # the system. Enable FCLK1/FCLK2 through devcfg so the clock framework keeps
    # them on, then set the rates.
    devcfg = '/sys/class/xdevcfg/xdevcfg/device'
    for n in (1, 2):
        d = '%s/fclk/fclk%d' % (devcfg, n)
        if not os.path.exists(d) and os.path.exists(devcfg + '/fclk_export'):
            with open(devcfg + '/fclk_export', 'w') as f:
                f.write('fclk%d' % n)
        if os.path.exists(d + '/enable'):
            with open(d + '/enable', 'w') as f:
                f.write('1')
    reg(0xF8000008, 0xDF0D)
    reg(0xF8000180, 0x00200400)  # FCLK1: IO PLL / 4 / 2 = 125 MHz
    reg(0xF8000190, 0x00500500)  # FCLK2: IO PLL / 5 / 5 = 40 MHz
    for gate in (0xF8000188, 0xF8000198):  # FPGAn_CLK_CTRL + 8, bit 0 = clock stopped
        if reg(gate) & 1:
            reg(gate, reg(gate) & ~1)
    reg(0xF8000004, 0x767B)
    if reg(0xF8000188) & 1 or reg(0xF8000198) & 1:
        raise RuntimeError('FCLK1/FCLK2 still gated: not loading the PL')
    with open('/dev/xdevcfg', 'wb', buffering=0) as dev:
        n = dev.write(bit)
        if n != len(bit):
            raise RuntimeError('short PCAP write')
    if open('/sys/class/xdevcfg/xdevcfg/device/prog_done').read().strip() != '1':
        raise RuntimeError('PL configuration did not complete')
    for bus in ['e0004000.i2c', 'e0005000.i2c']:
        with open('/sys/bus/platform/devices/' + bus + '/power/control', 'w') as f:
            f.write('on')
    reg(0xF8000008, 0xDF0D)
    reg(0xF800012C, reg(0xF800012C) | (1 << 18) | (1 << 19))
    reg(0xF8000004, 0x767B)
    # Let the HDMI connector settle as disconnected (its EDID bus is now camera
    # I2C) so no later hotplug event makes the kernel reprogram the video path.
    for status in ['/sys/class/drm/card0-HDMI-A-1/status']:
        if os.path.exists(status):
            open(status).read()
    fb.close()
    print('AN5642 PL loaded; 125 MHz video clock, 40 MHz stereo clock; both PS I2C clocks enabled')


if __name__ == '__main__':
    main()

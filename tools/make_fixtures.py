#!/usr/bin/env python3
"""Writes synthetic .h30 machine states (v4) covering all engine features.

These are generated test data (not d-lusion content), so they can live in the
repository; their reference hashes come from the original executable via
tools/oracle (see tools/oracle/README.md).
"""
import random
import struct
import sys

SIGNATURE = b"Rubber Duck H3O+ * D-LUSiON"


def make(seed):
    r = random.Random(seed)
    out = bytearray()
    out += bytes([len(SIGNATURE)]) + SIGNATURE
    out += struct.pack('<i', 4)
    song_len = r.randint(1, 8)
    out += struct.pack('<i', song_len)
    song = [r.randrange(16) for _ in range(99)]
    out += bytes(song)
    for _ in range(16):
        for _ in range(16):
            roll = r.random()
            note = 0xFE if roll < 0.15 else 0xFF if roll < 0.2 else r.randint(24, 96)
            out += bytes([note])
            out += struct.pack('<i', 1 if r.random() < 0.3 else 0)
            out += struct.pack('<i', 1 if r.random() < 0.25 else 0)
    knob = lambda: r.randrange(256)
    fields = [
        knob(),                      # tempo knob
        knob(),                      # volume (ignored on load)
        r.randint(-128, 127),        # tune
        knob(),                      # accent
    ]
    out += struct.pack('<4i', *fields)
    out += bytes([r.randrange(4), r.randrange(3)])  # osc1, osc2
    out += struct.pack('<i', r.randrange(2))       # osc2 x2
    out += struct.pack('<i', knob())               # mix
    out += struct.pack('<5i', knob(), knob(), knob(), knob(), knob())  # cutoff attack decay mod reso
    out += struct.pack('<i', 0 if seed % 4 == 0 else 1)  # modification
    out += struct.pack('<2i', knob(), knob())      # squelch drive
    out += struct.pack('<2i', r.randrange(2), r.randrange(2))  # tweak tweet
    out += struct.pack('<i', 0)                    # unknown field
    out += struct.pack('<5i', knob(), knob(), knob(), knob(), knob())  # dist A/B, delay time/gain/fb
    return bytes(out)


if __name__ == '__main__':
    folder = sys.argv[1]
    for seed in range(1, 25):
        with open('%s/synthetic_%02d.h30' % (folder, seed), 'wb') as f:
            f.write(make(seed))

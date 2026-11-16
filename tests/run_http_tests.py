#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright (c) 2026 DSHA contributors
#
# Starts tests/fake_ha.py in every response framing mode and runs the network
# test program against it. With --screenshots it runs the screenshot renderer
# instead and converts its PPM output to PNG.

import os
import subprocess
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fake_ha  # noqa: E402


def start(mode):
    httpd = fake_ha.make_server("127.0.0.1", 0, mode)
    t = threading.Thread(target=httpd.serve_forever, daemon=True)
    t.start()
    return httpd, httpd.server_address[1]


def ppm_to_png(src, dst):
    from PIL import Image
    img = Image.open(src)
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(dst)


def screenshots(binary, outdir):
    os.makedirs(outdir, exist_ok=True)
    httpd, port = start("length")
    tmp = os.path.join(os.path.dirname(binary), "shots")
    os.makedirs(tmp, exist_ok=True)
    rc = subprocess.call([binary, "127.0.0.1", str(port), tmp])
    httpd.shutdown()
    if rc != 0:
        return rc
    for name in sorted(os.listdir(tmp)):
        if name.endswith(".ppm"):
            ppm_to_png(os.path.join(tmp, name),
                       os.path.join(outdir, name[:-4] + ".png"))
            print("wrote", os.path.join(outdir, name[:-4] + ".png"))
    return 0


def main():
    if sys.argv[1] == "--screenshots":
        return screenshots(sys.argv[2], sys.argv[3])

    binary = sys.argv[1]
    failed = 0
    for mode in ("length", "chunked", "close"):
        print(f"== fake_ha mode: {mode}")
        httpd, port = start(mode)
        rc = subprocess.call([binary, "127.0.0.1", str(port)])
        httpd.shutdown()
        if rc != 0:
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

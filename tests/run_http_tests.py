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


def start(mode, bind="127.0.0.1", port=0):
    httpd = fake_ha.make_server(bind, port, mode)
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


def phone(port, path, form=None):
    """Acts like the browser of a phone: returns the final page text."""
    import urllib.parse
    import urllib.request
    data = urllib.parse.urlencode(form).encode() if form is not None else None
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", data,
                                timeout=10) as r:
        return r.read().decode()


def expect(page, text):
    if text not in page:
        print(f"  FAIL: '{text}' not in page:\n{page[-600:]}")
        return 1
    print(f"  ok: {text}")
    return 0


def setup_test(binary):
    import socket
    import time
    httpd, ha_port = start("length")
    failed = 0
    for variant in ("password", "token"):
        print(f"== phone setup with {variant}")
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        proc = subprocess.Popen([binary, f"127.0.0.1:{ha_port}", str(port)],
                                stdout=subprocess.PIPE, text=True)
        proc.stdout.readline()  # "listening"
        server = f"127.0.0.1:{ha_port}"
        page = phone(port, "/")
        failed += expect(page, "DSHA setup")
        failed += expect(page, f'value="{server}"')
        if variant == "password":
            page = phone(port, "/", {"server": "127.0.0.1:1", "username": "sam",
                                     "password": "secret", "token": ""})
            failed += expect(page, "find Home Assistant")
            page = phone(port, "/", {"server": server, "username": "sam",
                                     "password": "nope", "token": ""})
            failed += expect(page, "Wrong user name or password")
            page = phone(port, "/", {"server": server, "username": "mfa",
                                     "password": "secret", "token": ""})
            failed += expect(page, "Two-factor code")
            page = phone(port, "/mfa", {"code": "000000"})
            failed += expect(page, "Wrong code")
            page = phone(port, "/mfa", {"code": " 123456 "})
            failed += expect(page, "All set")
        else:
            page = phone(port, "/", {"server": "http://" + server + "/",
                                     "username": "", "password": "",
                                     "token": "wrong"})
            failed += expect(page, "token doesn")
            page = phone(port, "/", {"server": server, "username": "",
                                     "password": "", "token": "testtoken\n"})
            failed += expect(page, "All set")
        out, _ = proc.communicate(timeout=20)
        print("  " + out.strip().splitlines()[-1])
        if proc.returncode != 0:
            failed += 1
            print("  FAIL: exit code", proc.returncode)
        if variant == "password" and "refresh=ref-" not in out:
            failed += 1
            print("  FAIL: no refresh token")
        if variant == "token" and "token=testtoken " not in out:
            failed += 1
            print("  FAIL: no token")
    httpd.shutdown()
    print(f"test_setup: {failed} failed")
    return 1 if failed else 0


def main():
    if sys.argv[1] == "--setup":
        return setup_test(sys.argv[2])

    if sys.argv[1] == "--discover":
        # The fake server listens on 127.0.0.7 and on localhost (127.0.0.1)
        # with the same port. The scan must find it by IP and by name.
        httpd, port = start("length", "127.0.0.7")
        httpd2, _ = start("length", "127.0.0.1", port)
        rc = subprocess.call([sys.argv[2], "127.0.0.7", str(port)])
        httpd.shutdown()
        httpd2.shutdown()
        return rc

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

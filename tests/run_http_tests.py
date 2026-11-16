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


def phone(port, path, form=None, headers=False):
    """Acts like the browser of a phone: returns the final page text."""
    import urllib.parse
    import urllib.request
    data = urllib.parse.urlencode(form).encode() if form is not None else None
    with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", data,
                                timeout=10) as r:
        body = r.read().decode()
        return (body, r.headers) if headers else body


def expect(page, text, present=True):
    if (text in page) != present:
        print(f"  FAIL: '{text}' {'not ' if present else ''}in page:\n{page[-600:]}")
        return 1
    print(f"  ok: {'' if present else 'not '}{text}")
    return 0


def setup_test(binary):
    import queue
    import socket
    import threading as th
    httpd, ha_port = start("length")
    failed = 0
    for variant in ("password", "token", "pin", "garbage"):
        print(f"== phone setup: {variant}")
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()
        proc = subprocess.Popen([binary, f"127.0.0.1:{ha_port}", str(port)],
                                stdout=subprocess.PIPE, text=True)
        lines = queue.Queue()
        th.Thread(target=lambda: [lines.put(l) for l in proc.stdout],
                  daemon=True).start()
        pin = lines.get(timeout=10).split()[1]   # "listening <pin>"
        log = []

        def current_pin():
            nonlocal pin
            while not lines.empty():
                line = lines.get()
                log.append(line)
                if line.startswith("pin "):
                    pin = line.split()[1]
            return pin

        server = f"127.0.0.1:{ha_port}"
        page, hdrs = phone(port, "/", headers=True)
        failed += expect(page, "DSHA setup")
        failed += expect(page, f'value="{server}"')
        for h in ("X-Frame-Options", "Content-Security-Policy",
                  "X-Content-Type-Options", "Referrer-Policy"):
            if h not in hdrs:
                failed += 1
                print(f"  FAIL: missing header {h}")
        print("  ok: security headers")

        if variant == "garbage":
            # Hostile clients: random bytes, huge headers and lengths,
            # truncated requests, slow clients. The server must survive
            # (it runs with AddressSanitizer) and keep working.
            import random
            rnd = random.Random(1234)
            samples = [
                b"POST / HTTP/1.1\r\nContent-Length: 999999999\r\n\r\npin=1",
                b"POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n",
                b"POST / HTTP/1.1\r\nContent-Length: 18446744073709551615\r\n\r\n",
                b"GET /" + b"A" * 9000 + b" HTTP/1.1\r\n\r\n",
                b"POST / HTTP/1.1\r\n\r\npin=" + b"%" * 3000,
                b"POST / HTTP/1.1\r\n\r\n" + b"&" * 4000 + b"pin=%zz%0",
                b"GET /status HTTP/1.1\r\n" + b"X: y\r\n" * 800 + b"\r\n",
                b"\x00\xff\r\n\r\n",
                b"POST /mfa HTTP/1.1\r\n\r\ncode=1",
                b"GET",
                b"",
            ]
            for i in range(1500):
                if i < len(samples):
                    data = samples[i]
                else:
                    data = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(0, 600)))
                    if rnd.random() < 0.5:
                        data = b"POST / HTTP/1.1\r\nContent-Length: %d\r\n\r\n" % rnd.randint(0, 5000) + data
                try:
                    c = socket.create_connection(("127.0.0.1", port), timeout=3)
                    c.sendall(data)
                    # A few clients never finish their request (they cost
                        # the server its 1.5 s timeout each)
                    if rnd.random() < 0.98:
                        c.shutdown(socket.SHUT_WR)
                    try:
                        c.recv(65536)
                    except OSError:
                        pass
                    c.close()
                except OSError:
                    pass
            print("  ok: survived 1500 hostile requests")
            page = phone(port, "/", {"pin": current_pin(), "server": server,
                                     "username": "sam", "password": "secret"})
            failed += expect(page, "All set")
        elif variant == "password":
            page = phone(port, "/", {"pin": current_pin(), "server": "127.0.0.1:1",
                                     "username": "sam", "password": "secret", "token": ""})
            failed += expect(page, "find Home Assistant")
            page = phone(port, "/", {"pin": current_pin(), "server": server,
                                     "username": "sam", "password": "nope", "token": ""})
            failed += expect(page, "Wrong user name or password")
            page = phone(port, "/", {"pin": current_pin(), "server": server,
                                     "username": "mfa", "password": "secret", "token": ""})
            failed += expect(page, "Two-factor code")
            page = phone(port, "/mfa", {"code": "000000"})
            failed += expect(page, "Wrong code")
            page = phone(port, "/mfa", {"code": " 123456 "})
            failed += expect(page, "All set")
        elif variant == "token":
            page = phone(port, "/", {"pin": current_pin(), "server": "http://" + server + "/",
                                     "username": "", "password": "", "token": "wrong"})
            failed += expect(page, "token doesn")
            page = phone(port, "/", {"pin": current_pin(), "server": server, "username": "",
                                     "password": "", "token": "bad\r\nX-Evil: 1"})
            failed += expect(page, "token isn")
            page = phone(port, "/", {"pin": current_pin(), "server": server, "username": "",
                                     "password": "", "token": "testtoken\n"})
            failed += expect(page, "All set")
        else:
            # Without the code (another device, or a web page doing CSRF),
            # nothing happens even with right credentials
            page = phone(port, "/", {"server": server, "username": "sam",
                                     "password": "secret", "token": ""})
            failed += expect(page, "Wrong code")
            failed += expect(page, "All set", present=False)
            old = current_pin()
            wrong = "000000" if old != "000000" else "111111"
            for _ in range(4):
                page = phone(port, "/", {"pin": wrong, "server": server,
                                         "username": "sam", "password": "secret"})
            failed += expect(page, "Too many wrong codes")
            import time
            time.sleep(0.2)
            new = current_pin()
            if new == old:
                failed += 1
                print("  FAIL: the code didn't change")
            else:
                print("  ok: new code after 5 failures")
            # The old code doesn't work anymore
            page = phone(port, "/", {"pin": old, "server": server,
                                     "username": "sam", "password": "secret"})
            failed += expect(page, "All set", present=False)
            page = phone(port, "/", {"pin": current_pin(), "server": server,
                                     "username": "sam", "password": "secret"})
            failed += expect(page, "All set")
            # Hostile server address
            # (validated before use; here the setup is already done)

        proc.wait(timeout=20)
        current_pin()
        result = [l for l in log if l.startswith("result:")]
        print("  " + (result[-1].strip() if result else "no result"))
        if proc.returncode != 0:
            failed += 1
            print("  FAIL: exit code", proc.returncode)
        if variant in ("password", "pin", "garbage") and not any("refresh=ref-" in l for l in result):
            failed += 1
            print("  FAIL: no refresh token")
        if variant == "token" and not any("token=testtoken " in l for l in result):
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

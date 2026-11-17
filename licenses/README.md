# Third party licenses

`dsha.nds` contains code from these projects. Their licenses are included in
this folder and must be distributed together with the ROM.

| Project | Used for | License | File |
|---|---|---|---|
| [libnds (BlocksDS)](https://github.com/blocksds/libnds) | DS hardware access, FAT/SD card, NitroFS | Zlib (FatFs: BSD-1-Clause) | [libnds.txt](libnds.txt) |
| [DSWifi (BlocksDS)](https://github.com/blocksds/dswifi) | WiFi driver | MIT | [dswifi.txt](dswifi.txt) |
| [lwIP](https://savannah.nongnu.org/projects/lwip/) (inside DSWifi) | TCP/IP stack | BSD-3-Clause | [lwip.txt](lwip.txt) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (BlocksDS package, and inside DSWifi) | HTTPS (TLS), WPA2 cryptography | Apache-2.0 OR GPL-2.0-or-later | [mbedtls.txt](mbedtls.txt) |
| Root certificates from [Mozilla's CA store](https://wiki.mozilla.org/CA) (`source/core/ca_roots.c`) | Verifying HTTPS servers | MPL-2.0 | [ca-certificates.txt](ca-certificates.txt) |
| [font8x8](https://github.com/dhepper/font8x8) | 8x8 bitmap font | Public domain | - |

The DSHA code itself is MIT licensed, see [../LICENSE](../LICENSE).

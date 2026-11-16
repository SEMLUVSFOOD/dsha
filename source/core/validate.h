// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Validation of values that end up in HTTP requests or in config.ini. Anything
// that comes from the network (WiFi names, server responses, the setup page)
// is checked with these before use, so it can't inject headers, paths or
// config lines.

#ifndef DSHA_VALIDATE_H
#define DSHA_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

// "light.living_room": lowercase letters, digits and '_', one dot
bool valid_entity_id(const char *s);

// Host name or IP address: letters, digits, '.', '-', '_' and ':' (IPv6)
bool valid_host(const char *s);

// Access/refresh token: printable ASCII without spaces (no CR/LF)
bool valid_token(const char *s);

// No control characters (newlines, NUL, DEL...). Used for values written to
// config.ini, like WiFi names.
bool valid_line(const char *s);

// Replaces control characters with spaces
void sanitize_line(char *s);

// Is this IPv4 address (host byte order) in a private/local range?
// 10/8, 172.16/12, 192.168/16, 127/8, 169.254/16 and 100.64/10 (CGNAT, used
// by VPNs like Tailscale).
bool is_private_ipv4(uint32_t ip);

#endif // DSHA_VALIDATE_H

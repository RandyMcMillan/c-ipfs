#!/usr/bin/env python3
"""Publish a signed Nostr event to one or more relays using only the stdlib."""

import argparse
import base64
import hashlib
import json
import os
import random
import socket
import ssl
import struct
import sys
import time


def parse_url(url):
    if url.startswith("wss://"):
        scheme = "wss"
        rest = url[len("wss://"):]
        port = 443
    elif url.startswith("ws://"):
        scheme = "ws"
        rest = url[len("ws://"):]
        port = 80
    else:
        raise ValueError("relay URL must start with ws:// or wss://")

    host = rest.split("/", 1)[0]
    if ":" in host:
        host, port = host.split(":", 1)
        port = int(port)
    return scheme, host, port


def read_exact(sock, size):
    chunks = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise EOFError("socket closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def recv_message(sock):
    hdr = read_exact(sock, 2)
    b1, b2 = hdr[0], hdr[1]
    fin = bool(b1 & 0x80)
    opcode = b1 & 0x0f
    masked = bool(b2 & 0x80)
    length = b2 & 0x7f
    if length == 126:
        length = struct.unpack(">H", read_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", read_exact(sock, 8))[0]

    mask = read_exact(sock, 4) if masked else None
    payload = read_exact(sock, length)
    if mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

    if opcode == 0x08:
        return ("close", payload, fin)
    if opcode == 0x09:
        # Reply to a ping with a pong.
        send_frame(sock, payload, 0x0a)
        return recv_message(sock)
    if opcode == 0x0a:
        return recv_message(sock)
    if opcode == 0x01:
        return ("text", payload, fin)
    if opcode == 0x02:
        return ("binary", payload, fin)
    return ("unknown", payload, fin)


def send_frame(sock, payload):
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    payload = bytes(payload)

    header = bytes((0x81,))  # FIN + text frame
    if len(payload) < 126:
        header += bytes((0x80 | len(payload),))
    elif len(payload) < 65536:
        header += bytes((0xfe,)) + struct.pack(">H", len(payload))
    else:
        header += bytes((0xff,)) + struct.pack(">Q", len(payload))

    mask = os.urandom(4)
    masked_payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    header += mask
    sock.sendall(header + masked_payload)


def connect_relay(url):
    scheme, host, port = parse_url(url)
    sock = socket.create_connection((host, port), timeout=20)
    if scheme == "wss":
        context = ssl.create_default_context()
        sock = context.wrap_socket(sock, server_hostname=host)

    key = base64.b64encode(os.urandom(16)).decode("ascii")
    accept_value = base64.b64encode(
        hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii"))
        .digest()
    ).decode("ascii")

    request = (
        "GET / HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: nostr\r\n"
        "\r\n"
    )
    sock.sendall(request.encode("ascii"))
    response = b""
    deadline = time.time() + 20
    while b"\r\n\r\n" not in response and time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        response += chunk
        if len(response) > 65536:
            break
    if b"\r\n\r\n" not in response:
        raise RuntimeError(f"{url}: incomplete WebSocket handshake")
    headers, _ = response.split(b"\r\n\r\n", 1)
    if b" 101 " not in headers:
        raise RuntimeError(f"{url}: handshake failed: {headers.decode('utf-8', 'replace')}")
    header_map = {}
    for line in headers.decode("utf-8", "replace").split("\r\n")[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            header_map[name.strip().lower()] = value.strip()
    if header_map.get("sec-websocket-accept") != accept_value:
        raise RuntimeError(f"{url}: invalid Sec-WebSocket-Accept")
    return sock


def publish(relay, event, timeout):
    sock = connect_relay(relay)
    sock.settimeout(timeout)
    try:
        message = []
        event_id = None
        if isinstance(event, dict):
            event_id = event.get("id")
            message = ["EVENT", event]
        else:
            event = json.loads(event)
            event_id = event.get("id")
            message = ["EVENT", event]
        send_frame(sock, json.dumps(message, separators=(",", ":")))

        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                kind, payload, fin = recv_message(sock)
            except socket.timeout:
                break
            if kind == "close":
                break
            if kind == "text":
                try:
                    parsed = json.loads(payload.decode("utf-8"))
                except json.JSONDecodeError:
                    continue
                if isinstance(parsed, list) and parsed and parsed[0] == "OK":
                    ok = bool(parsed[2]) if len(parsed) > 2 else True
                    return (ok, json.dumps(parsed))
            if not fin:
                pass
        if event_id:
            # Some relays accept silently.
            return (True, "published (no explicit OK)")
        raise RuntimeError("no OK response from relay")
    finally:
        try:
            sock.sendall(b"\x88\x80" + os.urandom(4))
        except Exception:
            pass
        try:
            sock.close()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--relay", action="append", default=[])
    parser.add_argument("--event", help="Path to the event JSON file")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--timeout", type=int, default=20)
    args = parser.parse_args()

    if args.dry_run:
        print("dry-run mode: dry")
        return 0

    if not args.event:
        event_text = sys.stdin.read()
    else:
        with open(args.event, "r", encoding="utf-8") as handle:
            event_text = handle.read()

    event = json.loads(event_text)

    relays = args.relay or os.environ.get("NOSTR_RELAYS", "wss://relay.damus.io").split()
    results = []
    failures = 0
    for relay in relays:
        try:
            ok, reply = publish(relay, event, args.timeout)
            print(f"{relay}: {'OK' if ok else 'rejected'} {reply}")
            results.append((relay, ok, reply))
            if not ok:
                failures += 1
        except Exception as exc:
            print(f"{relay}: ERROR {exc}", file=sys.stderr)
            results.append((relay, False, str(exc)))
            failures += 1

    successful = len(results) - failures
    print(json.dumps({"published_to": successful, "total": len(results)}, indent=2))
    if successful == 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

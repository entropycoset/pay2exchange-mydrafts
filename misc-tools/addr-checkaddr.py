#!/usr/bin/env python3
#vibecoded
import sys
import base58
import hashlib

USAGE = f"Usage: {sys.argv[0]} <BTS_key_or_address>"

# Try to get ripemd160 from hashlib; fall back to PyCryptodome
try:
    def ripemd160(x):
        return hashlib.new('ripemd160', x).digest()
except ValueError:
    from Crypto.Hash import RIPEMD160
    def ripemd160(x):
        return RIPEMD160.new(x).digest()

def decode_graphene(s, prefix="BTS"):
    if not s.startswith(prefix):
        print(f"Error: missing prefix '{prefix}'")
        sys.exit(1)
    enc = s[len(prefix):]
    try:
        full = base58.b58decode(enc)
    except Exception as e:
        print("Error: Base58 decode failed:", e)
        sys.exit(1)
       
    print("String " + s + " base58 decodes into: " + full.hex())
    if len(full) < 5:
        print("Error: decoded data too short")
        sys.exit(1)

    payload, chk = full[:-4], full[-4:]
    expected_chk = ripemd160(payload)[:4]
    if chk != expected_chk:
        print("Error: invalid checksum")
        sys.exit(1)
    print("Checksum is correct: given chk=" + chk.hex() + ", expected: ripemd160(payload)=" + expected_chk.hex())

    return payload

def main():
    if len(sys.argv) != 2:
        print(USAGE); sys.exit(1)

    s = sys.argv[1].strip()
    payload = decode_graphene(s)

    L = len(payload)
    if L == 33:
        print("Type: Public Key")
        print("Raw  : 0x" + payload.hex())
    elif L == 20:
        print("Type: Account Address (RIPEMD160(pubkey))")
        print("Raw  : 0x" + payload.hex())
    else:
        print(f"Warning: payload is {L} bytes (unexpected).")
        print("Raw  : 0x" + payload.hex())

if __name__ == "__main__":
    main()


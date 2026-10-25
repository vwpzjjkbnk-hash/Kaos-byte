"""Devkit ("debug") RSA signature for XEX2 files.

A XEX whose signature is all zeros is classified as a *retail* image, and a retail image is
decrypted with the retail XEX key. Every devkit-encrypted module that loads on the user's Peer
Pressure console (AuroraFixer.xex, Aurora's FtpDll.xex, SR3.xex) carries a devkit signature; the
only zero-signature module there (Peer Pressure's BootXell.xex) is retail-encrypted. A
devkit-encrypted image with a zero signature is decrypted with the wrong key and silently refused,
so the plugin is signed the way XDK's imagexex / XexTool "-m d" sign.

Signature, over the security info from image_info.info_size (security+0x108) up to
allowed_media_types (security+0x17C):
  digest = XeCryptRotSumSha(region)
  block  = XeCryptBnQwBeSigFormat(digest, "XBOX360XEX")  (PKCS-like pad, RC4-masked, 0xBC end)
  sig    = (R^2 * block) ^ D mod N    (R^2 = 2^((e-1)<<11) mod N, the Montgomery factor the
                                       console's verify divides back out; e = 3)
stored as 32 qwords, least significant first, each qword big-endian.

The key pair is the long-public XDK devkit key that every XEX tool ships. `verify()` checks a
signature with only the public half and is tested against the console's own signed modules.
"""
import hashlib

N = int(
    "D7492F13B1C17FBF2C5B33E51CFA0002BB9D881671648107A6E9A7C278F5C0B8D4B7419B0A9960027F95271ACC7BAC29"
    "16601A8AC74B6893ED8C08ED2513CCBB9EC2A4CFB3ECC8059F18D04DADA56EFCB0484CF99E6FA4764054226544102AD2"
    "D587667BF2289CB37207DA63AAF33EED1EBCD83D91A7897C664E8BD022ABDB90EFA039EA1CF948667DBC4559D0E3F297"
    "44F48DC58451C81B4E3A8142C56EB949B0E0F51A8E8039C2B8D9A3F13B6E71DB56910FF8E8BD904DCCBED0C5A743AA6C"
    "4C5D8565988C4C6BB8705CC0BE8F1B3D6D5D97FD7330763AE1767061E6BE93273E0670624AA2AD9984E7AB2EE4DBAE1E"
    "642F4E6C7399ACE5C91C3577C8BFA06B", 16)
D = int(
    "8F861F627680FFD4C83CCD4368A6AAAC7D13B00EF64300AFC49BC52C50A3D5D08DCF81120710EAAC550E1A11DDA7C81B"
    "644011B1DA3245B7F3B2B09E18B7DDD269D7188A77F33003BF65E033C918F4A8758588A6699FC2F9803816EE2D601C8C"
    "8E5A4452A1706877A15A9197C74CD49E147DE57E611A5BA844345D356C72925F664E995622C73C3D9B7038113E984744"
    "9E2F660CCDC58A26974F6B9C4222E98E1A553EEB4416C0445DA709AC06807D9E91293542C4CA53310D5E26331F791FE6"
    "EBB0D0324922713486EBD1220C81729C6B908BE936A44D55169268EBB10F12DE4FAE198393352901BC167F442D33563A"
    "3C9EEF40E6DDAD8D0862F50F4D47CC5B", 16)
E = 3
SALT = b"XBOX360XEX"
M64 = (1 << 64) - 1

SIGNATURE_OFFSET = 0x008  # relative to the security info
REGION_START = 0x108      # image_info.info_size
REGION_END = 0x17C        # allowed_media_types (not signed; covered by the header digest)


def rot_sum(data):
    """XeCryptRotSum over big-endian qwords: four 64-bit lanes with carries and rotates."""
    a = b = c = d = 0
    for i in range(0, len(data) - len(data) % 8, 8):
        q = int.from_bytes(data[i:i + 8], "big")
        s = (q + b) & M64
        b = 1 if s < q else 0
        d = (d + (~q & M64) + 1) & M64
        a = (a + b) & M64
        b = ((s << 29) | (s >> 35)) & M64
        borrow = 1 if d > q else 0
        c = (c + (~borrow & M64) + 1) & M64
        d = ((d << 31) | (d >> 33)) & M64
    return b"".join(x.to_bytes(8, "big") for x in (a, b, c, d))


def rot_sum_sha(data):
    rs = rot_sum(data)
    inv = bytes(x ^ 0xFF for x in rs)
    return hashlib.sha1(rs + rs + data + inv + inv).digest()


def rc4(key, data):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    out = bytearray()
    i = j = 0
    for byte in data:
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out.append(byte ^ s[(s[i] + s[j]) & 0xFF])
    return bytes(out)


def sig_block(digest):
    """XeCryptBnQwBeSigFormat as a big-endian integer (before the qword swap)."""
    blk = bytearray(0x100)
    blk[0xE0] = 0x01
    blk[0xE1:0xEB] = SALT
    blk[0xFF] = 0xBC
    mask_key = hashlib.sha1(bytes(blk[:8]) + digest + SALT).digest()
    blk[0xEB:0xFF] = mask_key
    blk[:0xEB] = rc4(mask_key, bytes(blk[:0xEB]))
    blk[0] &= 0x7F
    return int.from_bytes(blk, "big")


def to_qw_ne(value):
    raw = value.to_bytes(0x100, "big")
    return b"".join(raw[i:i + 8] for i in range(0xF8, -8, -8))


def from_qw_ne(raw):
    return int.from_bytes(b"".join(raw[i:i + 8] for i in range(0xF8, -8, -8)), "big")


def _expected(security):
    r2 = pow(2, (E - 1) << 11, N)
    return r2 * sig_block(rot_sum_sha(bytes(security[REGION_START:REGION_END]))) % N


def sign(security):
    """Returns the 0x100-byte signature for a security info block (bytes from its start)."""
    return to_qw_ne(pow(_expected(security), D, N))


def verify(security):
    sig = from_qw_ne(bytes(security[SIGNATURE_OFFSET:SIGNATURE_OFFSET + 0x100]))
    return sig != 0 and pow(sig, E, N) == _expected(security)


def verify_file(data):
    sec = int.from_bytes(data[0x10:0x14], "big")
    return verify(data[sec:sec + 0x180])


if __name__ == "__main__":
    import sys
    bad = 0
    for path in sys.argv[1:]:
        ok = verify_file(open(path, "rb").read())
        bad += not ok
        print(("devkit-signed  " if ok else "NOT signed     ") + path)
    sys.exit(1 if bad else 0)

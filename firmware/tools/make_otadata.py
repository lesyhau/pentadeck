"""Build an otadata partition image (0x2000 bytes at 0xd000) that makes the bootloader start a given slot.

The otadata partition holds two 32-byte records (one per 4 KB sector):
    uint32 ota_seq; uint8 seq_label[20]; uint32 ota_state; uint32 crc   (crc = CRC32 of ota_seq)
The bootloader boots slot (max_valid_seq - 1) % 2. We keep the newer record and replace the older one,
exactly like esp_ota_set_boot_partition() does.

usage:
    python make_otadata.py <full_flash_backup.bin> <slot 0|1> <out.bin>     # build
    python make_otadata.py --check <full_flash_backup.bin>                  # just decode / verify CRCs
"""
import binascii
import struct
import sys

OTADATA_OFF, SECTOR = 0xD000, 0x1000
UNDEFINED = 0xFFFFFFFF


def crc(seq):
    return binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def decode(otadata):
    out = []
    for i in range(2):
        rec = otadata[i * SECTOR:i * SECTOR + 32]
        seq, = struct.unpack_from("<I", rec, 0)
        state, stored = struct.unpack_from("<II", rec, 24)
        valid = seq != UNDEFINED and stored == crc(seq)
        out.append({"sector": i, "seq": seq, "state": state, "crc": stored, "valid": valid})
    return out


def main():
    if sys.argv[1] == "--check":
        flash = open(sys.argv[2], "rb").read()
        recs = decode(flash[OTADATA_OFF:OTADATA_OFF + 2 * SECTOR])
        for r in recs:
            print(f"sector {r['sector']}: seq={r['seq']:#x} state={r['state']:#x} crc={r['crc']:#010x} "
                  f"computed={crc(r['seq']):#010x} valid={r['valid']}")
        best = max((r for r in recs if r["valid"]), key=lambda r: r["seq"], default=None)
        print("boots:", f"ota_{(best['seq'] - 1) % 2}" if best else "ota_0 (no valid record)")
        return

    flash, slot, out = open(sys.argv[1], "rb").read(), int(sys.argv[2]), sys.argv[3]
    otadata = bytearray(flash[OTADATA_OFF:OTADATA_OFF + 2 * SECTOR])
    recs = decode(otadata)
    valid = [r for r in recs if r["valid"]]
    newest = max(valid, key=lambda r: r["seq"]) if valid else None
    seq = newest["seq"] + 1 if newest else 1
    while (seq - 1) % 2 != slot:          # next sequence number that selects the wanted slot
        seq += 1
    target = 1 - newest["sector"] if newest else 0   # overwrite the older record, keep the newer one
    rec = struct.pack("<I", seq) + b"\xff" * 20 + struct.pack("<II", UNDEFINED, crc(seq))
    sector = bytearray(b"\xff" * SECTOR)
    sector[:32] = rec
    otadata[target * SECTOR:(target + 1) * SECTOR] = sector
    open(out, "wb").write(otadata)
    print(f"wrote {out}: sector {target} seq={seq:#x} -> boots ota_{(seq - 1) % 2}")


if __name__ == "__main__":
    main()

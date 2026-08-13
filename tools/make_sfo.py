"""Generate a PS Vita param.sfo (PSF format), compatible with vita-mksfoex output."""
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'vpk' / 'sce_sys' / 'param.sfo'

TITLE = 'Red Ball 4'
STITLE = 'Red Ball 4'
TITLE_ID = 'RDBL40001'

FMT_STR = 0x0204
FMT_INT = 0x0404


def entries():
    return [
        ('APP_VER', '01.00', 8),
        ('ATTRIBUTE', 0x8000, 4),
        ('ATTRIBUTE2', 0, 4),
        ('ATTRIBUTE_MINOR', 0x10, 4),
        ('BOOT_FILE', '', 32),
        ('CATEGORY', 'gd', 4),
        ('CONTENT_ID', '', 48),
        ('EBOOT_APP_MEMSIZE', 0, 4),
        ('EBOOT_ATTRIBUTE', 0, 4),
        ('EBOOT_PHY_MEMSIZE', 0, 4),
        ('LAREA_TYPE', 0, 4),
        ('NP_COMMUNICATION_ID', '', 16),
        ('PARENTAL_LEVEL', 0, 4),
        ('PSP2_DISP_VER', '00.000', 8),
        ('PSP2_SYSTEM_VER', 0, 4),
        ('STITLE', STITLE, 52),
        ('TITLE', TITLE, 0x80),
        ('TITLE_ID', TITLE_ID, 12),
        ('VERSION', '01.00', 8),
    ]


def build():
    ents = sorted(entries(), key=lambda e: e[0])

    key_blob = b''
    data_blob = b''
    index = []
    for key, value, max_len in ents:
        key_off = len(key_blob)
        key_blob += key.encode('ascii') + b'\0'
        data_off = len(data_blob)
        if isinstance(value, int):
            fmt, dlen, dmax = FMT_INT, 4, 4
            data_blob += struct.pack('<i', value)
        else:
            raw = value.encode('utf-8') + b'\0'
            fmt, dlen, dmax = FMT_STR, len(raw), max(max_len, len(raw))
            data_blob += raw + b'\0' * (dmax - len(raw))
        index.append((key_off, fmt, dlen, dmax, data_off))

    header_size = 20 + 16 * len(ents)
    key_table_start = header_size
    pad = (4 - (len(key_blob) % 4)) % 4
    key_blob += b'\0' * pad
    data_table_start = key_table_start + len(key_blob)

    out = struct.pack('<4sIIII', b'\0PSF', 0x0101,
                      key_table_start, data_table_start, len(ents))
    for key_off, fmt, dlen, dmax, data_off in index:
        out += struct.pack('<HHIII', key_off, fmt, dlen, dmax, data_off)
    out += key_blob + data_blob
    return out


def verify(blob):
    magic, version, kts, dts, n = struct.unpack_from('<4sIIII', blob, 0)
    assert magic == b'\0PSF' and version == 0x0101, 'bad header'
    print(f'param.sfo OK: {n} entries, {len(blob)} bytes')
    for i in range(n):
        ko, fmt, dlen, dmax, do = struct.unpack_from('<HHIII', blob, 20 + 16 * i)
        key = blob[kts + ko:blob.index(b'\0', kts + ko)].decode()
        raw = blob[dts + do:dts + do + dlen]
        val = struct.unpack('<i', raw)[0] if fmt == FMT_INT else raw.rstrip(b'\0').decode()
        print(f'  {key:22s} = {val!r}')


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    blob = build()
    OUT.write_bytes(blob)
    verify(blob)
    print(f'-> {OUT}')


if __name__ == '__main__':
    main()

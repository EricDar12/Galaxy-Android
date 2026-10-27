"""Synthetic disc, RARC and BCSV fixtures for the cook-pipeline tests.

They live here rather than inside a test module because the repository caps
every maintained file at 300 lines and the cooking tests need that room.
"""
from pathlib import Path
import struct
import sys

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / 'cook'))

import dol_data  # noqa: E402

STRING_PTR = 6


def _rarc(folder, name, payload):
    """Minimal big-endian RARC holding one folder and one uncompressed file."""
    strings = b'\0' + folder.encode() + b'\0' + name.encode() + b'\0'
    folder_off = 1
    name_off = 1 + len(folder) + 1
    dirs_at, files_at = 0x40, 0x60
    strings_at = files_at + 2 * 0x14
    data_at = (strings_at + len(strings) + 0x1F) & ~0x1F
    total = data_at + len(payload)
    out = bytearray(total)
    out[0:4] = b'RARC'
    struct.pack_into('>IIIIII', out, 4, total, 0x20, data_at - 0x20, len(payload), 0, 0)
    struct.pack_into('>IIIIII', out, 0x20, 2, dirs_at - 0x20, 2, files_at - 0x20,
                     len(strings), strings_at - 0x20)
    struct.pack_into('>IIHHI', out, dirs_at, 0, 0, 0, 1, 0)
    struct.pack_into('>IIHHI', out, dirs_at + 0x10, 1, folder_off, 0, 1, 1)
    struct.pack_into('>HHIIII', out, files_at, 0, 0, (0x02 << 24) | folder_off, 1, 0x10, 0)
    struct.pack_into('>HHIIII', out, files_at + 0x14, 1, 0, name_off, 0, len(payload), 0)
    out[strings_at:strings_at + len(strings)] = strings
    out[data_at:] = payload
    return bytes(out)


def _bcsv(columns):
    """One-row big-endian BCSV; columns are (name, type) at 4-byte slots."""
    fields = len(columns)
    entry = 4 * fields
    data_at = 16 + 12 * fields
    body = bytearray(data_at + entry + 1)
    struct.pack_into('>iiii', body, 0, 1, fields, data_at, entry)
    for i, (name, kind) in enumerate(columns):
        struct.pack_into('>IIHBB', body, 16 + 12 * i, dol_data.jmap_hash(name), 0, 4 * i, 0, kind)
    return bytes(body + b'\0' * (-len(body) % 32))


def _main_dol():
    """The error archive and the two BCSV tables the cooker extracts from it."""
    names = [('name', STRING_PTR), ('progress', STRING_PTR)]
    galaxy = [('name', STRING_PTR), ('MapPaneName', STRING_PTR), ('PowerStarNum', STRING_PTR),
              ('unused0', 0), ('unused1', 0), ('unused2', 0), ('unused3', 0)]
    out = bytearray(32)
    for part in (_rarc('layoutdata', 'errormessagewindow.arc', b'\0' * 16),
                 _bcsv(names), _bcsv(galaxy)):
        out += b'\0' * (-len(out) % 32)
        out += part
    return bytes(out)


def _disc(root):
    data = root / 'DATA'
    (data / 'sys').mkdir(parents=True)
    (data / 'files' / 'StageData').mkdir(parents=True)
    (data / 'files' / 'ObjectData').mkdir()
    (data / 'sys' / 'fst.bin').write_bytes(b'fst-fixture')
    (data / 'sys' / 'main.dol').write_bytes(_main_dol())
    (data / 'files' / 'StageData' / 'Model.arc').write_bytes(
        _rarc('data', 'note.txt', b'hello'))
    (data / 'files' / 'StageData' / 'Table.bcsv').write_bytes(
        _bcsv([('name', STRING_PTR), ('value', 0)]))
    (data / 'files' / 'ObjectData' / 'Readme.txt').write_bytes(b'plain text')
    return data

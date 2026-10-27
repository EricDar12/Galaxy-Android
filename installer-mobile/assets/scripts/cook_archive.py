import os
import collections
import struct
import bcsv
import bmg
import collision
import dol_data
import j3d
import jaudio
import jpa
import kcl
import lyt
import rarc_swap
from rarc import yaz0_decompress
from util import sw32

TEXT_EXT = {'.txt', '.ini', '.csv', '.csv#', '.xnim', '.dtp', '.dck'}
UNUSED_EXT = {'.dvmat', '.dvw', '.prj', '.backup', '.bgrv', '.dat', '.gst', '.bin'}
BCSV_EXT = {'.bcsv', '.pa', '.bcam', '.banmt', '.tbl'}


def cook_blob(path, b, stats):
    """Converts one file's bytes (bytearray) in place; returns it."""
    name = path.rsplit('/', 1)[-1].lower()
    ext = os.path.splitext(name)[1]
    magic = bytes(b[:4])

    if magic == b'J3D2':
        j3d.swap_model(b)
        kind = 'j3d-model'
    elif magic == b'J3D1':
        j3d.swap_anim(b)
        kind = 'j3d-anim'
    elif ext == '.kcl' and kcl.looks_like_kcl(b):
        kcl.swap_kcl(b)
        kind = 'kcl'
    elif ext == '.bas':
        j3d.swap_sound_anim(b, 0)
        kind = 'bas'
    elif magic == b'RLYT':
        lyt.swap_rlyt(b)
        kind = 'rlyt'
    elif magic == b'RLAN':
        lyt.swap_rlan(b)
        kind = 'rlan'
    elif magic == b'RFNT':
        lyt.swap_rfnt(b)
        kind = 'rfnt'
    elif magic == b'MESG':
        bmg.swap_bmg(b)
        kind = 'bmg'
    elif ext == '.tpl' and struct.unpack_from('>I', b, 0)[0] == lyt.TPL_VERSION:
        lyt.swap_tpl(b)
        kind = 'tpl'
    elif ext == '.bti':
        lyt.swap_timg(b)
        kind = 'bti'
    elif magic == b'ANDO':
        sw32(b, 8, (len(b) - 8) // 4)
        kind = 'canm'
    elif magic == b'JPAC':
        jpa.swap_jpc(b)
        kind = 'jpc'
    elif magic == b'AA_<':
        jaudio.swap_baa(b)
        kind = 'baa'
    elif ext == '.cit':
        jaudio.swap_cit(b)
        kind = 'cit'
    elif ext == '.bmt' and magic != b'J3D2':
        jaudio.swap_me_table(b)
        kind = 'me-table'
    elif ext == '.bme':
        jaudio.swap_me_seq(b)
        kind = 'me-seq'
    elif ext == '.brs':
        jaudio.swap_words(b)
        kind = 'remix'
    elif ext == '.bct':
        jaudio.swap_spk_table(b)
        kind = 'spk-table'
    elif ext == '.csw':
        jaudio.swap_spk_wave(b)
        kind = 'spk-wave'
    elif ext == '.ast' and magic == b'STRM':
        jaudio.swap_ast(b)
        kind = 'ast'
    elif (ext in BCSV_EXT or ext == '' or ext[1:].isdigit()) and bcsv.looks_like_bcsv(b):
        bcsv.swap(b)
        kind = 'bcsv'
    elif ext in TEXT_EXT or name == 'collisionversion':
        kind = 'text'
    elif ext in UNUSED_EXT:
        kind = 'raw' + ext
    else:
        kind = 'UNKNOWN' + (ext or ':' + magic.hex())
    stats[kind] += 1
    return b


def is_converted_kind(stats_before, stats_after):
    """True if the last cook_blob call converted something (not text/raw/unknown)."""
    for k in stats_after:
        if stats_after[k] != stats_before.get(k, 0):
            return not (k == 'text' or k.startswith('raw') or k.startswith('UNKNOWN'))
    return False


def cook_rarc(data, stats, replace=None):
    """Converts a (decompressed, big-endian) RARC; nested compressed files that
    need conversion are decompressed, converted and stored uncompressed.
    replace: {path: big-endian bytes} to convert in place of those files."""
    def cook_file(path, blob, flags):
        if replace and path in replace:
            stats['collision-from-model'] += 1
            return (cook_blob(path, bytearray(replace[path]), stats), flags & ~(rarc_swap.FLAG_COMPRESSED | rarc_swap.FLAG_YAZ0))
        if not flags & rarc_swap.FLAG_COMPRESSED:
            return cook_blob(path, blob, stats)
        if not flags & rarc_swap.FLAG_YAZ0 or bytes(blob[:4]) != b'Yaz0':
            stats['nested-yay0-kept'] += 1
            return None
        inner = bytearray(yaz0_decompress(bytes(blob)))
        plain = flags & ~(rarc_swap.FLAG_COMPRESSED | rarc_swap.FLAG_YAZ0)
        if inner[:4] == b'RARC':
            stats['nested-rarc'] += 1
            return (cook_rarc(inner, stats), plain)
        before = collections.Counter(stats)
        cooked = cook_blob(path, inner, stats)
        if is_converted_kind(before, stats):
            stats['nested-decompressed'] += 1
            return (cooked, plain)
        stats['nested-compressed-kept'] += 1
        return None

    return rarc_swap.swap_rarc(data, cook_file)


def cook_archive(src, dst, stats, notes):
    with open(src, 'rb') as fh:
        raw = fh.read()
    data = bytearray(yaz0_decompress(raw))
    if data[:4] == b'AA_<':
        return cook_blob(src.replace('\\', '/'), data, stats)
    if data[:4] != b'RARC':
        notes.append('%s: not a RARC archive (%r), copied decompressed' % (src, bytes(data[:4])))
        return data
    # Planets whose collision is rebuilt from their model (collision.py).
    replace = None
    rel = src.replace('\\', '/')
    for arc, model in collision.FROM_MODEL.items():
        if rel.endswith('/' + arc):
            files = {path: bytes(data[off:off + size]) for _, path, flags, off, size in rarc_swap.entries(data)
                     if not flags & rarc_swap.FLAG_COMPRESSED}
            replace = collision.rebuild(files, model)
    return cook_rarc(data, stats, replace)


def cook_dol_data(data_dir, out_dir):
    """Writes the data main.dol holds (dol_data.py) to <out>/sys; returns the
    errors."""
    path = os.path.join(data_dir, 'sys', 'main.dol')
    if not os.path.isfile(path):
        return ['%s is missing: extract the whole disc (its sys/ and files/ folders)' % path]
    with open(path, 'rb') as fh:
        dol = fh.read()
    out = {}
    archive = dol_data.find_error_archive(dol)
    if archive:
        out['ErrorMessageArchive.arc'] = cook_rarc(bytearray(archive), collections.Counter())
    for name in dol_data.TABLES:
        table = dol_data.find_table(dol, name)
        if table:
            table = bytearray(table)
            bcsv.swap(table)
            out[name + '.bcsv'] = table
    for name, data in out.items():
        with open(os.path.join(out_dir, 'sys', name), 'wb') as fh:
            fh.write(data)
    found = len(out) == 1 + len(dol_data.TABLES)
    print('[Engine] System tables extracted from main.dol: %s' % ', '.join(out.keys()))
    return [] if found else ['%s: the error message archive or a table was not found (not Super Mario Galaxy?)' % path]

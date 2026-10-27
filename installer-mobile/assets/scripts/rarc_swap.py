"""RARC archive container: big-endian -> little-endian in place.

Layout (JKRArchive.hpp): RarcHeader (8 x u32), RarcInfoBlock (6 x u32, 2 x u16,
u32), SDIDirEntry[] (u32 id, u32 name, u16 hash, u16 count, u32 first),
SDIFileEntry[] (u16 id, u16 hash, u32 flag<<24|name, u32 data, u32 size,
u32 runtime pointer), string table.  File contents are handed to `cook_file`
before the container is converted; it may also replace a file with data of
a different size (e.g. a decompressed and converted nested archive), in
which case the data section is laid out again.
"""
from util import u16, u32, sw16, sw32

FLAG_FOLDER = 0x02
FLAG_COMPRESSED = 0x04
FLAG_MRAM = 0x10
FLAG_ARAM = 0x20
FLAG_YAZ0 = 0x80


def _tables(b):
    """Locates the container's tables and checks that each one is inside the
    archive, returning (data_start, ndirs, dir_off, nfiles, file_off, str_off)
    with the offsets absolute.

    Every one of them is a number stored in the file, and a slice that runs off
    the end reads short instead of failing: the data section is then rebuilt
    around the truncated blob and the archive comes out self-consistent but
    missing its tail.  A damaged container has to stop the cook."""
    if len(b) < 0x20:
        raise ValueError('RARC archive is only %#x bytes' % len(b))
    hdr = u32(b, 8)
    if hdr < 0x20 or hdr + 0x20 > len(b):
        raise ValueError('RARC info block at %#x is outside the archive (%#x bytes)' % (hdr, len(b)))
    ndirs, dir_off, nfiles, file_off, _, str_off = (u32(b, hdr + 4 * i) for i in range(6))
    data_start = hdr + u32(b, 0xC)
    dir_off += hdr
    file_off += hdr
    str_off += hdr
    if data_start > len(b):
        raise ValueError('RARC data section at %#x is outside the archive (%#x bytes)' % (data_start, len(b)))
    if ndirs < 1 or dir_off + 0x10 * ndirs > len(b):
        raise ValueError('RARC directory table at %#x for %d entries is outside the archive (%#x bytes)'
                         % (dir_off, ndirs, len(b)))
    if file_off + 0x14 * nfiles > len(b):
        raise ValueError('RARC file table at %#x for %d entries is outside the archive (%#x bytes)'
                         % (file_off, nfiles, len(b)))
    if str_off >= len(b):
        raise ValueError('RARC string table at %#x is outside the archive (%#x bytes)' % (str_off, len(b)))
    return data_start, ndirs, dir_off, nfiles, file_off, str_off


def entries(b):
    """Yields (index, path, flags, data_offset, size) for every file entry."""
    data_start, ndirs, dir_off, nfiles, file_off, str_off = _tables(b)

    def cstr(o):
        start = str_off + o
        if start >= len(b):
            raise ValueError('RARC string at %#x is outside the archive (%#x bytes)' % (start, len(b)))
        end = b.index(b'\0', start, len(b))
        return bytes(b[start:end]).decode('shift_jis', errors='replace')

    dirs = []
    for i in range(ndirs):
        d = dir_off + 0x10 * i
        dirs.append((cstr(u32(b, d + 4)), u16(b, d + 0xA), u32(b, d + 0xC)))

    out = []

    def walk(di, prefix, depth):
        if depth > 64:
            raise ValueError('RARC directory loop')
        name, count, first = dirs[di]
        if first + count > nfiles:
            raise ValueError('RARC directory %s lists file entries %d..%d past the table (%d entries)'
                             % (prefix, first, first + count - 1, nfiles))
        for e in range(first, first + count):
            f = file_off + 0x14 * e
            word = u32(b, f + 4)
            flags, name_off = word >> 24, word & 0xFFFFFF
            ename = cstr(name_off)
            if flags & FLAG_FOLDER:
                if ename in ('.', '..'):
                    continue
                sub = u32(b, f + 8)
                if sub >= ndirs:
                    raise ValueError('RARC folder %s points at directory %d of %d' % (prefix + ename, sub, ndirs))
                walk(sub, prefix + ename + '/', depth + 1)
            else:
                off = data_start + u32(b, f + 8)
                size = u32(b, f + 0xC)
                if off + size > len(b):
                    raise ValueError('RARC file %s at %#x+%#x runs past the archive (%#x bytes)'
                                     % (prefix + ename, off, size, len(b)))
                out.append((e, prefix + ename, flags, off, size))

    walk(0, dirs[0][0] + '/', 0)
    return out


def _relayout(b, repl):
    """Rebuilds the data section (big-endian tables) with replaced files.
    repl: {entry index: (bytes, flags)}.  Keeps the original file order so
    the MRAM/ARAM/DVD grouping JKRCompArchive relies on is preserved."""
    data_start, ndirs, dir_off, nfiles, file_off, str_off = _tables(b)
    files = []
    for i in range(nfiles):
        f = file_off + 0x14 * i
        flags = u32(b, f + 4) >> 24
        if not flags & FLAG_FOLDER:
            off, size = u32(b, f + 8), u32(b, f + 0xC)
            if data_start + off + size > len(b):
                raise ValueError('RARC entry %d at %#x+%#x runs past the archive (%#x bytes)'
                                 % (i, data_start + off, size, len(b)))
            files.append((off, i, flags, size))
    files.sort()
    data = bytearray()
    placed = {}
    mram = aram = 0
    for off, i, flags, size in files:
        if i in repl:
            blob, flags = repl[i]
            key = ('new', i)
        else:
            blob = b[data_start + off:data_start + off + size]
            key = (off, size)
        if key in placed:
            noff = placed[key]
        else:
            noff = len(data)
            placed[key] = noff
            data += blob
            data += b'\0' * (-len(data) & 31)
        f = file_off + 0x14 * i
        name_off = u32(b, f + 4) & 0xFFFFFF
        b[f + 4:f + 16] = ((flags << 24) | name_off).to_bytes(4, 'big') + noff.to_bytes(4, 'big') + len(blob).to_bytes(4, 'big')
        aligned = (len(blob) + 31) & ~31
        if flags & FLAG_MRAM:
            mram += aligned
        elif flags & FLAG_ARAM:
            aram += aligned
    new = bytearray(b[:data_start]) + data
    new[4:8] = len(new).to_bytes(4, 'big')
    new[0x10:0x14] = len(data).to_bytes(4, 'big')
    new[0x14:0x18] = mram.to_bytes(4, 'big')
    new[0x18:0x1C] = aram.to_bytes(4, 'big')
    return new


def swap_rarc(b, cook_file):
    """cook_file(path, blob, flags) -> None (unchanged), a bytearray of the same
    size, or (new bytes, new flags) to replace the file.  For compressed
    entries `blob` is the stored (compressed) data.  Returns the converted
    archive (a new object if the data section had to be rebuilt)."""
    repl = {}
    for index, path, flags, off, size in entries(b):
        blob = bytearray(b[off:off + size])
        new = cook_file(path, blob, flags)
        if new is None:
            continue
        if isinstance(new, tuple):
            repl[index] = new
        elif len(new) != size:
            raise ValueError('%s changed size' % path)
        else:
            b[off:off + size] = new
    if repl:
        b = _relayout(b, repl)

    data_start, ndirs, dir_off, nfiles, file_off, str_off = _tables(b)
    hdr = u32(b, 8)
    info = hdr
    sw32(b, 0, 8)
    sw32(b, info, 6)
    sw16(b, info + 0x18, 2)
    sw32(b, info + 0x1C)
    for i in range(ndirs):
        d = dir_off + 0x10 * i
        sw32(b, d, 2)
        sw16(b, d + 8, 2)
        sw32(b, d + 0xC)
    for i in range(nfiles):
        f = file_off + 0x14 * i
        sw16(b, f, 2)
        sw32(b, f + 4, 4)
    return b

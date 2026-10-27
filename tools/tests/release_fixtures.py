"""APK and ELF fixtures for the release-gate tests.

They are kept out of the test module itself because the repository caps every
maintained file at 300 lines and the gate tests need the room.
"""
import io
import zipfile


def elf64(machine=183):
    header = bytearray(64)
    header[0:4] = b"\x7fELF"
    header[4] = 2
    header[5] = 1
    header[18:20] = machine.to_bytes(2, "little")
    return bytes(header)


def make_apk(**overrides):
    payload = {
        "AndroidManifest.xml": b"\x03\x00\x08\x00",
        "classes.dex": b"dex\n035\x00" + b"\x00" * 64,
        "assets/galaxy_shader_manifest.bin": b"manifest",
        "lib/arm64-v8a/libgame.so": elf64(),
        "lib/arm64-v8a/libmain.so": elf64(),
        "lib/arm64-v8a/libreserve.so": elf64(),
        "lib/arm64-v8a/libinstaller.so": elf64(),
        "lib/arm64-v8a/wrap.sh": b"#!/system/bin/sh\nexec \"$0\"\n",
    }
    for name, data in overrides.items():
        if data is None:
            payload.pop(name, None)
        else:
            payload[name] = data
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    buffer.seek(0)
    return zipfile.ZipFile(buffer)


def write_apk(path, **overrides):
    """The same payload as make_apk, on disk, so archive-level checks can open it."""
    with make_apk(**overrides) as apk:
        payload = {name: apk.read(name) for name in apk.namelist()}
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    return path

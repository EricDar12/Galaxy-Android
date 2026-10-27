"""End-to-end cooking tests using a synthetic disc; no game data is needed."""
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest

TOOLS = Path(__file__).resolve().parents[1]
COOK = TOOLS / 'cook'
MOBILE_SCRIPTS = TOOLS.parent / 'installer-mobile' / 'assets' / 'scripts'
sys.path.insert(0, str(COOK))
sys.path.insert(0, str(TOOLS))

import cook  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parent))
from cook_fixtures import _disc, _rarc  # noqa: E402


class CookPipelineTests(unittest.TestCase):
    def test_desktop_convert_promotes_a_complete_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            out = root / 'cooked'
            progress = []
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1, progress=lambda *a: progress.append(a))
            self.assertEqual(result, 0)
            self.assertEqual(progress[:1], [(0, 3)])
            self.assertEqual(progress[-1], (3, 3))
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'fst-fixture')
            self.assertEqual((out / 'sys' / 'ErrorMessageArchive.arc').read_bytes()[:4], b'CRAR')
            for name in ('StoryEvent.bcsv', 'GalaxyID.bcsv'):
                self.assertTrue((out / 'sys' / name).is_file(), name)
            self.assertEqual((out / 'files' / 'ObjectData' / 'Readme.txt').read_bytes(), b'plain text')
            self.assertEqual((out / 'files' / 'StageData' / 'Model.arc').read_bytes()[:4], b'CRAR')
            header = (out / 'files' / 'StageData' / 'Table.bcsv').read_bytes()[:16]
            self.assertEqual(struct.unpack('<iiii', header), (1, 2, 40, 8))
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_worker_failure_is_reported_and_live_tree_is_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            (data / 'files' / 'StageData' / 'Broken.arc').write_bytes(b'RARC')
            out = root / 'GalaxyData'
            (out / 'sys').mkdir(parents=True)
            (out / 'sys' / 'fst.bin').write_bytes(b'previous')
            (out / 'saves' / 'slot1').mkdir(parents=True)
            (out / 'saves' / 'slot1' / 'GameData.bin').write_bytes(b'save')
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(result, 1)
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'previous')
            self.assertEqual((out / 'saves' / 'slot1' / 'GameData.bin').read_bytes(), b'save')
            self.assertFalse((out / 'sys' / 'StoryEvent.bcsv').exists())
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_missing_system_tables_fail_before_conversion(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            (data / 'sys' / 'main.dol').write_bytes(b'not a dol')
            out = root / 'cooked'
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(result, 1)
            self.assertFalse(out.exists())
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_mobile_scripts_convert_and_preserve_saves(self):
        if not MOBILE_SCRIPTS.is_dir():
            self.skipTest('mobile scripts are not present')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            out = root / 'GalaxyData'
            (out / 'saves' / 'slot1').mkdir(parents=True)
            (out / 'saves' / 'slot1' / 'GameData.bin').write_bytes(b'save')
            runner = root / 'run_mobile_cook.py'
            runner.write_text(
                'import sys\n'
                'sys.dont_write_bytecode = True\n'
                'sys.path.insert(0, %r)\n'
                'import cook\n'
                'raise SystemExit(cook.convert(data_dir=%r, out_dir=%r, jobs=1))\n'
                % (str(MOBILE_SCRIPTS), str(data), str(out)),
                encoding='utf-8')
            result = subprocess.run([sys.executable, str(runner)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((out / 'saves' / 'slot1' / 'GameData.bin').read_bytes(), b'save')
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'fst-fixture')
            self.assertEqual((out / 'sys' / 'ErrorMessageArchive.arc').read_bytes()[:4], b'CRAR')
            self.assertEqual((out / 'files' / 'ObjectData' / 'Readme.txt').read_bytes(), b'plain text')
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])

    def test_parallel_cook_notes_are_per_thread(self):
        import threading
        import j3d

        del j3d.warnings[:]
        barrier = threading.Barrier(2)
        seen = {}

        def worker(name):
            del j3d.warnings[:]
            barrier.wait()
            j3d.warnings.append(name)
            barrier.wait()
            seen[name] = list(j3d.warnings)

        threads = [threading.Thread(target=worker, args=(name,))
                   for name in ('left', 'right')]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=10)
        self.assertEqual(seen, {'left': ['left'], 'right': ['right']})
        self.assertEqual(['note'] + j3d.warnings, ['note'],
                         'worker notes leaked into the parent thread')

    def test_shared_cook_modules_stay_identical(self):
        if not MOBILE_SCRIPTS.is_dir():
            self.skipTest('mobile scripts are not present')
        for name in ('cook_archive.py', 'cook_output.py', 'cook_warnings.py',
                     'j3d.py', 'j3d_anim.py', 'kcl_builder.py',
                     'lyt.py', 'jpa.py', 'jaudio.py'):
            self.assertEqual((COOK / name).read_bytes(), (MOBILE_SCRIPTS / name).read_bytes(), name)

    def test_sound_animation_cooking_regression_issue_116(self):
        import j3d
        payload = bytearray(struct.pack('>HHI', 1, 0, 0x12345678) + b'\xAA' * 0x20)
        j3d.swap_sound_anim(payload, 0)
        self.assertEqual(struct.unpack('<HHI', payload[:8]), (1, 0, 0x12345678))

    def test_circular_import_immunity(self):
        for script_dir in (str(COOK), str(MOBILE_SCRIPTS)):
            for first, second in (('j3d', 'j3d_anim'), ('j3d_anim', 'j3d')):
                cmd = [sys.executable, '-c',
                       f'import sys; sys.path.insert(0, {script_dir!r}); import {first}; import {second}']
                res = subprocess.run(cmd, capture_output=True, text=True)
                self.assertEqual(res.returncode, 0, f'{script_dir}: {res.stderr}')

    def test_failed_cook_creates_failed_marker_and_cleans_on_retry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            broken = data / 'files' / 'StageData' / 'Broken.arc'
            broken.write_bytes(b'RARC')
            out = root / 'GalaxyData'
            res = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(res, 1)
            self.assertTrue((out / '.install_failed').is_file())
            self.assertFalse((out / '.installed_ok').is_file())
            broken.unlink()
            res2 = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(res2, 0)
            self.assertFalse((out / '.install_failed').is_file())
            self.assertTrue((out / '.installed_ok').is_file())


    def test_swapper_counts_stop_at_the_buffer_end(self):
        import jaudio
        b = bytearray(0x40)
        s = jaudio.Swapper(b)
        started = time.time()
        # A damaged table can ask for a billion values; the run has to end with
        # the buffer rather than iterate them, because the app cooks in embedded
        # Python where a runaway loop has no watchdog.
        s.w32(0x3C, 1 << 30)
        s.w16(0x3E, 1 << 30)
        self.assertLess(time.time() - started, 1.0, 'the cooker ran away over a stored count')
        self.assertEqual(s.done, {0x3C, 0x3E})

    def test_damaged_table_counts_do_not_run_away(self):
        import jaudio
        b = bytearray(0x100)
        base, root, sec, grp = 0x40, 0x50, 0x60, 0x70
        struct.pack_into('>I', b, base + 12, root - base)
        struct.pack_into('>I', b, root, 1)              # one section
        struct.pack_into('>I', b, root + 4, sec - base)
        struct.pack_into('>I', b, sec, 1)               # one group
        struct.pack_into('>I', b, sec + 8, grp - base)  # BSTN lists groups at +8
        struct.pack_into('>I', b, grp, 0xFFFFFFFF)      # the group's entry count
        started = time.time()
        jaudio.swap_bstn(b, base)
        self.assertLess(time.time() - started, 1.0, 'the cooker ran away over a stored count')

    def test_swapper_ignores_offsets_before_the_buffer(self):
        import jaudio
        b = bytearray(0x40)
        b[0x38:0x40] = b'\0\0\x11\x22\x33\x44\x55\x66'
        untouched = bytes(b[0x38:0x40])
        s = jaudio.Swapper(b)
        # A negative offset wraps onto the tail of the buffer and byte-swaps a
        # record that belongs to something else.
        s.w32(-8)
        s.w16(-8)
        self.assertEqual(bytes(b[0x38:0x40]), untouched)
        self.assertEqual(s.done, set())

    def test_util_swaps_ignore_negative_offsets(self):
        import util
        b = bytearray(0x20)
        b[0x18:0x20] = b'\0\0\x11\x22\x33\x44\x55\x66'
        untouched = bytes(b)
        util.sw32(b, -8)
        util.sw16(b, -8)
        util.sw_range(b, -8, 0x20, 2)
        self.assertEqual(bytes(b), untouched)

    def test_rarc_container_is_bounds_checked(self):
        import rarc_swap
        good = bytearray(_rarc('data', 'note.txt', b'hello'))
        self.assertEqual([entry[1] for entry in rarc_swap.entries(good)], ['/data/note.txt'])
        # Every offset below is a number the archive uses to address its own
        # tables and file data; pointing one past the end used to read a short
        # slice and rebuild the container around it, losing data silently.
        for what, position, fmt, value in (
                ('archive header', 0x8, '>I', 0x1000),
                ('data section', 0xC, '>I', 0x1000),
                ('directory count', 0x20, '>I', 200),
                ('file count', 0x28, '>I', 200),
                ('string table', 0x34, '>I', 0x1000),
                ('directory file range', 0x40 + 0x10 + 0xA, '>H', 40),
                ('folder directory index', 0x60 + 0x8, '>I', 99),
                ('file data offset', 0x74 + 0x8, '>I', 0x1000),
                ('file size', 0x74 + 0xC, '>I', 0x1000),
                ('file name offset', 0x74 + 0x4, '>I', 0x1000)):
            bad = bytearray(good)
            struct.pack_into(fmt, bad, position, value)
            with self.assertRaises(ValueError, msg=what):
                rarc_swap.entries(bad)

    def test_archive_entry_past_the_end_fails_the_cook(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            model = data / 'files' / 'StageData' / 'Model.arc'
            archive = bytearray(model.read_bytes())
            struct.pack_into('>I', archive, 0x74 + 0xC, 0x1000)
            model.write_bytes(bytes(archive))
            out = root / 'GalaxyData'
            out.mkdir()
            (out / '.installed_ok').write_text('ok\n')
            with contextlib.redirect_stdout(io.StringIO()):
                result = cook.convert(str(data), str(out), jobs=1)
            self.assertEqual(result, 1)
            self.assertTrue((out / '.install_failed').is_file())
            self.assertFalse((out / '.installed_ok').exists(),
                             'a failed cook must not leave the tree looking installed')
            self.assertFalse((out / 'files').exists())

    def test_partial_promotion_marks_the_live_tree_failed(self):
        import cook_output
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = _disc(root / 'disc')
            out = root / 'GalaxyData'
            (out / 'sys').mkdir(parents=True)
            (out / 'sys' / 'fst.bin').write_bytes(b'previous')
            (out / '.installed_ok').write_text('ok\n')
            real_promote = cook_output.promote

            def interrupted_promote(staging, out_dir):
                # promote() moves file by file, so however far it got before the
                # volume fails, the live tree is a mixture of both installs.
                raise OSError('no space left on device')

            cook_output.promote = interrupted_promote
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    result = cook.convert(str(data), str(out), jobs=1)
            finally:
                cook_output.promote = real_promote
            self.assertEqual(result, 1)
            self.assertTrue((out / '.install_failed').is_file())
            self.assertFalse((out / '.installed_ok').exists(),
                             'a half-replaced tree must not keep the old install sentinel')
            self.assertEqual((out / 'sys' / 'fst.bin').read_bytes(), b'previous')
            self.assertEqual(list(root.glob('.galaxy-cook-*')), [])


if __name__ == '__main__':
    unittest.main()

#!/usr/bin/env python3
"""Converts an extracted Super Mario Galaxy disc into the port's data layout.

  cook.py <extracted disc (or its DATA dir)> <output dir> [--jobs N] [--only SUBSTR] [--no-audio] [--with-movies]

--with-movies adds the prologue and ending movies (2.3 GB).

Output: <out>/sys/fst.bin (copied) and <out>/files/... with every archive
decompressed and every big-endian structure the port maps directly converted
to little-endian.  Formats that the port reads as byte streams (display
lists, textures, sequences, ghost data) keep their original byte order.
<out>/sys/ also gets the data sys/main.dol holds rather than the files
(ErrorMessageArchive.arc, StoryEvent.bcsv, GalaxyID.bcsv; see dol_data.py).
"""
import argparse
import collections
import concurrent.futures
import multiprocessing
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

# The per-format converters and the archive walkers live in cook_archive.py.
from cook_archive import cook_archive, cook_blob, cook_dol_data, cook_rarc  # noqa: E402
import collision  # noqa: E402
import dol_data  # noqa: E402
import rarc_swap  # noqa: E402
import thp  # noqa: E402


def plan(data_dir, out_dir, args):
    jobs = []
    files = os.path.join(data_dir, 'files')
    for dp, _, names in os.walk(files):
        rel = os.path.relpath(dp, files).replace('\\', '/')
        top = rel.split('/')[0]
        if top == 'AudioRes' and args.no_audio:
            continue
        if top == 'MovieData' and not args.with_movies:
            continue
        for n in names:
            src = os.path.join(dp, n)
            relp = n if rel == '.' else rel + '/' + n
            if args.only and args.only.lower() not in relp.lower():
                continue
            dst = os.path.abspath(os.path.join(out_dir, 'files', relp))
            out_root = os.path.abspath(os.path.join(out_dir, 'files'))
            if not dst.startswith(out_root + os.sep) and dst != out_root:
                print('[Security Warning] Skipping traversal path in plan: %s' % relp)
                continue
            ext = os.path.splitext(n)[1].lower()
            if ext in ('.arc', '.szs'):
                mode = 'archive'
            elif ext in ('.bcsv', '.tbl', '.ast'):
                mode = 'file'
            elif ext == '.thp':
                mode = 'thp'
            else:
                mode = 'copy'
            jobs.append((src, dst, mode))
    return jobs


def mark_install_failed(out_dir, errors):
    """Records that this cook did not finish, in the live output folder.

    The launchers refuse a directory holding .install_failed, and they accept a
    directory holding .installed_ok without looking at the files, so both marks
    have to move together: a tree that was only half replaced, or a retry that
    left an older .installed_ok behind, would otherwise boot straight into
    missing or mismatched data."""
    try:
        os.makedirs(out_dir, exist_ok=True)
        ok = os.path.join(out_dir, '.installed_ok')
        if os.path.isfile(ok):
            os.remove(ok)
        with open(os.path.join(out_dir, '.install_failed'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(errors[:20]))
    except OSError:
        pass


def convert(data_dir, out_dir, jobs=None, only=None, no_audio=False,
            with_movies=False, progress=None):
    """Run the same conversion from the CLI or the desktop installer.

    progress(completed, total) is called in the parent process, never in
    the conversion workers. A nonzero return means files were not converted.
    """
    from cook_output import process, promote, stage_dir
    args = argparse.Namespace(data_dir=data_dir, out_dir=out_dir,
                              jobs=jobs or os.cpu_count(), only=only,
                              no_audio=no_audio, with_movies=with_movies)
    # The game partition holds sys/ and files/; Dolphin extracts it to DATA/
    # under the folder it was given; nodtool extracts directly or into DATA.
    if not os.path.isfile(os.path.join(args.data_dir, 'sys', 'fst.bin')):
        inner = os.path.join(args.data_dir, 'DATA')
        if os.path.isfile(os.path.join(inner, 'sys', 'fst.bin')):
            args.data_dir = inner
        else:
            found_data = None
            for root, dirs, files in os.walk(args.data_dir):
                if 'fst.bin' in files and os.path.basename(root).lower() == 'sys':
                    found_data = os.path.dirname(root)
                    break
            if found_data:
                args.data_dir = found_data
            else:
                raise FileNotFoundError(f"{args.data_dir} holds no sys/fst.bin: extraction directory may be empty or incomplete")

    staging = stage_dir(args.out_dir)
    try:
        os.makedirs(os.path.join(staging, 'sys'))
        shutil.copyfile(os.path.join(args.data_dir, 'sys', 'fst.bin'),
                        os.path.join(staging, 'sys', 'fst.bin'))
        errors = cook_dol_data(args.data_dir, staging)
        if errors:
            # Nothing has been written outside the staging directory yet, so the
            # live tree keeps whatever markers it had: the source disc is wrong,
            # not the data already installed.
            print('[Engine] Conversion failed: %s' % errors[0])
            return 1

        jobs = plan(args.data_dir, staging, args)
        if progress:
            progress(0, len(jobs))
        t0 = time.time()
        total = collections.Counter()
        notes = collections.Counter()
        # On Android embedded Python, child processes cannot be spawned,
        # so we use ThreadPoolExecutor which runs natively within the app.
        workers = min(4, args.jobs or 4)
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
            results = ex.map(process, jobs)
            for done, ((stats, n, err, src), job) in enumerate(zip(results, jobs), 1):
                total.update(stats)
                notes.update(n)
                if err:
                    errors.append(err)
                elif not os.path.isfile(job[1]):
                    errors.append('%s: converter reported success but wrote no output' % job[0])
                if progress:
                    # Update UI frequently with current file being processed
                    if done % 2 == 0 or done == len(jobs) or done <= 50:
                        progress(done, len(jobs), src)
                if done % 100 == 0 or done == len(jobs):
                    pct = int(done * 100 / len(jobs)) if len(jobs) > 0 else 0
                    print(f"[Engine] Converted {done}/{len(jobs)} files ({pct}%)...")
        print('cooked %d disc files in %.1fs' % (len(jobs), time.time() - t0))
        for k, v in sorted(total.items()):
            print('  %-24s %6d' % (k, v))
        for msg, v in notes.most_common(40):
            print('note (%dx): %s' % (v, msg))
        if errors:
            print('[Engine] Conversion failed with %d error(s):' % len(errors))
            for e in errors[:20]:
                print('  [Error]', e)
            mark_install_failed(args.out_dir, errors)
            return 1
        # Fail closed: never report success without the core converted output,
        # and never promote a partial tree over the live game data.
        if not os.path.isfile(os.path.join(staging, 'sys', 'fst.bin')):
            print('[Engine] Conversion failed: sys/fst.bin missing from the staged output')
            mark_install_failed(args.out_dir, ['the conversion produced no sys/fst.bin'])
            return 1
        try:
            promote(staging, args.out_dir)
            staging = None
            try:
                fail_path = os.path.join(args.out_dir, '.install_failed')
                if os.path.isfile(fail_path):
                    os.remove(fail_path)
                with open(os.path.join(args.out_dir, '.installed_ok'), 'w', encoding='utf-8') as f:
                    f.write('ok\n')
            except OSError:
                pass
        except OSError as e:
            print('[Engine] Could not move converted data into %s: %s' % (args.out_dir, e))
            # promote() moves file by file, so the live tree is now a mixture of
            # the old install and this one, however far the move got.
            mark_install_failed(args.out_dir, ['moving the converted data into %s failed: %s' % (args.out_dir, e)])
            return 1
    finally:
        if staging is not None:
            shutil.rmtree(staging, ignore_errors=True)
    return 0


def main(args=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('data_dir')
    ap.add_argument('out_dir')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    ap.add_argument('--only')
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--with-movies', action='store_true')
    parsed = ap.parse_args(args[1:] if args and len(args) > 1 else None)
    return convert(**vars(parsed))


if __name__ == '__main__':
    sys.exit(main())

# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Runs a QtTest executable of a Windows ASan build tree (PDF4QT_ENABLE_MSVC_ASAN=ON, arch/checks.md
"Windows ASan") split into one process per test function and data tag, keeps the output of every process
and counts the reports of AddressSanitizer. Windows only (Linux: the ASan + UBSan tree of arch/checks.md).

Usage (only the standard library; run it with python -I):

    python -I UnitTests/runtest_asan.py <exe name> --tree <build dir> --temp <dir> [options]

    --tree DIR       the build tree configured with PDF4QT_ENABLE_MSVC_ASAN=ON (its CMakeCache.txt names Qt,
                     the prefixes of vcpkg and the compiler); the tests are in <tree>/usr/bin (--bin DIR)
    --temp DIR       TEMP / TMP of the tests; refused on the system drive (C:) or under the system TEMP
    --out DIR        the results (default <tree>/asan_runs): <out>/<exe>/<time>_<pid>/<sweep>/<item>.txt
                     (QtTest, -o <file>,txt) and <item>.err (stdout + stderr of the process: the reports
                     of ASan); runs older than a day are removed
    --jobs N         parallel processes (default 8, at most 24)
    --filter REGEX   only the items "function" / "function:tag" matching REGEX
    --whole          no split: the whole executable in one process
    --repeat N       N sweeps over all items, run at the same time
    --timeout S      timeout of one process in seconds (default 3600)
    --full-stacks    the stacks of the allocation and of the release in a report (the default
                     malloc_context_size=0 makes the run about 3 times faster); rerun a hit with it
    --env K=V        extra environment variable, also ASAN_OPTIONS (repeatable)
    --arg=ARG        extra argument of every test process, e.g. --arg=-nocrashhandler (repeatable)
    --qt-bin DIR     the bin directory of Qt (default: from Qt6_DIR of the cache, else the bin directory
                     of a prefix of the cache holding Qt6Core.dll)
    --asan-runtime DIR  the directory of clang_rt.asan_dynamic-x86_64.dll (default: the directory of
                     the compiler of the cache, %VCToolsInstallDir%\\bin\\Hostx64\\x64, then PATH)
    --slowest N      number of the slowest items printed (default 10)

The environment of a test: the runtime of ASan, Qt's bin, the bin directories of the prefixes of the cache
(vcpkg) and the tests on PATH, ASAN_OPTIONS (below), QTEST_FUNCTION_TIMEOUT=1800000 (the 300 s of QtTest
are too short for a lifetime row under ASan), PDF4QT_TEST_TIMEOUT_FACTOR=3, QT_QPA_PLATFORM=offscreen.
The processes run with the priority "below normal". PDF4QT_TEST_SLOT_DIR=<dir> shares a machine-wide limit
of PDF4QT_TEST_SLOTS (default 12, at most 24) processes with the other runs using the same directory.

Every test process runs in its own job object (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE): a timeout, Ctrl+C or
the end of the runner kills the whole tree, also the processes started by the test (QProcess).

The exit code is 1, when an item failed, crashed, timed out or ASan reported an error (exit code 113,
"ERROR: AddressSanitizer" in its .err file), when the listing of the items failed or the run was
interrupted, else 0.
"""
import argparse
import concurrent.futures
import ctypes
import ctypes.wintypes as wintypes
import functools
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

SPECIAL = ('initTestCase', 'cleanupTestCase', 'init', 'cleanup')
ASAN_RUNTIME = 'clang_rt.asan_dynamic-x86_64.dll'
ASAN_EXIT_CODE = 113
# As the Linux ASan tree without detect_leaks (no LeakSanitizer on Windows); the stack unwinding of every
# allocation and release makes a run 7-8 times slower than Release, without it 2.5-3 times
ASAN_OPTIONS = 'halt_on_error=1:exitcode=113:allocator_may_return_null=1:check_initialization_order=1:symbolize=1'
ASAN_OPTIONS_SWEEP = ':malloc_context_size=0'
# Frames of the stack of the access printed in the summary (the whole report is in the .err file)
REPORT_FRAMES = 4
BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
LISTING_TIMEOUT = 600
# Slots of this run, which release_slot could not delete (they carry the pid of this living runner)
LEAKED_SLOTS = set()
# Job objects of the running test processes (terminated on Ctrl+C) and the stop of the queued items
RUNNING_JOBS = set()
RUNNING_LOCK = threading.Lock()
STOP = threading.Event()

JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS = 9


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [('ReadOperationCount', ctypes.c_ulonglong), ('WriteOperationCount', ctypes.c_ulonglong),
                ('OtherOperationCount', ctypes.c_ulonglong), ('ReadTransferCount', ctypes.c_ulonglong),
                ('WriteTransferCount', ctypes.c_ulonglong), ('OtherTransferCount', ctypes.c_ulonglong)]


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                ('LimitFlags', wintypes.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wintypes.DWORD),
                ('Affinity', ctypes.c_size_t), ('PriorityClass', wintypes.DWORD), ('SchedulingClass', wintypes.DWORD)]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [('BasicLimitInformation', JOBOBJECT_BASIC_LIMIT_INFORMATION), ('IoInfo', IO_COUNTERS),
                ('ProcessMemoryLimit', ctypes.c_size_t), ('JobMemoryLimit', ctypes.c_size_t),
                ('PeakProcessMemoryUsed', ctypes.c_size_t), ('PeakJobMemoryUsed', ctypes.c_size_t)]


@functools.lru_cache(maxsize=None)
def kernel32():
    library = ctypes.WinDLL('kernel32', use_last_error=True)
    library.CreateJobObjectW.restype = wintypes.HANDLE
    library.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    library.SetInformationJobObject.restype = wintypes.BOOL
    library.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    library.AssignProcessToJobObject.restype = wintypes.BOOL
    library.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    library.TerminateJobObject.restype = wintypes.BOOL
    library.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    library.CloseHandle.restype = wintypes.BOOL
    library.CloseHandle.argtypes = [wintypes.HANDLE]
    return library


def start_in_job(command, **popen_arguments):
    """Starts a process in its own job object, which kills the whole tree when it is terminated or closed.
    Returns (process, job handle or None); without a job the tree is killed by taskkill /T."""
    process = subprocess.Popen(command, **popen_arguments)
    # The process runs before it is assigned; a test starts its children much later (Qt, ASan and QtTest
    # initialize first), and a child started before the assignment is still killed by taskkill /T
    job = None
    try:
        library = kernel32()
        handle = library.CreateJobObjectW(None, None)
        if handle:
            limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if (library.SetInformationJobObject(handle, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS,
                                                ctypes.byref(limits), ctypes.sizeof(limits))
                    and library.AssignProcessToJobObject(handle, wintypes.HANDLE(int(process._handle)))):
                job = handle
            else:
                library.CloseHandle(handle)
    except (OSError, AttributeError):
        job = None
    if job:
        with RUNNING_LOCK:
            RUNNING_JOBS.add(job)
    return process, job


def kill_tree(process, job):
    """Kills the process and every process it has started"""
    if process.poll() is None:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(process.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if job:
        kernel32().TerminateJobObject(job, 1)
    try:
        process.kill()
    except OSError:
        pass


def close_job(job):
    """Closes the job: the processes still left in it (a child of a finished test) are killed"""
    if job:
        # Under the lock: Ctrl+C terminates the registered jobs, never a closed (reused) handle
        with RUNNING_LOCK:
            RUNNING_JOBS.discard(job)
            kernel32().CloseHandle(job)


def read_cache(tree):
    """Returns the entries NAME -> value of <tree>/CMakeCache.txt"""
    cache = {}
    path = os.path.join(tree, 'CMakeCache.txt')
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                match = re.match(r'^([A-Za-z0-9_.+-]+):[A-Z]+=(.*)$', line.rstrip('\r\n'))
                if match:
                    cache[match.group(1)] = match.group(2)
    except OSError:
        sys.exit(f'{path} not found: --tree is the build directory of a configured tree')
    return cache


def find_asan_runtime(a, cache):
    candidates = []
    if a.asan_runtime:
        candidates.append(a.asan_runtime)
    compiler = cache.get('CMAKE_CXX_COMPILER', '')
    if compiler:
        candidates.append(os.path.dirname(compiler))
    tools = os.environ.get('VCToolsInstallDir', '')
    if tools:
        candidates.append(os.path.join(tools, 'bin', 'Hostx64', 'x64'))
    candidates.extend(os.environ.get('PATH', '').split(os.pathsep))
    for directory in candidates:
        if directory and os.path.isfile(os.path.join(directory, ASAN_RUNTIME)):
            return os.path.normpath(directory)
    sys.exit(f'{ASAN_RUNTIME} not found (--asan-runtime DIR, or run from a vcvars64 environment)')


def find_qt_bin(a, cache):
    if a.qt_bin:
        return os.path.normpath(a.qt_bin)
    candidates = []
    qt_dir = cache.get('Qt6_DIR', '')
    if qt_dir:
        candidates.append(os.path.normpath(os.path.join(qt_dir, '..', '..', '..', 'bin')))
    candidates.extend(prefix_bins(cache))
    for directory in candidates:
        if os.path.isfile(os.path.join(directory, 'Qt6Core.dll')):
            return directory
    sys.exit('Qt6Core.dll not found by Qt6_DIR or the prefixes of the cache (--qt-bin DIR)')


def prefix_bins(cache):
    """The bin directories of the prefixes of the cache (CMAKE_PREFIX_PATH, vcpkg installed directory)"""
    prefixes = [p for p in cache.get('CMAKE_PREFIX_PATH', '').split(';') if p]
    installed = cache.get('_VCPKG_INSTALLED_DIR', '') or cache.get('VCPKG_INSTALLED_DIR', '')
    triplet = cache.get('VCPKG_TARGET_TRIPLET', '')
    if installed and triplet:
        prefixes.append(os.path.join(installed, triplet))
    bins = []
    for prefix in prefixes:
        directory = os.path.normpath(os.path.join(prefix, 'bin'))
        if os.path.isdir(directory) and directory not in bins:
            bins.append(directory)
    return bins


def checked_temp(path):
    """The absolute --temp; refused on the system drive or under the system TEMP (the tests write a lot)"""
    temp_dir = os.path.normcase(os.path.abspath(path))
    system_drive = os.path.normcase(os.environ.get('SystemDrive', 'C:'))
    if os.path.splitdrive(temp_dir)[0] == system_drive:
        sys.exit(f'--temp {path}: not on the system drive {system_drive}')
    for directory in filter(None, (os.environ.get('TEMP'), os.environ.get('TMP'), tempfile.gettempdir())):
        directory = os.path.normcase(os.path.abspath(directory))
        try:
            inside = os.path.commonpath([temp_dir, directory]) == directory
        except ValueError:
            inside = False  # another drive
        if inside:
            sys.exit(f'--temp {path}: not under the system TEMP {directory}')
    return os.path.abspath(path)


def environment(a, cache, bin_dir):
    env = dict(os.environ)
    path = [find_asan_runtime(a, cache), find_qt_bin(a, cache)] + prefix_bins(cache) + [os.path.normpath(bin_dir)]
    env['PATH'] = os.pathsep.join(path) + os.pathsep + env.get('PATH', '')
    env['ASAN_OPTIONS'] = ASAN_OPTIONS + ('' if a.full_stacks else ASAN_OPTIONS_SWEEP)
    env['QTEST_FUNCTION_TIMEOUT'] = '1800000'
    env['PDF4QT_TEST_TIMEOUT_FACTOR'] = '3'
    env['QT_QPA_PLATFORM'] = 'offscreen'
    temp_dir = checked_temp(a.temp)
    os.makedirs(temp_dir, exist_ok=True)
    env['TEMP'] = temp_dir
    env['TMP'] = temp_dir
    for item in a.env:
        key, _, value = item.partition('=')
        env[key] = value
    return env


def run_listing(exe, option, env, cwd):
    """Returns the stdout of "<exe> <option>"; a crash (an ASan report in a static initializer, a missing DLL)
    or a timeout ends the runner with the exit code and the output of the process"""
    process, job = start_in_job([exe, option], cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, errors='replace')
    try:
        stdout, stderr = process.communicate(timeout=LISTING_TIMEOUT)
    except subprocess.TimeoutExpired:
        kill_tree(process, job)
        stdout, stderr = process.communicate()
        close_job(job)
        sys.exit(f'{exe} {option}: timeout {LISTING_TIMEOUT} s\n{stderr[-4000:]}')
    close_job(job)
    if process.returncode != 0:
        # 0xC0000135 is a missing DLL; 113 a report of ASan
        sys.exit(f'{exe} {option}: exit code {process.returncode} ({process.returncode & 0xFFFFFFFF:#x})\n'
                 f'stderr:\n{stderr[-8000:]}\nstdout:\n{stdout[-2000:]}')
    return stdout


def list_items(exe, env, cwd):
    functions = run_listing(exe, '-functions', env, cwd).split()
    functions = [f[:-2] for f in functions if f.endswith('()') and f[:-2] not in SPECIAL]
    tags = run_listing(exe, '-datatags', env, cwd).splitlines()
    tagged = {}
    for line in tags:
        parts = line.split(None, 2)
        if len(parts) == 3:
            tagged.setdefault(parts[1], []).append(parts[2].strip())
    items = []
    for function in functions:
        for tag in tagged.get(function, [None]):
            items.append(function if tag is None else function + ':' + tag)
    return items


def is_process_alive(pid):
    try:
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return code.value == 259  # STILL_ACTIVE
    except Exception:
        return True


def acquire_slot(slot_dir, maximum):
    """Takes one of the machine-wide slots (waits until one is free), returns its path"""
    os.makedirs(slot_dir, exist_ok=True)
    while True:
        for index in range(maximum):
            path = os.path.join(slot_dir, str(index))
            try:
                os.mkdir(path)
            except PermissionError:
                # the folder of a released slot is still being deleted
                continue
            except FileExistsError:
                if path in LEAKED_SLOTS:
                    shutil.rmtree(path, ignore_errors=True)
                    if not os.path.exists(path):
                        LEAKED_SLOTS.discard(path)
                    elif len(LEAKED_SLOTS) >= maximum:
                        raise RuntimeError(f'no slot of {slot_dir} can be deleted by this run - check the permissions of the folder')
                    continue
                # a slot of a run, which has ended without releasing it
                try:
                    with open(os.path.join(path, 'pid'), encoding='utf-8') as f:
                        owner = int(f.read().strip() or '0')
                    if owner and not is_process_alive(owner):
                        shutil.rmtree(path, ignore_errors=True)
                except (OSError, ValueError):
                    try:
                        if time.time() - os.path.getmtime(path) > 120:
                            shutil.rmtree(path, ignore_errors=True)
                    except OSError:
                        pass
                continue
            with open(os.path.join(path, 'pid'), 'w', encoding='utf-8') as f:
                f.write(str(os.getpid()))
            return path
        time.sleep(0.5)


def release_slot(path):
    # Another runner may read the pid file at the same time (Windows refuses to delete an open file): retry
    for _ in range(400):
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.exists(path):
            return
        time.sleep(0.05)
    LEAKED_SLOTS.add(path)
    print(f'WARNING: slot {path} could not be released, retried by the next acquire', flush=True)


def run_item(exe, selector, out_dir, env, a, cwd, slots):
    safe = re.sub(r'[^A-Za-z0-9_.-]', '_', selector or 'whole')
    # The file system ignores the case ("empty TJ" / "empty Tj") and MAX_PATH limits QtTest: a short name with a hash
    digest = hashlib.sha1((selector or 'whole').encode('utf-8')).hexdigest()[:8]
    result = os.path.join(out_dir, safe[:40] + '_' + digest + '.txt')
    err = result[:-4] + '.err'
    command = [exe] + ([selector] if selector else []) + a.arg + ['-o', result + ',txt']
    start = time.time()
    if STOP.is_set():
        return selector or 'whole', 'NOT RUN (interrupted)', 0.0, result, err
    slot = None
    try:
        slot = acquire_slot(*slots) if slots else None
        with open(err, 'wb') as err_file:
            process, job = start_in_job(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=err_file,
                                        stderr=subprocess.STDOUT, creationflags=BELOW_NORMAL_PRIORITY_CLASS)
            try:
                code = process.wait(timeout=a.timeout)
            except subprocess.TimeoutExpired:
                kill_tree(process, job)
                process.wait()
                code = 'TIMEOUT'
            finally:
                close_job(job)
        if STOP.is_set() and code != 0:
            code = f'{code} (interrupted)'
    except Exception as exception:
        # A slot or the start of the process failed: listed as a problem, the run goes on
        code = f'ERROR: {type(exception).__name__}: {exception}'
    finally:
        if slot:
            release_slot(slot)
    return selector or 'whole', code, time.time() - start, result, err


def parse(result):
    """Returns (passed, failed, skipped, failure blocks, has totals) of a QtTest result file"""
    try:
        text = open(result, encoding='utf-8', errors='replace').read()
    except OSError:
        return 0, 0, 0, [], False
    passed = failed = skipped = 0
    failures = []
    lines = text.splitlines()
    has_totals = False
    for i, line in enumerate(lines):
        if line.startswith('PASS   :'):
            if not re.search(r'::(initTestCase|cleanupTestCase)\(\)', line):
                passed += 1
        elif line.startswith('FAIL!  :') or line.startswith('XPASS  :'):
            failed += 1
            block = [line[:400]]
            for follow in lines[i + 1:i + 12]:
                if follow.startswith(('PASS', 'FAIL', 'SKIP', 'QWARN', 'QDEBUG', 'QINFO', 'Totals', 'XFAIL', 'XPASS', 'BPASS', 'BFAIL')):
                    break
                block.append(follow[:400])
            failures.append('\n'.join(block))
        elif line.startswith('SKIP   :'):
            skipped += 1
        elif line.startswith('Totals:'):
            has_totals = True
    return passed, failed, skipped, failures, has_totals


def asan_report(err):
    """Returns the error line, the first frames and the summary of a report of ASan in the output, or []"""
    try:
        lines = open(err, encoding='utf-8', errors='replace').read().splitlines()
    except OSError:
        return []
    report = []
    frames = 0
    inside = False
    for line in lines:
        if 'ERROR: AddressSanitizer' in line:
            inside = True
            report.append(line.strip())
        elif inside and re.match(r'^\s*#\d+ ', line) and frames < REPORT_FRAMES:
            frames += 1
            report.append(line.strip())
        elif line.startswith('SUMMARY: AddressSanitizer'):
            report.append(line.strip())
            break
        elif inside and frames and not line.strip():
            frames = REPORT_FRAMES  # only the stack of the access (the allocation / release stacks follow)
    return report


def remove_old_runs(base_dir):
    for old_run in os.listdir(base_dir):
        old_path = os.path.join(base_dir, old_run)
        try:
            if os.path.isdir(old_path) and time.time() - os.path.getmtime(old_path) > 86400:
                shutil.rmtree(old_path, ignore_errors=True)
        except OSError:
            pass


def main():
    # A failure text may hold characters the console code page cannot print: replace them
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, 'reconfigure'):
            stream.reconfigure(errors='replace')
    if os.name != 'nt':
        sys.exit('runtest_asan.py runs only on Windows (the ASan of MSVC); Linux: the ASan + UBSan tree of arch/checks.md')
    ap = argparse.ArgumentParser(description='Runs a QtTest executable of a Windows ASan tree split per function:tag.')
    ap.add_argument('exe')
    ap.add_argument('--tree', required=True)
    ap.add_argument('--temp', required=True)
    ap.add_argument('--bin', default='')
    ap.add_argument('--out', default='')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--filter', default='')
    ap.add_argument('--whole', action='store_true')
    ap.add_argument('--repeat', type=int, default=1)
    ap.add_argument('--timeout', type=int, default=3600)
    ap.add_argument('--full-stacks', action='store_true')
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('--arg', action='append', default=[])
    ap.add_argument('--qt-bin', default='')
    ap.add_argument('--asan-runtime', default='')
    ap.add_argument('--slowest', type=int, default=10)
    a = ap.parse_args()
    a.jobs = max(1, min(a.jobs, 24))
    a.repeat = max(1, a.repeat)

    tree = os.path.abspath(a.tree)
    cache = read_cache(tree)
    if cache.get('PDF4QT_ENABLE_MSVC_ASAN', '').upper() not in ('ON', 'TRUE', 'YES', '1'):
        sys.exit(f'{tree} is not configured with PDF4QT_ENABLE_MSVC_ASAN=ON')
    bin_dir = os.path.abspath(a.bin or os.path.join(tree, 'usr', 'bin'))
    name = a.exe[:-4] if a.exe.lower().endswith('.exe') else a.exe
    exe = os.path.join(bin_dir, name + '.exe')
    if not os.path.isfile(exe):
        sys.exit(f'{exe} not found (build the target in the tree first)')
    env = environment(a, cache, bin_dir)

    slots = None
    slot_dir = os.environ.get('PDF4QT_TEST_SLOT_DIR', '')
    if slot_dir:
        slots = (slot_dir, max(1, min(24, int(os.environ.get('PDF4QT_TEST_SLOTS', '12')))))
        a.jobs = min(a.jobs, slots[1])

    items = [None] if a.whole else [i for i in list_items(exe, env, bin_dir) if re.search(a.filter, i)]
    if not items:
        sys.exit('no items')

    base_dir = os.path.join(os.path.abspath(a.out or os.path.join(tree, 'asan_runs')), name)
    os.makedirs(base_dir, exist_ok=True)
    remove_old_runs(base_dir)
    run_dir = os.path.join(base_dir, time.strftime('%H%M%S') + '_' + str(os.getpid()))
    work = []
    for sweep in range(a.repeat):
        out_dir = os.path.join(run_dir, str(sweep))
        os.makedirs(out_dir, exist_ok=True)
        work.extend((sweep, item, out_dir) for item in items)

    print(f'{name}: {len(items)} items x {a.repeat} sweeps on {a.jobs} jobs, ASAN_OPTIONS={env["ASAN_OPTIONS"]}', flush=True)
    print(f'results: {run_dir}', flush=True)
    start = time.time()
    results = []
    pool = concurrent.futures.ThreadPoolExecutor(a.jobs)
    futures = {pool.submit(run_item, exe, item, out_dir, env, a, bin_dir, slots): sweep for sweep, item, out_dir in work}
    pending = set(futures)
    interrupted = False
    while pending:
        try:
            # A short wait keeps the main thread responsive to Ctrl+C
            done, pending = concurrent.futures.wait(pending, timeout=0.5)
        except KeyboardInterrupt:
            interrupted = True
            STOP.set()
            with RUNNING_LOCK:
                for job in RUNNING_JOBS:
                    kernel32().TerminateJobObject(job, 1)
            print('interrupted: the running test trees were killed, the queued items are not run', flush=True)
            continue
        for future in done:
            results.append((futures[future],) + future.result())
    pool.shutdown()
    wall = time.time() - start

    passed = failed = skipped = 0
    problems = []
    reports = []
    not_run = 0
    for sweep, selector, code, seconds, result, err in sorted(results, key=lambda r: (r[0], r[1])):
        if isinstance(code, str) and code.startswith('NOT RUN'):
            not_run += 1
            continue
        p, f, s, failures, has_totals = parse(result)
        passed += p
        failed += f
        skipped += s
        report = asan_report(err)
        if report or code == ASAN_EXIT_CODE:
            reports.append((sweep, selector, code, report, err))
        elif failures or code != 0 or not has_totals:
            problems.append((sweep, selector, code, failures, has_totals, result, err))

    print(f'wall {wall:.1f} s, sum of the processes {sum(r[3] for r in results):.1f} s')
    print(f'Totals: {passed} passed, {failed} failed, {skipped} skipped (without initTestCase / cleanupTestCase), {len(results)} processes')
    if interrupted:
        print(f'INTERRUPTED: {not_run} items not run')
    print(f'ASan reports: {len(reports)}')
    for sweep, selector, code, report, err in reports:
        print(f'ASAN sweep {sweep} {selector}: exit={code} -> {err}')
        for line in report:
            print('  ' + line[:400])
    for sweep, selector, code, failures, has_totals, result, err in problems:
        print(f'PROBLEM sweep {sweep} {selector}: exit={code}{"" if has_totals else " (no totals: crash or timeout)"} -> {result}, {err}')
        for failure in failures:
            print('  ' + failure.replace('\n', '\n  '))
    print('slowest:')
    for sweep, selector, code, seconds, result, err in sorted(results, key=lambda r: -r[3])[:a.slowest]:
        print(f'  {seconds:7.1f} s  {selector} (sweep {sweep})')
    sys.exit(1 if problems or reports or interrupted else 0)


if __name__ == '__main__':
    main()

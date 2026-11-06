# Checks

## Who runs what

- An agent builds on Windows warning-free and runs its new and changed tests, then ASan + UBSan on Linux once on its final code (owner 2026-10-24, 2026-11-04). When more agents work at once, the coordinator runs the Linux checks for all of them (owner 2026-11-05).
- Valgrind memcheck, TSan, helgrind / drd and the whole tool corpus run once per step by the coordinator (phase 2: at its end).
- After a larger change of a library (new classes, ownership, signals, destruction) build it with GCC too - GCC rejects code which MSVC accepts. A change is finished only when every test ends with exit 0 there too.

## Linux trees

Linux runs in WSL, distribution `Ubuntu_Default` (`wsl.exe -d Ubuntu_Default -e bash -c '...'`). All Linux builds and checks running at the same time use at most 24 jobs together (owner 2026-10-25); the scripts build by `ninja -j 24` and run under `nice -n 10`. Each tree has a lock `<tree>/.lock` taken by its scripts - never wrap them in another `flock` of that file (deadlock). The scripts copy the working copy by `rsync --checksum --no-times` into `<tree>/src` (never edit that copy): a file is copied only when its content differs and gets the current time, so it is always compiled again (`rsync -a` kept the older mtimes of Windows). `PDF4QT_VG_TREE` / `PDF4QT_ASAN_TREE=<dir>` select another tree, `PDF4QT_VG_NO_SYNC=1` / `PDF4QT_ASAN_NO_SYNC=1` keep its sources (a mutation check in a copy).

Linux draws the glyphs of a widget with the subpixel antialiasing of fontconfig, but grayscale into a `QImage`: compare a widget with its painter function on the same kind of device. Freed memory stays readable in a Release build on Windows, so lifetime bugs pass the tests there.

## ASan + UBSan

    ~/pdf4qt-ci/tools/asan-build.sh [ninja targets]
    python3 ~/pdf4qt-ci/tools/asan-run.py [--jobs N] [--filter REGEX] [--shard REGEX] [--repeat N] [--timeout S]

- Tree `~/pdf4qt-ci/pdf4qt-asan`: gcc-15, RelWithDebInfo `-O2 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -DNDEBUG`, Qt 6.11.3; only the code of PDF4QT is instrumented. `PDF4QT_ASAN_JOBS` for the build.
- The runner uses 12 jobs by default (at most 24) and splits the big GUI executables (App, Annotation, Fluent, WidgetStyle, FormFields, PageContentEditorTools) into one process per `function:tag`. About 25 times faster than valgrind (the whole set, about 3000 items, 9-10 min on 6 jobs).
- `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0:exitcode=113:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:allocator_may_return_null=1` (a huge allocation of a hostile-size test returns null as glibc does), `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1:exitcode=114`, `PDF4QT_TEST_TIMEOUT_FACTOR=3`, offscreen.
- Any report of the test or of a child process is a failure (exit 113 / 114, `SANITIZER` in the summary). Logs and `summary.txt` in `~/pdf4qt-ci/pdf4qt-asan/logs`; `asan-summary.py` groups the reports by their first frames in our code.
- Suppressions, narrow and each with its reason in the file: `~/pdf4qt-ci/tools/pdf4qt-lsan.supp` (fontconfig, libspeechd of the Qt speech plugin), `pdf4qt-ubsan.supp` (`vptr:*/QtCore/qpointer.h` - the static_cast inside `QPointer<T>::data()` of an object whose derived destructors have run). The vptr check stays on: it finds a member called on an object whose derived part is gone, e.g. a signal of `qApp` reaching a widget while `~QWidget` deletes its children - disconnect it in the destructor.

## Valgrind memcheck

    ~/pdf4qt-ci/tools/vg-build.sh [ninja targets]
    python3 ~/pdf4qt-ci/tools/vg-run.py [--jobs N] [--filter REGEX] [--shard REGEX] [--tool helgrind|drd] [--exclude REGEX]

- Tree `~/pdf4qt-ci/pdf4qt-vg`: gcc-15, RelWithDebInfo `-O1 -g` (memcheck reports false uninitialized values of `-O2` code), Qt 6.11.3 in `~/pdf4qt-ci/qt`, system valgrind 3.26, `PDF4QT_ENABLE_VALGRIND_ANNOTATIONS=ON` (no precompiled headers - the guard of missing includes). `PDF4QT_VG_JOBS` for the build.
- 12 jobs by default, at most 24. Valgrind runs the threads of a process one after another, so the executables matching `--shard` (default `FluentLifetime`) are split per `function:tag`; known slow tests start first. Definite leaks are errors (exit 99).
- `QT_ENABLE_REGEXP_JIT=0` (the PCRE2 JIT code gives false reports); suppressions `~/pdf4qt-ci/tools/pdf4qt.supp` (only narrow, justified leaks of third-party libraries). Logs in `~/pdf4qt-ci/pdf4qt-vg/logs`, `vg-summary.py`.
- A native run: `cd ~/pdf4qt-ci/pdf4qt-vg/build/usr/bin && LD_LIBRARY_PATH=~/pdf4qt-ci/qt/6.11.3/gcc_64/lib:$PWD/../lib QT_QPA_PLATFORM=offscreen ./<test>`.

## Data races: TSan, helgrind, drd

- `tsan-build.sh` / `tsan-run.py` work like the ASan scripts: tree `~/pdf4qt-ci/pdf4qt-tsan`, gcc-15 `-O1 -fsanitize=thread`, logs `<item>.tsan.<pid>`, suppressions `pdf4qt-tsan.supp`, `ignore_noninstrumented_modules=1` (the list inside `QThreadPool` is reported otherwise, also without PDF4QT; a race seen only through an interceptor called from inside Qt stays hidden). Qt is not instrumented: TSan sees `QMutex` / `QReadWriteLock` by the inline hooks of `qtsan_impl.h` in our code, `QRecursiveMutex` (compiled in libQt6Core) by `tools/tsan-qt-preload.cpp` (built by `tsan-build.sh`, `LD_PRELOAD` of `tsan-run.py`), the relock inside `QWaitCondition::wait` only by `PDFThreadAnnotations::wait`.
- `vg-run.py --tool helgrind|drd` uses the valgrind tree (logs `logs-<tool>`, suppressions `pdf4qt-<tool>.supp` generated with their reasons by `gen-thread-supp.py` - edit the generator; only frames of Qt / glib / libgcc / SQLite / blend2d, never a frame of PDF4QT). Shard everything; exclude the load tests and benchmarks (they run under TSan) and under drd the 17 MiB result of `UnitTestsMcp resourcesRead`.
- drd needs `--check-stack-var=yes` (otherwise it keeps the synchronization objects of a dead stack frame) and is 10-20 times slower than helgrind on the parallel loops of the core: run the heavy Core items under helgrind and TSan only.
- Why the annotations: the detectors do not see the synchronization inside Qt. The valgrind tree adds the client requests of `PDFThreadAnnotations` (helgrind requests, understood by drd; `__tsan_release` / `__tsan_acquire` under TSan; nothing in a normal build), QMutex / QReadWriteLock hooks in `Pdf4QtLibCore/threadannotations/QtCore/qtsan_impl.h` (valgrind does not see the futex of QMutex), wrappers of the hand-offs inside Qt in `pdfthreadannotations.cpp` (posted events, queued calls, QThreadPool, QtConcurrent, new blocks of `QArrayData::allocate*` - drd forgets at free() only the requested bytes, not the slop read by the SIMD code of Qt), `-Wl,--no-as-needed` and the hooks of the reference count of `std::shared_ptr` in `pdf4qtvalgrindprelude.h`, included before every C++ file.

## Coverage

A second tree with clang-cl (Visual Studio, `VC/Tools/Llvm/x64/bin`): `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DPDF4QT_ENABLE_COVERAGE=ON -DPDF4QT_BUILD_ONLY_CORE_LIBRARY=ON`, the existing `vcpkg_installed` prefix paths instead of the vcpkg toolchain. Run the tests with `LLVM_PROFILE_FILE=<dir>/%p-%m.profraw`, then `llvm-profdata merge -sparse *.profraw -o coverage.profdata` and `llvm-cov report <test.exe> -object usr/bin/Pdf4QtLibCore.dll -instr-profile coverage.profdata <sources>`. Coverage is measured, never forced ([conventions.md](conventions.md#tests)).

# Conventions explained

The rules are in [CLAUDE.md](../CLAUDE.md); this file gives their details and reasons.

## Files and code shape

- CRLF in every source and text file (`.gitattributes` enforces `eol=crlf`); `.desktop` files stay LF. Every file starts with the MIT header block `Copyright (c) 2018-2026 Jakub Melka and Contributors`. All English in code, documentation and Figma is American English; protocol names and license texts excepted.
- No anonymous namespaces and no free functions in a namespace: a file-local function is a static method of the class it serves or of a helper class defined in the .cpp (`<Class>Helper`, deleted constructor). Generic code goes to the generic helpers, not to a feature module: `PDFJsonHelper` ([pdfjsonhelper.h](../Pdf4QtLibCore/sources/pdfjsonhelper.h)), `PDFGeometryUtils` and `PDFEnumerationNames` ([pdfutils.h](../Pdf4QtLibCore/sources/pdfutils.h)), `PDFObjectUtils`, `PDFImageConversion`, `PDFFontCacheShrinkGuard` ([pdffont.h](../Pdf4QtLibCore/sources/pdffont.h)).
- No lambdas in Pdf4QtLibFluent, Pdf4QtLibApp and Pdf4QtLibMcp: a lambda has no name in the call stack, no breakpoint by name, and its captures hide the lifetime. A connection is `connect(sender, &Sender::signal, receiver, &Receiver::slot)`; the slot gets the sender by its arguments or a member (`sender()` only when the signal does not identify the object, in a private slot, by `qobject_cast` with a null check); a comparator or local helper is a static method. A lambda that remains has a comment why, captures nothing by reference that may die before it runs, and a connected one has a context object. Allowed exception: the two string-based `QMetaObject::invokeMethod` calls of the protected slots `commitData` / `closeEditor` of `QAbstractItemView`. Tests and the gallery may use lambdas.
- A nested class with default member initializers cannot be a default argument of a function declared inside its enclosing class (`const State& state = State()`, CWG 1397): GCC rejects it, MSVC does not. Use an overload without the parameter.
- MSVC builds with `/W4`; the only suppressions are `/wd5054`, `/wd4127`, `/wd4702`. `QT_NO_EMIT` is defined, so signals are emitted by `Q_EMIT`.
- A walk over the object graph of a document (outline, page tree, name and number trees, structure tree, fields, actions, forms of the resources) goes through one `PDFObjectWalk` ([pdfobjectutils.h](../Pdf4QtLibCore/sources/pdfobjectutils.h)): one visited set for the whole walk, a depth limit and a node budget (owner 2026-11-06). A set per path or per chain stops a cycle but repeats a shared node for each path - the outline of Remove Pages took 2^256 steps (review RCORE). Every new walk has a test with a cycle and with a node shared by two paths (`UnitTestsObjectWalk`).

## Painting

- The state of a `QPainter` is saved and restored only by a RAII guard: `PDFPainterStateGuard` ([pdfpainterutils.h](../Pdf4QtLibCore/sources/pdfpainterutils.h)) in Pdf4QtLibCore and above, `PDFStylePainterGuard` (accepts a null painter) in Pdf4QtLibFluent, which does not depend on Core. A part of a function with its own state gets a nested block.
- Pdf4QtLibFluent never reads a `QPalette` ([fluent-style.md](fluent-style.md)).

## Lifetime

Freed memory stays readable in a Release build on Windows, so these bugs pass the tests there; ASan and valgrind find them ([checks.md](checks.md)). Several use-after-free bugs of phase 2 were caught only by the sanitizers, so lifetime is written in the first draft (owner 2026-11-01):

- **Emit last.** State is changed before the signal; `Q_EMIT` and every call that may run code of the application (a callback, a menu, a dialog, a store with slots) is the last thing a function does. When the code must go on, it holds `this` and the objects it uses in a `QPointer` and checks them afterwards.
- **Ownership first.** Before a new group of objects is written, who owns each of them and in which order they die is decided. A raw pointer to something another object can delete is a `QPointer`.
- **A walk before the report** over every new `Q_EMIT` and call of foreign code, reading what runs after it.
- **A finding of a sweep is fixed in the code**; an exclusion in a lifetime test is the last resort, with its reason at its place.
- A slot of `QObject::destroyed` never calls a member of the sender (the derived destructor has run, but a `QPointer` to it is cleared only in `~QObject`): it only forgets the pointer. A watched QWidget subclass compared while it may be in its destructor is held in `PDFWatchedPointer` ([pdfwatchedpointer.h](../Pdf4QtLibFluent/sources/pdfwatchedpointer.h)) - `QPointer<T>::data()` would cast the dying object to `T` (UBSan vptr). An object which others watch says it is going away from its own destructor while it is still whole.
- A slot never deletes its sender synchronously (`deleteLater`). Every connection has a context object, so it is cut when the receiver dies.
- A popup run by `exec()` is never an object on the stack whose parent the application can delete: `new` + `QPointer`, as `PDFMessageBox::showMessage`.
- A widget with nested objects, popups or watched objects has a test which deletes them in every order (parent first, child first, from its own signal, with a popup open). Every widget of Pdf4QtLibFluent has a live sample on a gallery page: `UnitTestsFluentLifetime` destroys every widget of every page in every order, so a new widget is covered by its sample. A child the application must not delete (a part of a Qt widget) is excluded by its catalog; what an application does with a widget that the generic input cannot (a menu, a running timer) is a lifetime hint of its sample; a library class with a meta object and no sample fails the test.

## Threads

- A new hand-off of data between threads which synchronizes by something the race detectors do not see (the queue of a `QThreadPool` of our own `QRunnable`, a queued invocation or signal to another thread, an atomic flag, a reference count, a function-local static) gets a pair of [pdfthreadannotations.h](../Pdf4QtLibCore/sources/pdfthreadannotations.h): `happensBefore(tag)` in the sending thread after the data are complete, `happensAfter(tag)` in the receiving thread before it reads them. One stable address per hand-off (the task, the call, the result; our `QRunnable` uses `this`); a callable in a pool is started by `PDFExecutionPolicy::startTask`; a queued invocation of another thread is wrapped in `TransportScope` (TSan does not see the queue of Qt between the copies of the arguments and the slot). A wait on a `QWaitCondition` with a `QMutex` locked by our code is `PDFThreadAnnotations::wait`, never `QWaitCondition::wait`: the wait unlocks and relocks the mutex inside Qt, TSan would believe the waiting thread holds it (a "double lock of a mutex" at the next lock of another thread, then data races of everything it guards).
- Threads are fanned out only by `PDFExecutionPolicy`, never spawned ad hoc.
- No `Qt::DirectConnection` (and no connection to a functor without a context object) on a signal that can be emitted in another thread (owner 2026-11-06). `PDFProgress::step` emits in the thread which steps - a thread of a parallel loop -, so a direct receiver runs in several threads at once in code written for one thread: two threads assigned the same `QString` of the progress of the tools and corrupted the heap. A queued connection does not help either when the receiver lives in a pool thread without an event loop. A computation therefore reports through an interface it calls, `PDFProgressSink` ([pdfprogress.h](../Pdf4QtLibCore/sources/pdfprogress.h); any thread at once, never blocks, never runs foreign code): `PDFProgressCounter` is atomics only (an immutable table of the phases, the current phase, the done steps, a monotonic percentage), and the GUI or MCP reads `snapshot()` by a `QTimer` in its own thread and builds the texts there (`PDFDocumentToolRunner`, `PDFAppDocumentLoader`, the progress of an MCP call). The signals of `PDFProgress` stay for the old applications, whose receivers live in the GUI thread (automatic = queued connection). A direct connection, which stays, has a comment why: the same thread (`PDFAppSessionEnd`, the editor of `PDFComponentDelegates`) or a collector under a lock in a command without an event loop (`PdfTool` render errors).
- A `QPainterPath` and its copies are never used by more threads at once: its const functions write into its data (bounding rectangles, paint engine data) and before Qt 6.10 the data are shared by copies. Data shared by threads are plain immutable data (glyphs as `PDFGlyphOutline`, from which each processor creates its own paths).

## Texts

- User-visible strings are translatable (`tr()` / `QApplication::translate`, `%Ln` for counts). `.ts` files are generated ([build.md](build.md)), never edited by hand.
- The texts of the parameters of a tool are `QT_TR_NOOP` in a member function with the `tr` of the class (`Q_DECLARE_TR_FUNCTIONS`): `Definition::createBool(tr, name, QT_TR_NOOP("Title"), QT_TR_NOOP("Description"), true)`, options `{ id, QT_TR_NOOP("Title"), QT_TR_NOOP("Description"), icon }`. The JSON schema of MCP is the English source in every language - only the assistant reads it (owner 2026-11-02); the panels translate when they read it. A translated `QString` does not compile (`PDFDocumentToolText`). A shared text is a `static constexpr const char*` with `QT_TRANSLATE_NOOP("pdf::<ToolClass>", ...)`, no text is `nullptr`; read by `getTitle()` / `getDescription()`.
- Dates, times, sizes and numbers of the new application are formatted by `PDFAppLocale` (owner O17, [layering.md](layering.md)); a test simulates a system locale by `UnitTests/applocaleguard.h`.

## Tests

A test checks behavior, not coverage (owner 2026-10-29):

- Every test asserts an observable result of the behavior it names: returned values, the state of the model, the written document or database rows, emitted signals with their arguments, the pixels of a drawn widget. A test which only runs code ("no crash", a line executed for coverage, `QVERIFY(true)`, a result compared with itself or with a value copied from the implementation) is not a test.
- Expected values come from the specification (Figma, the PDF specification, the design documents), not from a run of the code.
- A new test is revert-checked: break the code it guards (remove the fix, invert the condition, skip the call) and the test must fail; the report names the mutation.
- Uncovered code is never covered by force: an unreachable defensive branch stays uncovered and is named; a gap is closed only by a test of a real case.
- No assertion is weakened to make a test pass; time limits scale by `PDF4QT_TEST_TIMEOUT_FACTOR`.

## Elsewhere

- [SECURITY_TODO.md](../SECURITY_TODO.md) collects security weaknesses found outside the scope of the current work (mostly Core, also the old applications); they are solved together in phase 3 of the new application, after phase 2 (owner 2026-10-29), not fixed ad hoc.
- [NOTES.txt](../NOTES.txt) tracks, section by section, where the implementation deliberately deviates from PDF 2.0 or does not implement it yet - check it before assuming a gap is a bug.
- The new application stores everything in its SQLite database, never in QSettings; only Pdf4QtLibApp links SQLite. Only Pdf4QtOcrTesseract includes Tesseract / Leptonica headers. Out of memory is not handled (owner 2026-11-05).

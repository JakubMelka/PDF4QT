// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfthreadannotations.h"

#if defined(PDF4QT_VALGRIND_ANNOTATIONS) && defined(__linux__) && !defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)

// Hand-offs of data between threads inside of the Qt libraries for valgrind helgrind / drd
// (a build configured with PDF4QT_ENABLE_VALGRIND_ANNOTATIONS only).
//
// The queues of Qt (posted events - queued signals and invocations, QFutureWatcher
// notifications -, the queue of QThreadPool, the state of QFutureInterface) are guarded
// by the futex based QMutex compiled in the Qt libraries, which valgrind does not see.
// Valgrind replaces the functions named below by these wrappers in the whole process
// (function wrapping of the valgrind core: a wrapper is found by its encoded name in any
// loaded object), each wrapper adds the happens-before relation of its hand-off and calls
// the original function:
//  - QCoreApplication::postEvent / notifyInternal2: posting an event happens before its
//    delivery (the arguments of a queued call are written into the event by the sender),
//  - QThreadPool::start / tryStart / startOnReservedThread and QRunnable::QGenericRunnable::run:
//    starting a task happens before the task (QThreadPool::start of a callable),
//  - QFutureInterfaceBase::reportStarted / isCanceled: QtConcurrent::run reports the start
//    of its task (the stored function and arguments are complete) and the task asks the same
//    future interface for its cancellation at its start, before it calls the function,
//  - QArrayData::allocate / allocate1 / allocate2: the deallocation of a storage happens
//    before its next allocation ([new.delete.dataraces]), so a new block of QString /
//    QByteArray / QList has no accesses of other threads before it. helgrind and
//    ThreadSanitizer forget the state of new memory themselves; drd forgets only the
//    requested bytes of a block at free(), not the bytes behind them in the same chunk of the
//    allocator, which the SIMD code of Qt reads by aligned 16-byte loads (qHash of a short
//    string - aeshash128_lt16: "this may produce valgrind warnings, but it's safe" -,
//    qustrlen). A block of another thread allocated later in that chunk was reported as a
//    conflicting store against the thread, which had read and freed the old block (drd,
//    UnitTestsMcp listenStreams: a QString of a task id hashed by PDFMcpSubscriptionFilter::parse
//    in the control thread of the MCP server, the QByteArray of the response in the test
//    thread). The wrapper forgets the history of the whole new block (ANNOTATE_NEW_MEMORY -
//    the clean memory request of helgrind, which drd understands too), so the later accesses
//    of the block are checked against its own life only; the header written inside of the
//    allocation function itself before the wrapper gets the block is covered by the entry
//    qarraydata-new-header of pdf4qt-drd.supp (gen-thread-supp.py, top frame QArrayData::allocate*).
//    QArrayData::reallocateUnaligned keeps the contents of its block, so it is not wrapped.
// The wrappers must have the encoded names of valgrind and C linkage, so they are free
// functions (an exception of the conventions of the project, nothing calls them).

#include <malloc.h>

#include <valgrind/valgrind.h>
#include <valgrind/helgrind.h>

class QObject;
class QEvent;
class QRunnable;

#define PDF4QT_VALGRIND_WRAPPER __attribute__((visibility("default"))) __attribute__((used))

extern "C" {

PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN16QCoreApplication9postEventEP7QObjectP6QEventi)(QObject* receiver, QEvent* event, int priority);
PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN16QCoreApplication9postEventEP7QObjectP6QEventi)(QObject* receiver, QEvent* event, int priority)
{
    OrigFn function;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_BEFORE(event);
    CALL_FN_v_WWW(function, receiver, event, priority);
}

PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN16QCoreApplication15notifyInternal2EP7QObjectP6QEvent)(QObject* receiver, QEvent* event);
PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN16QCoreApplication15notifyInternal2EP7QObjectP6QEvent)(QObject* receiver, QEvent* event)
{
    OrigFn function;
    unsigned long result = 0;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_AFTER(event);
    ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(event);
    CALL_FN_W_WW(result, function, receiver, event);
    return (result & 0xFF) != 0;
}

PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool5startEP9QRunnablei)(void* pool, QRunnable* runnable, int priority);
PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool5startEP9QRunnablei)(void* pool, QRunnable* runnable, int priority)
{
    OrigFn function;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_BEFORE(runnable);
    CALL_FN_v_WWW(function, pool, runnable, priority);
}

PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool8tryStartEP9QRunnable)(void* pool, QRunnable* runnable);
PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool8tryStartEP9QRunnable)(void* pool, QRunnable* runnable)
{
    OrigFn function;
    unsigned long result = 0;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_BEFORE(runnable);
    CALL_FN_W_WW(result, function, pool, runnable);
    return (result & 0xFF) != 0;
}

PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool21startOnReservedThreadEP9QRunnable)(void* pool, QRunnable* runnable);
PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN11QThreadPool21startOnReservedThreadEP9QRunnable)(void* pool, QRunnable* runnable)
{
    OrigFn function;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_BEFORE(runnable);
    CALL_FN_v_WW(function, pool, runnable);
}

PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN9QRunnable16QGenericRunnable3runEv)(QRunnable* runnable);
PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN9QRunnable16QGenericRunnable3runEv)(QRunnable* runnable)
{
    OrigFn function;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_AFTER(runnable);
    ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(runnable);
    CALL_FN_v_W(function, runnable);
}

PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN20QFutureInterfaceBase13reportStartedEv)(void* futureInterface);
PDF4QT_VALGRIND_WRAPPER void I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN20QFutureInterfaceBase13reportStartedEv)(void* futureInterface)
{
    OrigFn function;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_BEFORE(futureInterface);
    CALL_FN_v_W(function, futureInterface);
}

PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZNK20QFutureInterfaceBase10isCanceledEv)(const void* futureInterface);
PDF4QT_VALGRIND_WRAPPER bool I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZNK20QFutureInterfaceBase10isCanceledEv)(const void* futureInterface)
{
    OrigFn function;
    unsigned long result = 0;
    VALGRIND_GET_ORIG_FN(function);
    ANNOTATE_HAPPENS_AFTER(futureInterface);
    CALL_FN_W_W(result, function, futureInterface);
    return (result & 0xFF) != 0;
}

// The whole new block (malloc_usable_size of valgrind is the requested size); no block
// (capacity 0, the shared null) - nothing to forget
#define PDF4QT_FORGET_NEW_ARRAY_DATA(header) \
    if (header) \
    { \
        ANNOTATE_NEW_MEMORY((header), malloc_usable_size(header)); \
    }

PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData8allocateEPPS_xxxNS_16AllocationOptionE)(void** header, long long objectSize, long long alignment, long long capacity, int option);
PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData8allocateEPPS_xxxNS_16AllocationOptionE)(void** header, long long objectSize, long long alignment, long long capacity, int option)
{
    OrigFn function;
    void* result = nullptr;
    VALGRIND_GET_ORIG_FN(function);
    CALL_FN_W_5W(result, function, header, objectSize, alignment, capacity, option);
    PDF4QT_FORGET_NEW_ARRAY_DATA(header ? *header : nullptr);
    return result;
}

PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData9allocate1EPPS_xNS_16AllocationOptionE)(void** header, long long capacity, int option);
PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData9allocate1EPPS_xNS_16AllocationOptionE)(void** header, long long capacity, int option)
{
    OrigFn function;
    void* result = nullptr;
    VALGRIND_GET_ORIG_FN(function);
    CALL_FN_W_WWW(result, function, header, capacity, option);
    PDF4QT_FORGET_NEW_ARRAY_DATA(header ? *header : nullptr);
    return result;
}

PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData9allocate2EPPS_xNS_16AllocationOptionE)(void** header, long long capacity, int option);
PDF4QT_VALGRIND_WRAPPER void* I_WRAP_SONAME_FNNAME_ZU(libQt6CoreZdsoZa, _ZN10QArrayData9allocate2EPPS_xNS_16AllocationOptionE)(void** header, long long capacity, int option)
{
    OrigFn function;
    void* result = nullptr;
    VALGRIND_GET_ORIG_FN(function);
    CALL_FN_W_WWW(result, function, header, capacity, option);
    PDF4QT_FORGET_NEW_ARRAY_DATA(header ? *header : nullptr);
    return result;
}

#undef PDF4QT_FORGET_NEW_ARRAY_DATA

}   // extern "C"

#endif

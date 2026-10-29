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

// Replacement of <QtCore/qtsan_impl.h> of Qt used only by a build configured with
// PDF4QT_ENABLE_VALGRIND_ANNOTATIONS (its directory is put before the directories
// of Qt). QMutex, QRecursiveMutex and QReadWriteLock of Qt lock by a futex on Linux,
// which valgrind helgrind / drd do not see, so every datum guarded by a QMutex looked
// like a data race. Their inline functions (qmutex.h, qreadwritelock.h) call the hooks
// of QtTsan around each lock and unlock - Qt fills them only for ThreadSanitizer.
// This header fills them with the happens-before client requests of helgrind (drd
// understands them too): an unlock happens before the next lock of the same mutex.
// Only the locks compiled in the code of PDF4QT are seen this way, not the locks
// inside of the Qt libraries (their hand-offs are annotated by PDFThreadAnnotations).

#if defined(__SANITIZE_THREAD__)
#define PDF4QT_QTSAN_IMPL_USE_QT
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define PDF4QT_QTSAN_IMPL_USE_QT
#endif
#endif

#if defined(PDF4QT_QTSAN_IMPL_USE_QT)
// ThreadSanitizer: the original header of Qt does the work
#include_next <QtCore/qtsan_impl.h>
#elif !defined(QTSAN_IMPL_H)
#define QTSAN_IMPL_H

#include <QtCore/qglobal.h>

#include <valgrind/helgrind.h>

QT_BEGIN_NAMESPACE

namespace QtTsan {

// Not ThreadSanitizer - the code of Qt depending on it keeps the paths of a normal build
constexpr inline bool IsEnabled = false;

inline void futexAcquire(void *addr, void *addr2 = nullptr)
{
    ANNOTATE_HAPPENS_AFTER(addr);
    if (addr2)
        ANNOTATE_HAPPENS_AFTER(addr2);
}

inline void futexRelease(void *addr, void *addr2 = nullptr)
{
    if (addr2)
        ANNOTATE_HAPPENS_BEFORE(addr2);
    ANNOTATE_HAPPENS_BEFORE(addr);
}

inline void latchWait(const void *addr)
{
    ANNOTATE_HAPPENS_AFTER(addr);
}

inline void latchCountDown(void *addr)
{
    ANNOTATE_HAPPENS_BEFORE(addr);
}

enum : unsigned {
    MutexWriteReentrant = 0x01,
    TryLock = 0x02,
    TryLockFailed = 0x04,
    ReadLock = 0x08,
};

inline void mutexPreLock(void *, unsigned) {}

inline void mutexPostLock(void *addr, unsigned flags, int)
{
    if (!(flags & TryLockFailed))
        ANNOTATE_HAPPENS_AFTER(addr);
}

inline void mutexPreUnlock(void *addr, unsigned)
{
    ANNOTATE_HAPPENS_BEFORE(addr);
}

inline void mutexPostUnlock(void *, unsigned) {}

} // namespace QtTsan

QT_END_NAMESPACE

#endif

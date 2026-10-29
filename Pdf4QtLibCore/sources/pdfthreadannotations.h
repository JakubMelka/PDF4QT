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

#ifndef PDFTHREADANNOTATIONS_H
#define PDFTHREADANNOTATIONS_H

// Annotations of the hand-offs of data between threads for the race detectors.
//
// The detectors do not see every synchronization: valgrind helgrind / drd see only
// the functions of pthread, never an atomic operation or a futex, so the reference
// counts, QMutex, QSemaphore and the queues of QThreadPool of Qt on Linux are
// invisible to them; ThreadSanitizer sees the atomics of our code, but not the
// synchronization inside of the Qt libraries, which are not instrumented. A pair
// happensBefore(address) / happensAfter(address) tells them, that everything a
// thread did before happensBefore happens before everything another thread does
// after a following happensAfter with the same address.
//
// The functions compile to nothing in a normal build. They are active under
// ThreadSanitizer (__tsan_release / __tsan_acquire) and in a build configured with
// PDF4QT_ENABLE_VALGRIND_ANNOTATIONS (the client requests of helgrind, which drd
// understands too - drd.h assigns the same request codes to its happens-before
// annotations; outside of valgrind a client request is a few instructions).

#if defined(__SANITIZE_THREAD__)
#define PDF4QT_THREAD_ANNOTATIONS_TSAN
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define PDF4QT_THREAD_ANNOTATIONS_TSAN
#endif
#endif

#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
#include <sanitizer/tsan_interface.h>

// Dynamic annotations of the runtime of ThreadSanitizer (no header declares them)
extern "C" void AnnotateIgnoreWritesBegin(const char* file, int line);
extern "C" void AnnotateIgnoreWritesEnd(const char* file, int line);
#elif defined(PDF4QT_VALGRIND_ANNOTATIONS)
#include <valgrind/helgrind.h>

#include <atomic>
#endif

namespace pdf
{

/// Hand-offs of data between threads for the race detectors (ThreadSanitizer,
/// valgrind helgrind and drd). Use one stable address per hand-off (the task, the
/// message, the result) and keep each call next to the place, where the data are
/// passed by a synchronization the detectors do not see (a queue of a thread pool,
/// a queued invocation, an atomic flag, a reference count).
class PDFThreadAnnotations
{
public:
    PDFThreadAnnotations() = delete;

    /// Returns true, when the annotations are compiled in
    static constexpr bool isEnabled()
    {
#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN) || defined(PDF4QT_VALGRIND_ANNOTATIONS)
        return true;
#else
        return false;
#endif
    }

    /// The memory accesses of this thread before this call happen before the memory
    /// accesses of a thread after its later call of happensAfter(address)
    /// \param address Address identifying the hand-off (nullptr - nothing is done:
    ///        drd asserts on a happens-before object at the address 0)
    static inline void happensBefore(const void* address) noexcept
    {
#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
        if (address)
        {
            __tsan_release(const_cast<void*>(address));
        }
#elif defined(PDF4QT_VALGRIND_ANNOTATIONS)
        if (address)
        {
            ANNOTATE_HAPPENS_BEFORE(address);
        }
#else
        (void)address;
#endif
    }

    /// The memory accesses of this thread after this call happen after the memory
    /// accesses of all threads before their earlier calls of happensBefore(address)
    /// \param address Address identifying the hand-off (nullptr - nothing is done)
    static inline void happensAfter(const void* address) noexcept
    {
#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
        if (address)
        {
            __tsan_acquire(const_cast<void*>(address));
        }
#elif defined(PDF4QT_VALGRIND_ANNOTATIONS)
        if (address)
        {
            ANNOTATE_HAPPENS_AFTER(address);
        }
#else
        (void)address;
#endif
    }

    /// Forgets the hand-offs of the address (helgrind keeps a synchronization object
    /// for each address forever otherwise). Call it, when the object identifying the
    /// hand-off is destroyed and its address may be used by an unrelated object.
    /// \param address Address identifying the hand-off (nullptr - nothing is done)
    static inline void forget(const void* address) noexcept
    {
#if defined(PDF4QT_VALGRIND_ANNOTATIONS) && !defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
        if (address)
        {
            ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(address);
        }
#else
        (void)address;
#endif
    }

    /// Scope of a queued invocation of another thread (QMetaObject::invokeMethod with
    /// Qt::QueuedConnection, a queued signal, QtConcurrent::run): the templates of Qt
    /// copy the arguments into the posted event in the scope and the other thread reads
    /// them before the slot - the queue of Qt between is invisible. ThreadSanitizer does
    /// not record the memory accesses of the scope (the copies of the arguments); the
    /// data the arguments refer to are handed off by happensBefore before the scope and
    /// happensAfter in the slot. Valgrind sees the posted events of Qt themselves
    /// (pdfthreadannotations.cpp), the scope does nothing there.
    class TransportScope
    {
    public:
        inline TransportScope() noexcept
        {
#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
            AnnotateIgnoreWritesBegin(__FILE__, __LINE__);
#endif
        }

        inline ~TransportScope()
        {
#if defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
            AnnotateIgnoreWritesEnd(__FILE__, __LINE__);
#endif
        }

        TransportScope(const TransportScope&) = delete;
        TransportScope& operator=(const TransportScope&) = delete;
    };

    /// Release of a reference of a shared object by an atomic reference count (call
    /// it before the decrement); all accesses of the owners to the object happen
    /// before its destruction by the last owner, see releasedLastReference. Only
    /// valgrind needs it, ThreadSanitizer understands the atomic operations. The sole
    /// owner (the count is 1, nobody else can release a reference at the same time)
    /// hands nothing to anybody: no client request - drd creates a new segment for
    /// each happens-before annotation, the temporary objects would slow it down.
    /// \param counter Reference count
    template<typename Counter>
    static inline void releasingReference(const Counter& counter) noexcept
    {
#if defined(PDF4QT_VALGRIND_ANNOTATIONS) && !defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
        if (counter.load(std::memory_order_relaxed) != 1)
        {
            ANNOTATE_HAPPENS_BEFORE(&counter);
        }
#else
        (void)counter;
#endif
    }

    /// The last reference of a shared object was released by this thread (call it
    /// after the decrement, before the object is destroyed)
    /// \param counter Address of the reference count
    static inline void releasedLastReference(const void* counter) noexcept
    {
#if defined(PDF4QT_VALGRIND_ANNOTATIONS) && !defined(PDF4QT_THREAD_ANNOTATIONS_TSAN)
        ANNOTATE_HAPPENS_AFTER(counter);
        ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(counter);
#else
        (void)counter;
#endif
    }
};

}   // namespace pdf

#endif // PDFTHREADANNOTATIONS_H

/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef WTF_WordLock_h
#define WTF_WordLock_h

#include <wtf/Atomics.h>
#include <wtf/Compiler.h>
#include <wtf/Locker.h>
#include <wtf/Noncopyable.h>

namespace WTF {

// A WordLock is a fully adaptive mutex that uses sizeof(void*) storage. It has a fast path that is
// similar to SpinLock, and a slow path that is similar to Mutex. In most cases, you should use Lock
// instead. WordLock sits lower in the stack and is used to implement Lock, so Lock is the main
// client of WordLock.

class WordLock {
    WTF_MAKE_NONCOPYABLE(WordLock);
public:
    WordLock()
    {
        m_word.store(0, std::memory_order_relaxed);
    }

    void lock()
    {
        if (LIKELY(m_word.compareExchangeWeak(0, isLockedBit, std::memory_order_acquire))) {
            // WordLock acquired!
            return;
        }

        lockSlow();
    }

    void unlock()
    {
        if (LIKELY(m_word.compareExchangeWeak(isLockedBit, 0, std::memory_order_release))) {
            // WordLock released, and nobody was waiting!
            return;
        }

        unlockSlow();
    }

    bool isHeld() const
    {
        return m_word.load(std::memory_order_acquire) & isLockedBit;
    }

    bool isLocked() const
    {
        return isHeld();
    }

private:
    static const uintptr_t isLockedBit = 1;
    static const uintptr_t isQueueLockedBit = 2;
    static const uintptr_t queueHeadMask = 3;

    WTF_EXPORT_PRIVATE void lockSlow();
    WTF_EXPORT_PRIVATE void unlockSlow();

    Atomic<uintptr_t> m_word;
};

typedef Locker<WordLock> WordLockHolder;

} // namespace WTF

using WTF::WordLock;
using WTF::WordLockHolder;

#endif // WTF_WordLock_h


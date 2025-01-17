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

#ifndef WTF_Lock_h
#define WTF_Lock_h

#include <wtf/Atomics.h>
#include <wtf/Compiler.h>
#include <wtf/Locker.h>
#include <wtf/Noncopyable.h>

namespace WTF {

// This is a fully adaptive mutex that only requires 1 byte of storage. It has fast paths that are
// competetive to SpinLock (uncontended locking is inlined and is just a CAS, microcontention is
// handled by spinning and yielding), and a slow path that is competetive to Mutex (if a lock cannot
// be acquired in a short period of time, the thread is put to sleep until the lock is available
// again). It uses less memory than either SpinLock or Mutex.

// This is a struct without a constructor or destructor so that it can be statically initialized.
// Use Lock in instance variables.
struct LockBase {
    void lock()
    {
        if (LIKELY(m_byte.compareExchangeWeak(0, isHeldBit, std::memory_order_acquire))) {
            // Lock acquired!
            return;
        }

        lockSlow();
    }

    void unlock()
    {
        if (LIKELY(m_byte.compareExchangeWeak(isHeldBit, 0, std::memory_order_release))) {
            // Lock released and nobody was waiting!
            return;
        }

        unlockSlow();
    }

    bool isHeld() const
    {
        return m_byte.load(std::memory_order_acquire) & isHeldBit;
    }

    bool isLocked() const
    {
        return isHeld();
    }

protected:
    static const uint8_t isHeldBit = 1;
    static const uint8_t hasParkedBit = 2;

    WTF_EXPORT_PRIVATE void lockSlow();
    WTF_EXPORT_PRIVATE void unlockSlow();

    Atomic<uint8_t> m_byte;
};

class Lock : public LockBase {
    WTF_MAKE_NONCOPYABLE(Lock);
public:
    Lock()
    {
        m_byte.store(0, std::memory_order_relaxed);
    }
};

typedef LockBase StaticLock;
typedef Locker<LockBase> LockHolder;

} // namespace WTF

using WTF::StaticLock;
using WTF::Lock;
using WTF::LockHolder;

#endif // WTF_Lock_h


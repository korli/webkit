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

#include "config.h"
#include "ParkingLot.h"

#include "DataLog.h"
#include "HashFunctions.h"
#include "StringPrintStream.h"
#include "ThreadSpecific.h"
#include "ThreadingPrimitives.h"
#include "Vector.h"
#include "WordLock.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace WTF {

namespace {

const bool verbose = false;

struct ThreadData {
    WTF_MAKE_FAST_ALLOCATED;
public:
    
    ThreadData();
    ~ThreadData();

    ThreadIdentifier threadIdentifier;
    
    bool shouldPark { false };
    std::mutex parkingLock;
    std::condition_variable parkingCondition;

    const void* address;
    ThreadData* nextInQueue { nullptr };
};

enum class DequeueResult {
    Ignore,
    RemoveAndContinue,
    RemoveAndStop
};

struct Bucket {
    WTF_MAKE_FAST_ALLOCATED;
public:
    void enqueue(ThreadData* data)
    {
        if (verbose)
            dataLog(toString(currentThread(), ": enqueueing ", RawPointer(data), " with address = ", RawPointer(data->address), " onto ", RawPointer(this), "\n"));
        ASSERT(data->address);
        ASSERT(!data->nextInQueue);
        
        if (queueTail) {
            queueTail->nextInQueue = data;
            queueTail = data;
            return;
        }

        queueHead = data;
        queueTail = data;
    }

    template<typename Functor>
    void genericDequeue(const Functor& functor)
    {
        if (verbose)
            dataLog(toString(currentThread(), ": dequeueing from bucket at ", RawPointer(this), "\n"));
        
        if (!queueHead) {
            if (verbose)
                dataLog(toString(currentThread(), ": empty.\n"));
            return;
        }

        // This loop is a very clever abomination. The induction variables are the pointer to the
        // pointer to the current node, and the pointer to the previous node. This gives us everything
        // we need to both proceed forward to the next node, and to remove nodes while maintaining the
        // queueHead/queueTail and all of the nextInQueue links. For example, when we are at the head
        // element, then removal means rewiring queueHead, and if it was also equal to queueTail, then
        // we'd want queueTail to be set to nullptr. This works because:
        //
        //     currentPtr == &queueHead
        //     previous == nullptr
        //
        // We remove by setting *currentPtr = (*currentPtr)->nextInQueue, i.e. changing the pointer
        // that used to point to this node to instead point to this node's successor. Another example:
        // if we were at the second node in the queue, then we'd have:
        //
        //     currentPtr == &queueHead->nextInQueue
        //     previous == queueHead
        //
        // If this node is not equal to queueTail, then removing it simply means making
        // queueHead->nextInQueue point to queueHead->nextInQueue->nextInQueue (which the algorithm
        // achieves by mutating *currentPtr). If this node is equal to queueTail, then we want to set
        // queueTail to previous, which in this case is queueHead - thus making the queue look like a
        // proper one-element queue with queueHead == queueTail.
        bool shouldContinue = true;
        ThreadData** currentPtr = &queueHead;
        ThreadData* previous = nullptr;
        while (shouldContinue) {
            ThreadData* current = *currentPtr;
            if (verbose)
                dataLog(toString(currentThread(), ": got thread ", RawPointer(current), "\n"));
            if (!current)
                break;
            DequeueResult result = functor(current);
            switch (result) {
            case DequeueResult::Ignore:
                if (verbose)
                    dataLog(toString(currentThread(), ": currentPtr = ", RawPointer(currentPtr), ", *currentPtr = ", RawPointer(*currentPtr), "\n"));
                previous = current;
                currentPtr = &(*currentPtr)->nextInQueue;
                break;
            case DequeueResult::RemoveAndContinue:
            case DequeueResult::RemoveAndStop:
                if (verbose)
                    dataLog(toString(currentThread(), ": dequeueing ", RawPointer(current), " from ", RawPointer(this), "\n"));
                if (current == queueTail)
                    queueTail = previous;
                *currentPtr = current->nextInQueue;
                current->nextInQueue = nullptr;
                if (result == DequeueResult::RemoveAndStop)
                    shouldContinue = false;
                break;
            }
        }

        ASSERT(!!queueHead == !!queueTail);
    }
    
    ThreadData* dequeue()
    {
        ThreadData* result = nullptr;
        genericDequeue(
            [&] (ThreadData* element) -> DequeueResult {
                result = element;
                return DequeueResult::RemoveAndStop;
            });
        return result;
    }

    ThreadData* queueHead { nullptr };
    ThreadData* queueTail { nullptr };

    // This lock protects the entire bucket. Thou shall not make changes to Bucket without holding
    // this lock.
    WordLock lock;

    // Put some distane between buckets in memory. This is one of several mitigations against false
    // sharing.
    char padding[64];
};

struct Hashtable {
    unsigned size;
    Atomic<Bucket*> data[1];

    static Hashtable* create(unsigned size)
    {
        ASSERT(size >= 1);
        
        Hashtable* result = static_cast<Hashtable*>(
            fastZeroedMalloc(sizeof(Hashtable) + sizeof(Atomic<Bucket*>) * (size - 1)));
        result->size = size;
        return result;
    }

    static void destroy(Hashtable* hashtable)
    {
        fastFree(hashtable);
    }
};

ThreadSpecific<ThreadData>* threadData;
Atomic<Hashtable*> hashtable;
Atomic<unsigned> numThreads;

// With 64 bytes of padding per bucket, assuming a hashtable is fully populated with buckets, the
// memory usage per thread will still be less than 1KB.
const unsigned maxLoadFactor = 3;

const unsigned growthFactor = 2;

unsigned hashAddress(const void* address)
{
    return WTF::PtrHash<const void*>::hash(address);
}

// Locks the hashtable. This reloops in case of rehashing, so the current hashtable may be different
// after this returns than when you called it. Guarantees that there is a hashtable. This is pretty
// slow and not scalable, so it's only used during thread creation and for debugging/testing.
Vector<Bucket*> lockHashtable()
{
    for (;;) {
        Hashtable* currentHashtable;

        currentHashtable = hashtable.load();
        if (!currentHashtable) {
            // Try to be the first to create the hashtable.
            currentHashtable = Hashtable::create(maxLoadFactor);
            if (!hashtable.compareExchangeWeak(nullptr, currentHashtable)) {
                Hashtable::destroy(currentHashtable);
                continue;
            }
            if (verbose)
                dataLog(toString(currentThread(), ": created initial hashtable ", RawPointer(currentHashtable), "\n"));
        }

        // Now find all of the buckets. This makes sure that the hashtable is full of buckets so that
        // we can lock all of the buckets, not just the ones that are materialized.
        Vector<Bucket*> buckets;
        for (unsigned i = currentHashtable->size; i--;) {
            Atomic<Bucket*>& bucketPointer = currentHashtable->data[i];

            for (;;) {
                Bucket* bucket = bucketPointer.load();

                if (!bucket) {
                    bucket = new Bucket();
                    if (!bucketPointer.compareExchangeWeak(nullptr, bucket)) {
                        delete bucket;
                        continue;
                    }
                }

                buckets.append(bucket);
                break;
            }
        }

        // Now lock the buckets in the right order.
        std::sort(buckets.begin(), buckets.end());
        for (Bucket* bucket : buckets)
            bucket->lock.lock();

        // If the hashtable didn't change (wasn't rehashed) while we were locking it, then we own it
        // now.
        if (hashtable.load() == currentHashtable)
            return buckets;

        // The hashtable rehashed. Unlock everything and try again.
        for (Bucket* bucket : buckets)
            bucket->lock.unlock();
    }
}

void unlockHashtable(const Vector<Bucket*>& buckets)
{
    for (Bucket* bucket : buckets)
        bucket->lock.unlock();
}

// Rehash the hashtable to handle numThreads threads.
void ensureHashtableSize(unsigned numThreads)
{
    // We try to ensure that the size of the hashtable used for thread queues is always large enough
    // to avoid collisions. So, since we started a new thread, we may need to increase the size of the
    // hashtable. This does just that. Note that we never free the old spine, since we never lock
    // around spine accesses (i.e. the "hashtable" global variable).

    // First do a fast check to see if rehashing is needed.
    Hashtable* oldHashtable = hashtable.load();
    if (oldHashtable && static_cast<double>(oldHashtable->size) / static_cast<double>(numThreads) >= maxLoadFactor) {
        if (verbose)
            dataLog(toString(currentThread(), ": no need to rehash because ", oldHashtable->size, " / ", numThreads, " >= ", maxLoadFactor, "\n"));
        return;
    }

    // Seems like we *might* have to rehash, so lock the hashtable and try again.
    Vector<Bucket*> bucketsToUnlock = lockHashtable();

    // Check again, since the hashtable could have rehashed while we were locking it. Also,
    // lockHashtable() creates an initial hashtable for us.
    oldHashtable = hashtable.load();
    if (oldHashtable && static_cast<double>(oldHashtable->size) / static_cast<double>(numThreads) >= maxLoadFactor) {
        if (verbose)
            dataLog(toString(currentThread(), ": after locking, no need to rehash because ", oldHashtable->size, " / ", numThreads, " >= ", maxLoadFactor, "\n"));
        unlockHashtable(bucketsToUnlock);
        return;
    }

    Vector<Bucket*> reusableBuckets = bucketsToUnlock;

    // OK, now we resize. First we gather all thread datas from the old hashtable. These thread datas
    // are placed into the vector in queue order.
    Vector<ThreadData*> threadDatas;
    for (Bucket* bucket : reusableBuckets) {
        while (ThreadData* threadData = bucket->dequeue())
            threadDatas.append(threadData);
    }

    unsigned newSize = numThreads * growthFactor * maxLoadFactor;
    RELEASE_ASSERT(newSize > oldHashtable->size);
    
    Hashtable* newHashtable = Hashtable::create(newSize);
    if (verbose)
        dataLog(toString(currentThread(), ": created new hashtable: ", RawPointer(newHashtable), "\n"));
    for (ThreadData* threadData : threadDatas) {
        if (verbose)
            dataLog(toString(currentThread(), ": rehashing thread data ", RawPointer(threadData), " with address = ", RawPointer(threadData->address), "\n"));
        unsigned hash = hashAddress(threadData->address);
        unsigned index = hash % newHashtable->size;
        if (verbose)
            dataLog(toString(currentThread(), ": index = ", index, "\n"));
        Bucket* bucket = newHashtable->data[index].load();
        if (!bucket) {
            if (reusableBuckets.isEmpty())
                bucket = new Bucket();
            else
                bucket = reusableBuckets.takeLast();
            newHashtable->data[index].store(bucket);
        }
        
        bucket->enqueue(threadData);
    }
    
    // At this point there may be some buckets left unreused. This could easily happen if the
    // number of enqueued threads right now is low but the high watermark of the number of threads
    // enqueued was high. We place these buckets into the hashtable basically at random, just to
    // make sure we don't leak them.
    for (unsigned i = 0; i < newHashtable->size && !reusableBuckets.isEmpty(); ++i) {
        Atomic<Bucket*>& bucketPtr = newHashtable->data[i];
        if (bucketPtr.load())
            continue;
        bucketPtr.store(reusableBuckets.takeLast());
    }
    
    // Since we increased the size of the hashtable, we should have exhausted our preallocated
    // buckets by now.
    ASSERT(reusableBuckets.isEmpty());
    
    // OK, right now the old hashtable is locked up and the new hashtable is ready to rock and
    // roll. After we install the new hashtable, we can release all bucket locks.
    
    bool result = hashtable.compareExchangeStrong(oldHashtable, newHashtable);
    RELEASE_ASSERT(result);

    unlockHashtable(bucketsToUnlock);
}

ThreadData::ThreadData()
    : threadIdentifier(currentThread())
{
    unsigned currentNumThreads;
    for (;;) {
        unsigned oldNumThreads = numThreads.load();
        currentNumThreads = oldNumThreads + 1;
        if (numThreads.compareExchangeWeak(oldNumThreads, currentNumThreads))
            break;
    }

    ensureHashtableSize(currentNumThreads);
}

ThreadData::~ThreadData()
{
    for (;;) {
        unsigned oldNumThreads = numThreads.load();
        if (numThreads.compareExchangeWeak(oldNumThreads, oldNumThreads - 1))
            break;
    }
}

ThreadData* myThreadData()
{
    static std::once_flag initializeOnce;
    std::call_once(
        initializeOnce,
        [] {
            threadData = new ThreadSpecific<ThreadData>();
        });

    return *threadData;
}

template<typename Functor>
bool enqueue(const void* address, const Functor& functor)
{
    unsigned hash = hashAddress(address);

    for (;;) {
        Hashtable* myHashtable = hashtable.load();
        unsigned index = hash % myHashtable->size;
        Atomic<Bucket*>& bucketPointer = myHashtable->data[index];
        Bucket* bucket;
        for (;;) {
            bucket = bucketPointer.load();
            if (!bucket) {
                bucket = new Bucket();
                if (!bucketPointer.compareExchangeWeak(nullptr, bucket)) {
                    delete bucket;
                    continue;
                }
            }
            break;
        }
        if (verbose)
            dataLog(toString(currentThread(), ": enqueueing onto bucket ", RawPointer(bucket), " with index ", index, " for address ", RawPointer(address), " with hash ", hash, "\n"));
        bucket->lock.lock();

        // At this point the hashtable could have rehashed under us.
        if (hashtable.load() != myHashtable) {
            bucket->lock.unlock();
            continue;
        }

        ThreadData* threadData = functor();
        bool result;
        if (threadData) {
            if (verbose)
                dataLog(toString(currentThread(), ": proceeding to enqueue ", RawPointer(threadData), "\n"));
            bucket->enqueue(threadData);
            result = true;
        } else
            result = false;
        bucket->lock.unlock();
        return result;
    }
}

template<typename Functor>
bool dequeue(const void* address, const Functor& functor)
{
    if (verbose)
        dataLog(toString(currentThread(), ": dequeueing address ", RawPointer(address), "\n"));
    unsigned hash = hashAddress(address);
    if (verbose)
        dataLog(toString(currentThread(), ": hash = ", hash, "\n"));

    for (;;) {
        Hashtable* myHashtable = hashtable.load();
        if (!myHashtable) {
            if (verbose)
                dataLog(toString(currentThread(), ": no hashtable.\n"));
            return false;
        }
        unsigned index = hash % myHashtable->size;
        if (verbose)
            dataLog(toString(currentThread(), ": index = ", index, "\n"));
        Bucket* bucket = myHashtable->data[index].load();
        if (!bucket) {
            if (verbose)
                dataLog(toString(currentThread(), ": no bucket at ", RawPointer(bucket), ".\n"));
            return false;
        }

        if (verbose)
            dataLog(toString(currentThread(), ": locking bucket at ", RawPointer(bucket), "\n"));
        bucket->lock.lock();
        if (verbose)
            dataLog(toString(currentThread(), ": locked bucket at ", RawPointer(bucket), "\n"));

        // At this point the hashtable could have rehashed under us.
        if (hashtable.load() != myHashtable) {
            if (verbose)
                dataLog(toString(currentThread(), ": hashtable changed.\n"));
            bucket->lock.unlock();
            continue;
        }

        if (verbose)
            dataLog(toString(currentThread(), ": found bucket.\n"));
        bucket->genericDequeue(functor);
        bool result = !!bucket->queueHead;
        bucket->lock.unlock();
        return result;
    }
}

} // anonymous namespace

bool ParkingLot::parkConditionally(const void* address, std::function<bool()> validation)
{
    if (verbose)
        dataLog(toString(currentThread(), ": parking.\n"));
    
    ThreadData* me = myThreadData();

    ASSERT(!me->shouldPark);
    
    bool result = enqueue(
        address,
        [&] () -> ThreadData* {
            if (!validation())
                return nullptr;

            me->address = address;
            me->shouldPark = true;
            return me;
        });

    if (!result)
        return false;

    if (verbose)
        dataLog(toString(currentThread(), ": parking self: ", RawPointer(me), "\n"));
    {
        std::unique_lock<std::mutex> locker(me->parkingLock);
        while (me->shouldPark)
            me->parkingCondition.wait(locker);

        me->address = nullptr;
    }
    if (verbose)
        dataLog(toString(currentThread(), ": unparked self: ", RawPointer(me), "\n"));
    return true;
}

bool ParkingLot::unparkOne(const void* address)
{
    if (verbose)
        dataLog(toString(currentThread(), ": unparking one.\n"));

    ThreadData* threadData = nullptr;
    bool result = dequeue(
        address,
        [&] (ThreadData* element) {
            if (element->address != address)
                return DequeueResult::Ignore;
            threadData = element;
            return DequeueResult::RemoveAndStop;
        });

    if (!threadData)
        return false;

    ASSERT(threadData->shouldPark);
    
    {
        std::unique_lock<std::mutex> locker(threadData->parkingLock);
        threadData->shouldPark = false;
        threadData->parkingCondition.notify_all();
    }

    return result;
}

void ParkingLot::unparkAll(const void* address)
{
    if (verbose)
        dataLog(toString(currentThread(), ": unparking all from ", RawPointer(address), ".\n"));
    
    Vector<ThreadData*, 8> threadDatas;
    dequeue(
        address,
        [&] (ThreadData* element) {
            if (verbose)
                dataLog(toString(currentThread(), ": Observing element with address = ", RawPointer(element->address), "\n"));
            if (element->address != address)
                return DequeueResult::Ignore;
            threadDatas.append(element);
            return DequeueResult::RemoveAndContinue;
        });

    for (ThreadData* threadData : threadDatas) {
        if (verbose)
            dataLog(toString(currentThread(), ": unparking ", RawPointer(threadData), " with address ", RawPointer(threadData->address), "\n"));
        ASSERT(threadData->shouldPark);
        std::unique_lock<std::mutex> locker(threadData->parkingLock);
        threadData->shouldPark = false;
        threadData->parkingCondition.notify_all();
    }

    if (verbose)
        dataLog(toString(currentThread(), ": done unparking.\n"));
}

void ParkingLot::forEach(std::function<void(ThreadIdentifier, const void*)> callback)
{
    Vector<Bucket*> bucketsToUnlock = lockHashtable();

    Hashtable* currentHashtable = hashtable.load();
    for (unsigned i = currentHashtable->size; i--;) {
        Bucket* bucket = currentHashtable->data[i].load();
        if (!bucket)
            continue;
        for (ThreadData* currentThreadData = bucket->queueHead; currentThreadData; currentThreadData = currentThreadData->nextInQueue)
            callback(currentThreadData->threadIdentifier, currentThreadData->address);
    }
    
    unlockHashtable(bucketsToUnlock);
}

} // namespace WTF


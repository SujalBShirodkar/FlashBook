#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <new>

template<typename T, size_t N>
class MemoryPool {
    static_assert(sizeof(T) >= sizeof(void*),
        "T must be at least pointer-sized for intrusive freelist");
    static_assert(N > 0, "Pool must have at least 1 slot");

public:
    MemoryPool() {
        for (size_t i = 0; i < N - 1; ++i) {
            reinterpret_cast<Slot*>(&storage_[i])->next =
                reinterpret_cast<Slot*>(&storage_[i + 1]);
        }
        reinterpret_cast<Slot*>(&storage_[N - 1])->next = nullptr;

        free_head_ = reinterpret_cast<Slot*>(&storage_[0]);
        free_count_ = N;
    }

    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    T* alloc() {
        if (!free_head_) {
            throw std::bad_alloc(); 
        }

        Slot* slot   = free_head_;
        free_head_   = slot->next;  
        --free_count_;

        return reinterpret_cast<T*>(slot);
    }

    void free(T* ptr) {
        if (!ptr) return;

        assert(owns(ptr) && "Pointer does not belong to this pool");

        Slot* slot   = reinterpret_cast<Slot*>(ptr);
        slot->next   = free_head_;
        free_head_   = slot;
        ++free_count_;
    }

    size_t freeCount()  const { return free_count_; }
    size_t usedCount()  const { return N - free_count_; }
    size_t capacity()   const { return N; }

    bool owns(const T* ptr) const {
        const auto* p     = reinterpret_cast<const StorageSlot*>(ptr);
        const auto* begin = &storage_[0];
        const auto* end   = &storage_[N];
        return p >= begin && p < end;
    }

private:
    struct Slot {
        Slot* next; 
    };

    struct alignas(T) StorageSlot {
        uint8_t bytes[sizeof(T)];
    };

    StorageSlot storage_[N];  
    Slot*       free_head_;   
    size_t      free_count_;  
};
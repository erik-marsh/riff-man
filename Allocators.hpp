#pragma once

#include <cassert>
#include <cstdlib>
#include <span>

// making our initial memory allocation strategy as dumb as humanly possible
// since i don't know what i want the lifetime of SongEntry entites to be
// this is just a stack-based arena that assumes **nothing is ever de-allocated**
template <typename T>
struct Arena {
    T* arr;
    int capacity = 0;
    int top = 0;

    void Reserve(size_t cap) {
        // arr = static_cast<T*>(std::malloc(cap * sizeof(T)));
        arr = new T[cap];
        capacity = cap;
    }

    int Allocate() {
        assert(arr && "No buffer has been allocated for this arena.");
        top++;
        assert((top < capacity) && "Overallocated!");
        return top - 1;
    }

    void Reset() {
        top = 0;
    }

    std::span<const T> Span() {
        return {arr, static_cast<size_t>(top)};
    }
};


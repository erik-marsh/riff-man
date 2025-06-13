#pragma once

#include <cassert>

// making our initial memory allocation strategy as dumb as humanly possible
// since i don't know what i want the lifetime of SongEntry entites to be
// this is just a stack-based arena that assumes **nothing is ever de-allocated**
template <typename T>
struct Arena {
    T arr[1000];
    int top = 0;

    int Allocate() {
        top++;
        assert((top < 1000) && "Overallocated!");
        return top - 1;
    }
};


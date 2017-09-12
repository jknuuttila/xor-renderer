#ifndef XOR_SORTINGNETWORKS_H
#define XOR_SORTINGNETWORKS_H

// Sorting macros defined using X macros.
// #define SWAP(a, b) to whatever, a and b will be integer literals.

#define XOR_SORTING_NETWORK_9 \
    SWAP(0, 1); \
    SWAP(3, 4); \
    SWAP(6, 7); \
    SWAP(1, 2); \
    SWAP(4, 5); \
    SWAP(7, 8); \
    SWAP(0, 1); \
    SWAP(3, 4); \
    SWAP(6, 7); \
    SWAP(0, 3); \
    SWAP(3, 6); \
    SWAP(0, 3); \
    SWAP(1, 4); \
    SWAP(4, 7); \
    SWAP(1, 4); \
    SWAP(2, 5); \
    SWAP(5, 8); \
    SWAP(2, 5); \
    SWAP(1, 3); \
    SWAP(5, 7); \
    SWAP(2, 6); \
    SWAP(4, 6); \
    SWAP(2, 4); \
    SWAP(2, 3); \
    SWAP(5, 6);

#endif

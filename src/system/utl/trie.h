#pragma once

#include "os\Debug.h"
#include "utl\MemMgr.h"

// Trie data structure for string storage
//
// Memory layout - each node is 17 bytes (0x11):
//   Offset 0x00 (4 bytes): First child index
//   Offset 0x04 (4 bytes): Next sibling index
//   Offset 0x08 (4 bytes): Parent index
//   Offset 0x0C (4 bytes): Count field (big-endian)
//       - Upper 24 bits (>> 8): duplicate/reference count
//       - Lower 8 bits (byte at 0x0F): sibling count
//   Offset 0x10 (1 byte): Character
//
// Header area at offset 0x220000 from base:
//   Offset 0x00 (4 bytes): _nodeCount - total allocated nodes
//   Offset 0x04 (4 bytes): free list head index
//
// Legacy header at offset 0x20 (used by remove):
//   Offset 0x20 (1 byte): root sibling count
//   Offset 0x21 (1 byte): root character

// The trie's backing store.  AllocInfoInit() is compiled into utl/AllocInfo.obj
// (the shipped map lists it as a plain out-of-line `f`), yet the __FILE__ its
// MemAlloc call bakes into the image is
//     e:\lazer_build_gmc1\system\src\utl\trie.h
// with line 0x28.  A __FILE__ is spelled where the token is WRITTEN, so the
// allocation was written here, in trie.h, and fully inlined into AllocInfoInit
// -- which is why it has no symbol of its own anywhere in the map.  The
// original's name for it is therefore unrecoverable; only the call's file, its
// line (0x28, i.e. just above class Trie, whose check_index sits at 0x36) and
// the emitted code are evidence.
static inline void *AllocTrieMemory() {
    // 0x20000 nodes * 0x11 bytes, plus the 8-byte header at the end.
    return MemAlloc(0x220008, __FILE__, 0x28, "Trie");
}

class Trie {
public:
    int store(const char *str);
    void remove(unsigned int index);
    unsigned int get_free_node();
    void delete_node(unsigned int index);
    void inc_count(unsigned int index);
    void dec_count(unsigned int index);
    void inc_dup_count(unsigned int index);
    void dec_dup_count(unsigned int index);

    enum { MAX_NODES = 0x20000, NODE_SIZE = 0x11, HEADER_OFFSET = 0x220000 };

    // Node access helpers - pointer arithmetic, because a node is 17 bytes.
    // They are Trie statics rather than free functions on purpose: `Character`
    // and `Parent` are ordinary engine class names, and trie.h reaches TUs that
    // use both.
    static char *NodePtr(Trie *trie, unsigned int idx) {
        return (char *)trie + idx * NODE_SIZE;
    }

    static unsigned int &FirstChild(char *node) { return *(unsigned int *)(node + 0x00); }
    static unsigned int &NextSibling(char *node) { return *(unsigned int *)(node + 0x04); }
    static unsigned int &Parent(char *node) { return *(unsigned int *)(node + 0x08); }
    static unsigned int &CountField(char *node) { return *(unsigned int *)(node + 0x0C); }
    static unsigned char &SiblingCount(char *node) { return *(unsigned char *)(node + 0x0F); }
    static unsigned char &Character(char *node) { return *(unsigned char *)(node + 0x10); }

    // Dup count is the upper 24 bits of the count field.
    static unsigned int GetDupCount(unsigned int countField) { return countField >> 8; }

    static unsigned int &NodeCount(Trie *trie) {
        return *(unsigned int *)((char *)trie + HEADER_OFFSET);
    }

    static unsigned int &FreeListHead(Trie *trie) {
        return *(unsigned int *)((char *)trie + HEADER_OFFSET + 4);
    }

    void check_index(unsigned int n) {
        MILO_ASSERT(0<= n && n < MAX_NODES, 0x36);
    }

    char *get(int index, char *buffer, int bufSize) {
        if (index <= 0 || index >= 0x20000) {
            *buffer = 0;
            return buffer;
        }

        check_index(index);
        char *node = (char *)this + index * 0x11;
        if (*(unsigned char *)(node + 0x10) != 0) {
            *buffer = 0;
            return buffer;
        }

        char *end = buffer + bufSize;
        int count = 0;
        char *ptr = end - 1;

        do {
            if (count >= bufSize) break;
            check_index(index);
            char *node = (char *)this + index * 0x11;
            *ptr = *(unsigned char *)(node + 0x10);
            check_index(index);
            count++;
            ptr--;
            index = *(unsigned int *)(node + 0x08);
        } while (index != 0);

        *(end - 1) = 0;
        return (ptr == end - 1) ? ptr : ptr + 1;
    }
};

// These six are `inline` here rather than out-of-line in trie.cpp because
// ham_xbox_r.map says so: every one of them is flagged `f i` (a pick-any COMDAT,
// which is what MSVC emits for an inline definition), while `store` and `remove`
// on the two lines below them are bare `f`.  It is also what makes
// get_free_node's MILO_ASSERT name utl\trie.h instead of trie.cpp.
//
// __declspec(noinline) is load-bearing: without it MSVC expands all six into
// Trie::store and Trie::remove, which took store 95.0 -> 79.93 and remove
// 82.90 -> 75.64.  The image keeps them out of line -- all six have their own
// address and their own callers in ham_xbox_r.map -- so the attribute restores
// the shipped inline boundary without giving up the linkage.

__declspec(noinline) inline void Trie::inc_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int *cf = &CountField(node);
    unsigned int count = SiblingCount(node);
    check_index(index);
    *cf = (*cf & 0xFFFFFF00) | (count + 1);
}

__declspec(noinline) inline void Trie::dec_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int *cf = &CountField(node);
    unsigned int count = SiblingCount(node);
    check_index(index);
    *cf = (*cf & 0xFFFFFF00) | (count - 1);
}

__declspec(noinline) inline void Trie::inc_dup_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int dupCount = GetDupCount(CountField(node));
    check_index(index);
    CountField(node) = ((dupCount + 1) << 8) | SiblingCount(node);
}

__declspec(noinline) inline void Trie::dec_dup_count(unsigned int index) {
    check_index(index);
    unsigned int *cf = &CountField(NodePtr(this, index));
    unsigned int dupCount = GetDupCount(CountField(NodePtr(this, index)));
    check_index(index);
    *cf = ((dupCount - 1) << 8) | SiblingCount(NodePtr(this, index));
}

__declspec(noinline) inline unsigned int Trie::get_free_node() {
    unsigned int freeHead = FreeListHead(this);

    if (freeHead != 0) {
        // Pop from free list - next free is stored in NextSibling slot
        check_index(freeHead);
        char *freeNode = NodePtr(this, freeHead);
        FreeListHead(this) = NextSibling(freeNode);
        return freeHead;
    }

    // Allocate new node - increment node count
    int &_nodeCount = *(int *)((char *)this + HEADER_OFFSET);
    MILO_ASSERT(_nodeCount < MAX_NODES, 0x82);
    unsigned int newIdx = _nodeCount;
    _nodeCount = newIdx + 1;
    return newIdx;
}

__declspec(noinline) inline void Trie::delete_node(unsigned int index) {
    check_index(index);
    // Clear FirstChild before recomputing node ptr
    FirstChild(NodePtr(this, index)) = 0;
    check_index(index);
    char *node = NodePtr(this, index);

    NextSibling(node) = 0;
    check_index(index);

    Parent(node) = 0;
    CountField(node) = 0;
    check_index(index);

    *(char *)(node + 0x10) = -1;

    // Get old free list head and link
    unsigned int oldHead = FreeListHead(this);
    if (oldHead != 0) {
        check_index(index);
        NextSibling(node) = FreeListHead(this);
    }

    FreeListHead(this) = index;
}

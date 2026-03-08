#include "trie.h"

// Node access helpers - uses pointer arithmetic due to unusual 17-byte node size
#define NODE_SIZE 0x11

// Get pointer to node at given index
static inline char *NodePtr(Trie *trie, unsigned int idx) {
    return (char *)trie + idx * NODE_SIZE;
}

// Node field accessors
static inline unsigned int &FirstChild(char *node) {
    return *(unsigned int *)(node + 0x00);
}

static inline unsigned int &NextSibling(char *node) {
    return *(unsigned int *)(node + 0x04);
}

static inline unsigned int &Parent(char *node) {
    return *(unsigned int *)(node + 0x08);
}

static inline unsigned int &CountField(char *node) {
    return *(unsigned int *)(node + 0x0C);
}

static inline unsigned char &SiblingCount(char *node) {
    return *(unsigned char *)(node + 0x0F);
}

static inline unsigned char &Character(char *node) {
    return *(unsigned char *)(node + 0x10);
}

// Get dup count from count field (upper 24 bits)
static inline unsigned int GetDupCount(unsigned int countField) {
    return countField >> 8;
}

// Header is at offset 0x220000 from trie base
// Header layout:
//   Offset 0: _nodeCount (4 bytes)
//   Offset 4: free list head (4 bytes)
#define HEADER_OFFSET 0x220000

static inline unsigned int &NodeCount(Trie *trie) {
    return *(unsigned int *)((char *)trie + HEADER_OFFSET);
}

static inline unsigned int &FreeListHead(Trie *trie) {
    return *(unsigned int *)((char *)trie + HEADER_OFFSET + 4);
}

void Trie::inc_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int *cf = &CountField(node);
    unsigned char count = SiblingCount(node);
    check_index(index);
    *cf = (*cf & 0xFFFFFF00) | (count + 1);
}

void Trie::dec_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned char count = SiblingCount(node);
    unsigned int *cf = &CountField(node);
    check_index(index);
    *cf = (*cf & 0xFFFFFF00) | (count - 1);
}

void Trie::inc_dup_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int dupCount = GetDupCount(CountField(node));
    check_index(index);
    CountField(node) = ((dupCount + 1) << 8) | SiblingCount(node);
}

void Trie::dec_dup_count(unsigned int index) {
    check_index(index);
    char *node = NodePtr(this, index);
    unsigned int *cf = &CountField(node);
    unsigned int dupCount = GetDupCount(CountField(node));
    check_index(index);
    *cf = ((dupCount - 1) << 8) | SiblingCount(node);
}

unsigned int Trie::get_free_node() {
    unsigned int freeHead = FreeListHead(this);

    if (freeHead != 0) {
        // Pop from free list - next free is stored in NextSibling slot
        check_index(freeHead);
        char *freeNode = NodePtr(this, freeHead);
        FreeListHead(this) = NextSibling(freeNode);
        return freeHead;
    }

    // Allocate new node - increment node count
    MILO_ASSERT(NodeCount(this) < 0x20000, 0x82);
    unsigned int newIdx = NodeCount(this) + 1;
    NodeCount(this) = newIdx;
    return newIdx;
}

void Trie::delete_node(unsigned int index) {
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

    Character(node) = (unsigned char)-1;

    // Get old free list head and link
    unsigned int oldHead = FreeListHead(this);
    if (oldHead != 0) {
        check_index(index);
        NextSibling(node) = FreeListHead(this);
    }

    FreeListHead(this) = index;
}

int Trie::store(const char *str) {
                if (str == 0)
            return 0;
        if (*str == 0)
            return 0;

    unsigned int curIdx = 1;
    unsigned int prevSib = 0;

    // Compute string length by walking the string
    unsigned char c;
    do {
        c = *str++;
    } while (c != 0);
    int strLen = (int)(str - str) - 1;
    if (strLen < 0) goto do_terminator;

    // Process each character
    for (int i = 0; ; ) {
        c = str[i];
        check_index(curIdx);
        unsigned int childIdx = FirstChild(NodePtr(this, curIdx));

        if (childIdx == 0) {
            // No children - create new child
            unsigned int newIdx = get_free_node();
            check_index(newIdx);
            char *newNode = NodePtr(this, newIdx);
            Character(newNode) = c;
            Parent(newNode) = curIdx;
            check_index(curIdx);
            FirstChild(NodePtr(this, curIdx)) = newIdx;
            inc_count(1);
            prevSib = curIdx;
            curIdx = newIdx;
        } else {
            // Search siblings for matching char
            check_index(childIdx);
            int sibCount = (int)(signed char)CountField(NodePtr(this, childIdx));
            if (sibCount > 0) {
                signed char extC = (signed char)c;
                unsigned int sibIdx = childIdx;
                int j = 0;

                while (j < sibCount) {
                    check_index(sibIdx);
                    char *sibNode = NodePtr(this, sibIdx);
                    if ((signed char)Character(sibNode) == extC) {
                        prevSib = curIdx;
                        curIdx = sibIdx;
                        goto next_iter;
                    }
                    j++;
                    if (j >= sibCount) break;
                    check_index(sibIdx);
                    sibIdx = NextSibling(sibNode);
                }
                prevSib = sibIdx;
            }

            // Not found - create new sibling
            unsigned int newIdx = get_free_node();
            if (prevSib != 0) {
                check_index(prevSib);
                NextSibling(NodePtr(this, prevSib)) = newIdx;
            }
            check_index(newIdx);
            char *newNode = NodePtr(this, newIdx);
            NextSibling(newNode) = childIdx;
            check_index(newIdx);
            Character(newNode) = c;
            check_index(newIdx);
            Parent(newNode) = curIdx;
            if (prevSib == 0) {
                check_index(curIdx);
                FirstChild(NodePtr(this, curIdx)) = newIdx;
            }
            inc_count(childIdx);
            prevSib = curIdx;
            curIdx = newIdx;
        }

    next_iter:
        i++;
        if (str[i] == 0) break;
        if (i > strLen) break;
    }

do_terminator:
    // Add terminating node (char = 0)
    check_index(curIdx);
    {
        unsigned int childIdx = FirstChild(NodePtr(this, curIdx));

        if (childIdx != 0) {
            check_index(childIdx);
            unsigned char sibCount = SiblingCount(NodePtr(this, childIdx));
            if (sibCount != 0) {
                unsigned int sibIdx = childIdx;
                for (unsigned int k = 0; k < sibCount; k++) {
                    check_index(sibIdx);
                    char *sibNode = NodePtr(this, sibIdx);
                    if (Character(sibNode) == 0) {
                        inc_dup_count(sibIdx);
                        return sibIdx;
                    }
                    sibIdx = NextSibling(sibNode);
                }
            }
        }

        // Create terminator node
        unsigned int termIdx = get_free_node();
        check_index(termIdx);
        char *termNode = NodePtr(this, termIdx);
        Character(termNode) = 0;
        Parent(termNode) = curIdx;

        if (!(childIdx == 0)) {
            unsigned int sibIdx = childIdx;
            check_index(childIdx);
            unsigned char sibCount = SiblingCount(NodePtr(this, childIdx));
            for (unsigned int k = 0; k < sibCount - 1; k++) {
                check_index(sibIdx);
                sibIdx = NextSibling(NodePtr(this, sibIdx));
            }
            check_index(sibIdx);
            NextSibling(NodePtr(this, sibIdx)) = termIdx;
            inc_count(childIdx);
        } else {
            check_index(curIdx);
            FirstChild(NodePtr(this, curIdx)) = termIdx;
            inc_count(termIdx);
        }

        inc_dup_count(termIdx);
        return termIdx;
    }
}

void Trie::remove(unsigned int index) {
    unsigned int curIdx = index;
    check_index(curIdx);
    char *curNode = NodePtr(this, curIdx);

    // Only process if char is 0 (end of string marker) and has dup count
    if (Character(curNode) != 0) {
        return;
    }

    check_index(curIdx);
    unsigned int counts = CountField(curNode);
    if ((counts & 0xFFFFFF00) == 0) {
        return;
    }

    check_index(curIdx);
    if ((counts & 0xFFFFFF00) != 0x100) {
        // More than one reference, just decrement
        dec_dup_count(curIdx);
        return;
    }

    // Single reference - remove the node chain
loop_start:
    if (curIdx != 0) {
        check_index(curIdx);
        unsigned int parentIdx = Parent(curNode);
        if (parentIdx != 0) {
            check_index(parentIdx);
            unsigned int parentFirstChild = FirstChild(NodePtr(this, parentIdx));
            check_index(parentFirstChild);
            char *firstChildNode = NodePtr(this, parentFirstChild);
            if (SiblingCount(firstChildNode) == 1) {
                // Single child - merge upward
                check_index(curIdx);
                unsigned int toDelete = curIdx;
                curIdx = Parent(curNode);
                delete_node(toDelete);
                goto update_node;
            }
        }
    }

    // Find the first child in the sibling chain
    check_index(curIdx);
    unsigned int firstChildIdx;
    if (Parent(curNode) == 0) {
        firstChildIdx = 1;
    } else {
        check_index(curIdx);
        unsigned int parentIdx = Parent(curNode);
        check_index(parentIdx);
        firstChildIdx = FirstChild(NodePtr(this, parentIdx));
    }

    unsigned int sibIdx = firstChildIdx;
    check_index(firstChildIdx);
    unsigned char sibCount = SiblingCount(NodePtr(this, firstChildIdx));
    unsigned int prevSib = 0;
    unsigned int traverseCount = 0;

    if (sibCount == 0) {
        goto update_node;
    }

    // Find this node in sibling chain
    while (sibIdx != curIdx) {
        prevSib = sibIdx;
        check_index(sibIdx);
        traverseCount++;
        sibIdx = NextSibling(NodePtr(this, sibIdx));
        if (traverseCount >= sibCount) {
            goto update_node;
        }
    }

    // Found the node - unlink from chain
    if (prevSib != 0) {
        check_index(curIdx);
        check_index(prevSib);
        NextSibling(NodePtr(this, prevSib)) = NextSibling(curNode);
        delete_node(curIdx);
        dec_count(firstChildIdx);
        return;
    }

    // Node is first in chain
    if (curIdx == 1) {
        // Root level special handling
        unsigned int scanCount = 0;
        unsigned char rootCount = *(unsigned char *)((char *)this + 0x20);

        while (scanCount < rootCount - 1) {
            check_index(curIdx);
            scanCount++;
            curIdx = NextSibling(curNode);
            curNode = NodePtr(this, curIdx);
        }

        if (curIdx == 1) {
            delete_node(1);
            return;
        }

        // Move last sibling to position 1
        check_index(curIdx);
        FirstChild(NodePtr(this, 1)) = FirstChild(curNode);
        check_index(curIdx);
        *(unsigned char *)((char *)this + 0x21) = Character(curNode);
        delete_node(curIdx);
        dec_count(firstChildIdx);

        // Update parent pointers of children
        unsigned int updateCount = 0;
        auto _tmp2 = NodePtr(this, 1);
        unsigned int updateIdx = FirstChild(_tmp2);

        while (true) {
            unsigned int childIdx = FirstChild(NodePtr(this, 1));
            check_index(childIdx);
            if (updateCount >= SiblingCount(NodePtr(this, childIdx))) {
                break;
            }
            check_index(updateIdx);
            char *updateNode = NodePtr(this, updateIdx);
            Parent(updateNode) = 1;
            check_index(updateIdx);
            updateCount++;
            updateIdx = NextSibling(updateNode);
        }
        return;
    }

    // Not root - update parent's first child
    check_index(curIdx);
    check_index(curIdx);
    unsigned int parentIdx2 = Parent(curNode);
    check_index(parentIdx2);
    FirstChild(NodePtr(this, parentIdx2)) = NextSibling(curNode);

    // Update sibling count
    check_index(curIdx);
    unsigned char newSibCount = SiblingCount(curNode) - 1;
    check_index(curIdx);
    unsigned int parentIdx3 = Parent(curNode);
    check_index(parentIdx3);
    unsigned int newFirstChild = FirstChild(NodePtr(this, parentIdx3));
    check_index(newFirstChild);
    char *newFirstChildNode = NodePtr(this, newFirstChild);
    CountField(newFirstChildNode) = (CountField(newFirstChildNode) & 0xFFFFFF00) | newSibCount;

    delete_node(curIdx);
    return;

update_node:
    check_index(curIdx);
    curNode = NodePtr(this, curIdx);
    if (Character(curNode) == 0) {
        return;
    }
    goto loop_start;
}

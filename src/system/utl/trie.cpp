#include "utl\trie.h"

// The node-access helpers, the six inline members and the backing-store
// allocator all live in utl/trie.h now -- ham_xbox_r.map flags them `f i`
// (pick-any COMDAT = an inline definition), and only `store` and `remove`
// below are bare `f`.  The include is spelled with the search-path prefix on
// purpose: it is what makes __FILE__ inside trie.h read
// e:\lazer_build_gmc1\system\src\utl\trie.h, which is the string the shipped
// image carries.  A bare #include "trie.h" resolves out of the compiler's cwd
// and yields the basename instead.

// Lane history (28 lines moved): docs/decomp/notes/system/utl/trie.md
int Trie::store(const char *str) {
    if (str == 0 || *str == 0)
        goto return_zero;

    unsigned int curIdx = 1;
    unsigned int parentIdx = 0;

    // Lane history (6 lines moved): docs/decomp/notes/system/utl/trie.md
    int strLen = strlen(str);
    int i;

    // A `for`, not a `do`: the image guards loop entry with `clrrwi. r21, r11, 0`
    // / `blt <tail>` -- MSVC's zero-trip test for `i = 0; i <= strLen` -- before
    // falling into a bottom-tested body.  A do/while emits no such guard at all
    // (95.00 -> 95.31; 4 deletes/2 replaces -> 3/3).  Residual there: we fuse the
    // decrement into the test (`subic. r21, r11, 0x1`) where the image keeps
    // `subi r11, r11, 0x1` and a separate record-form copy `clrrwi. r21, r11, 0`.
    //
    // Attempt 2, REFUTED: moving `parentIdx`'s declaration below the length walk
    // to break the r23..r28 rotation (image: str=r23, extsb(ch)=r24, j=r25,
    // sibCount=r26, parentIdx=r28; ours: parentIdx=r23 and everything else one
    // slot later).  It does not rotate the group, it splits `i` off into r25 and
    // sinks `li r28, 0x0`: 95.31 -> 93.90.  The rotation is 40 of the 47 residual
    // rows and is not reachable by declaration order.
    for (i = 0; i <= strLen; i++) {
        unsigned int nodeIdx = curIdx;
        char ch = str[i];
        check_index(nodeIdx);
        int j = 0;
        int sibCount = (signed char)(int)(*(unsigned int *)((char *)this + nodeIdx * NODE_SIZE + 0x0C));
        curIdx = nodeIdx;
        if ('\0' < sibCount) {
            do {
                check_index(nodeIdx);
                char *node = NodePtr(this, nodeIdx);
                if (node[0x10] == ch) {
                    parentIdx = curIdx;
                    check_index(nodeIdx);
                    curIdx = FirstChild(node);
                    goto found;
                }
                if (j != sibCount - 1) {
                    check_index(nodeIdx);
                    nodeIdx = NextSibling(node);
                }
                j++;
                curIdx = nodeIdx;
            } while (j < sibCount);
        }

        // Not found - insert new node
        {
            nodeIdx = get_free_node();
            if (sibCount == 0) {
                if ((int)parentIdx > 0) {
                    check_index(parentIdx);
                    FirstChild(NodePtr(this, parentIdx)) = nodeIdx;
                }
            } else {
                check_index(curIdx);
                NextSibling(NodePtr(this, curIdx)) = nodeIdx;
            }
            check_index(nodeIdx);
            char *newNode = NodePtr(this, nodeIdx);
            newNode[0x10] = ch;
            check_index(nodeIdx);
            *(unsigned int *)(newNode + 0x08) = parentIdx;
            if ((int)parentIdx > 0) {
                check_index(parentIdx);
                inc_count(FirstChild(NodePtr(this, parentIdx)));
            } else {
                inc_count(1);
            }
            parentIdx = nodeIdx;
            curIdx = nodeIdx;
            if (str[i] != '\0') goto fast_path;
        }

    found:;
    }

    goto done;

fast_path:
    do {
        i++;
        curIdx = get_free_node();
        check_index(parentIdx);
        FirstChild(NodePtr(this, parentIdx)) = curIdx;
        check_index(curIdx);
        char *newNode = NodePtr(this, curIdx);
        *(unsigned int *)(newNode + 0x08) = parentIdx;
        char c2 = str[i];
        check_index(curIdx);
        newNode[0x10] = c2;
        inc_count(curIdx);
        parentIdx = curIdx;
    } while (i < strLen);

done:
    int result = curIdx;
    if (result == 0)
        result = parentIdx;
    inc_dup_count(result);
    return result;
return_zero:
    return 0;
}

// Lane history (78 lines moved): docs/decomp/notes/system/utl/trie.md
// > RESOLVED 2026-09-15 (lane w7-bn): 88.61 -> 100.0 canonical, 924 B both sides, 231
// > rows all equal.
void Trie::remove(unsigned int index) {
    unsigned int curIdx = index;
    check_index(curIdx);
    char *curNode = NodePtr(this, curIdx);

    // Only process if char is 0 (end of string marker) and has dup count
    if (Character(curNode) != 0) {
        return;
    }

    check_index(curIdx);
    if ((CountField(curNode) & 0xFFFFFF00) == 0) {
        return;
    }

    check_index(curIdx);
    if ((CountField(curNode) & 0xFFFFFF00) == 0x100) {
        // Single reference - remove the node chain
    loop_start:
        if (curIdx != 0) {
            check_index(curIdx);
            unsigned int parentIdx = Parent(curNode);
            if (parentIdx != 0) {
                // Retail range-checks the node again and re-reads the parent
                // link inside the guard rather than reusing the value it just
                // tested (bl check_index / lwz 0x8(node) / bl check_index).
                check_index(curIdx);
                parentIdx = Parent(curNode);
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
        // Lane history (9 lines moved): docs/decomp/notes/system/utl/trie.md
        // > NEGATIVE RESULT (w7-as, 2026-09-14): the r25/r26/r27 three-way rotation (16
        // > of the 54 residual rows) is NOT reachable by declaration order.
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
            // The root sibling count is re-read from the header on every trip
            // (lbz 0x20(this) sits inside the loop), not hoisted into a local.
            // The `check_index(1)` is load-bearing even though it emits nothing:
            // it is inlined and folded, and the fold leaves the loop header
            // split across basic blocks, which is what stops MSVC rotating the
            // loop (peeled `subic./beq` guard + duplicated latch, 88.61).  With
            // it the image's single top test and `b .L_827FE588` back-edge
            // come out (93.5).
#ifdef HX_NATIVE
#define TRIE_ROOT_SIBLING_COUNT (check_index(1), SiblingCount(NodePtr(this, 1)))
#else
#define TRIE_ROOT_SIBLING_COUNT (check_index(1), *(unsigned char *)((char *)this + 0x20))
#endif

            // Lane history (8 lines moved): docs/decomp/notes/system/utl/trie.md
            while (scanCount < TRIE_ROOT_SIBLING_COUNT - 1) {
                check_index(curIdx);
                scanCount++;
                curIdx = NextSibling(NodePtr(this, curIdx));
            }
#undef TRIE_ROOT_SIBLING_COUNT

            // Retail tail-merges the root delete: at .L_827FE664 it emits
            // `li r4, 0x1` and branches straight to the shared `bl delete_node`
            // at .L_827FE708, skipping that path's CountField update.  It looks
            // like a missing call in the listing and is not -- both builds emit
            // `delete_node` 4 times.  MSVC keeps whichever copy of the merged
            // tail is reached by fall-through, so the root stub has to be
            // (a) laid out after the re-parent loop -- the inverted
            // `if (curIdx != 1) { ...; return; }` does that (97.4 -> 99.1) --
            // and (b) created AFTER the not-root path so that path's `bl` is
            // the fall-through copy: hence the `goto delete_root` to a label
            // past the not-root block (99.1 -> 100).
            if (curIdx != 1) {
                // Move last sibling to position 1
                check_index(curIdx);
                curNode = NodePtr(this, curIdx);
                FirstChild(NodePtr(this, 1)) = FirstChild(curNode);
                check_index(curIdx);
#ifdef HX_NATIVE
                Character(NodePtr(this, 1)) = Character(curNode);
#else
                *(unsigned char *)((char *)this + 0x21) = Character(curNode);
#endif
                delete_node(curIdx);
                dec_count(firstChildIdx);

                // Update parent pointers of children
                unsigned int updateCount = 0;
                auto _tmp2 = NodePtr(this, 1);
                unsigned int updateIdx = FirstChild(_tmp2);

                while (true) {
                    // Load-bearing no-op, same mechanism as the scan loop's
                    // macro: the folded check_index(1) keeps MSVC from
                    // duplicating this header (and its real `bl check_index`)
                    // into the latch (93.5 -> 97.4; the 30th check_index the
                    // note above chased was that duplicate).
                    check_index(1);
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
            goto delete_root;
        }

        // Not root - update parent's first child
        {
            check_index(curIdx);
            check_index(curIdx);
            unsigned int parentIdx2 = Parent(curNode);
            check_index(parentIdx2);
            FirstChild(NodePtr(this, parentIdx2)) = NextSibling(curNode);

            // Update sibling count
            check_index(curIdx);
            // Retail loads the raw sibling byte here (lbz r28, 0xf(r29) at
            // 827FE6B4) and does the decrement at the point of use as a 32-bit
            // `subi r9, r28, 0x1` (827FE6E8).  Writing it as
            // `unsigned char n = SiblingCount(..) - 1` truncates the result to a
            // byte (addi 0xff / clrlwi 24), which is both two extra instructions
            // and a different value if the count is ever 0.
            unsigned int sibCountBefore = SiblingCount(curNode);
            check_index(curIdx);
            unsigned int parentIdx3 = Parent(curNode);
            check_index(parentIdx3);
            unsigned int newFirstChild = FirstChild(NodePtr(this, parentIdx3));
            check_index(newFirstChild);
            char *newFirstChildNode = NodePtr(this, newFirstChild);
            // Through an explicit POINTER to the count field: the image emits a
            // dead `addi r10, r11, 0xc` at 0x827FE6EC -- the address is
            // materialised and then immediately overwritten by the `lwz r10,
            // 0xc(r11)` at 0x827FE6F8 -- which is the signature of `&CountField(..)`
            // being taken, exactly as Trie::dec_count / dec_dup_count do in
            // trie.h.  The member-expression form never materialises it.
            unsigned int *cf = &CountField(newFirstChildNode);
            *cf = (*cf & 0xFFFFFF00) | (sibCountBefore - 1);

            delete_node(curIdx);
            return;
        }

    delete_root:
        delete_node(1);
        return;

    update_node:
        check_index(curIdx);
        curNode = NodePtr(this, curIdx);
        if (Character(curNode) == 0) {
            return;
        }
        goto loop_start;
    }

    // More than one reference, just decrement
    dec_dup_count(curIdx);
    return;
}

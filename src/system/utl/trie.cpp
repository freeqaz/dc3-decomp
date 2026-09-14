#include "utl\trie.h"

// The node-access helpers, the six inline members and the backing-store
// allocator all live in utl/trie.h now -- ham_xbox_r.map flags them `f i`
// (pick-any COMDAT = an inline definition), and only `store` and `remove`
// below are bare `f`.  The include is spelled with the search-path prefix on
// purpose: it is what makes __FILE__ inside trie.h read
// e:\lazer_build_gmc1\system\src\utl\trie.h, which is the string the shipped
// image carries.  A bare #include "trie.h" resolves out of the compiler's cwd
// and yields the basename instead.

int Trie::store(const char *str) {
    if (str == 0 || *str == 0)
        goto return_zero;

    unsigned int curIdx = 1;
    unsigned int parentIdx = 0;

    // Walk string to compute length
    const char *p = str;
    while (true) {
        unsigned char c = *p;
        p++;
        if (c == 0) break;
    }
    int strLen = (int)(p - str) - 1;
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
            unsigned int firstChildIdx;
            if ((int)parentIdx > 0) {
                check_index(parentIdx);
                firstChildIdx = FirstChild(NodePtr(this, parentIdx));
            } else {
                firstChildIdx = 1;
            }
            inc_count(firstChildIdx);
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
        unsigned int newIdx = get_free_node();
        check_index(curIdx);
        FirstChild(NodePtr(this, curIdx)) = newIdx;
        check_index(newIdx);
        char *newNode = NodePtr(this, newIdx);
        *(unsigned int *)(newNode + 0x08) = curIdx;
        char c2 = str[i];
        check_index(newIdx);
        newNode[0x10] = c2;
        inc_count(newIdx);
        curIdx = newIdx;
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

// WRONG_CALLEE row on this function (target `check_index` vs base `delete_node`):
// NOT settled as an artifact.  Emitted-callee multisets, measured 2026-09-14 with
// scripts/analysis/callee_multiset.py (objdiff JSON, both sides counted
// independently of row pairing):
//
//     delete_node   target 4  base 4   <- equal; no call is missing
//     check_index   target 29 base 30  <- WE EMIT ONE EXTRA BOUNDS CHECK
//     everything else identical (37 vs 38 calls total)
//
// The extra `check_index` is the function's ONLY count asymmetry and it is the
// accused callee itself, so this row is a live lead, not alignment noise.  The
// source has 29 textual `check_index(` sites and the object emits 30, so the
// 30th is compiler-duplicated (loop rotation peeling a call out of a latch is
// the likely shape) or comes from an inline in trie.h -- not yet run down.
//
// ⚠ An earlier revision of this note claimed "5 delete_node call sites here, 4
// in the image" and blamed retail tail-merging for the row.  That was wrong: it
// compared the image's EMITTED `bl` count against SOURCE call sites, which is
// apples-to-oranges.  Our object emits 4 as well.  The tail-merge itself is
// real (see the note at the `delete_node(1)` site) but it produces no count
// difference and does not explain this row.
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
        // NEGATIVE RESULT (w7-as, 2026-09-14): the r25/r26/r27 three-way
        // rotation (16 of the 54 residual rows) is NOT reachable by declaration
        // order.  Image: firstChildIdx=r25, prevSib=r27, traverseCount=r26;
        // ours: r27 / r26 / r25, i.e. plain descending decl order.  Moving
        // `prevSib`/`traverseCount` above `firstChildIdx` *with* their `= 0`
        // initialisers sinks both `li` to the top of the block and costs
        // 88.61 -> 86.90; moving the bare declarations up without the
        // initialisers is exactly byte-identical (a slot is claimed at the
        // first STORE, not at the declaration).
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
#ifdef HX_NATIVE
#define TRIE_ROOT_SIBLING_COUNT SiblingCount(NodePtr(this, 1))
#else
#define TRIE_ROOT_SIBLING_COUNT (*(unsigned char *)((char *)this + 0x20))
#endif

            // Retail does NOT carry `curNode` across this loop: the body
            // recomputes NodePtr from the *current* index (mulli r30,0x11 /
            // add r11,r31 at 827FE550) and reads NextSibling out of that, and
            // curNode is rebuilt from scratch after the loop at 827FE5C8.
            // Keeping a loop-carried curNode forces MSVC to rotate the loop
            // and peel the zero-trip test.
            // NEGATIVE RESULT (w7-as, 2026-09-14): writing this as a `for` with
            // the increment in the latch is byte-identical (88.61 both ways).
            // The residual here is MSVC ROTATING the loop -- it peels the
            // zero-trip test as `subic./beq` and duplicates `lbz 0x20(r31)` +
            // `subi` + `cmplw` into the latch, where the image keeps ONE
            // top-tested copy and an unconditional `b` back-edge (827FE5B4).
            while (scanCount < TRIE_ROOT_SIBLING_COUNT - 1) {
                check_index(curIdx);
                scanCount++;
                curIdx = NextSibling(NodePtr(this, curIdx));
            }
#undef TRIE_ROOT_SIBLING_COUNT

            // Retail tail-merges this call: at .L_827FE664 it emits `li r4, 0x1`
            // and branches straight to the shared `bl delete_node` at
            // .L_827FE708, skipping that path's CountField update.  Recorded
            // because it looks like a missing call in the listing and is not --
            // our build emits `delete_node` 4 times too (see the note above
            // Trie::remove for the corrected counts).
            if (curIdx == 1) {
                delete_node(1);
                return;
            }

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

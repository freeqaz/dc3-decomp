#!/usr/bin/env python3
"""Census: where does the TARGET image fold ~ObjRefConcrete's `if (mObject) Release(this)`?

An ObjRefConcrete-derived stack local (ObjPtr<T>, ObjOwnerPtr<T>, ObjPtrVec<T>::Node,
ObjPtrList<T>::Node, ObjRefConcrete<T> itself) whose destructor MSVC inlined shows up
in the split listings as, at the slot `base`:

    stw <family vtable>, base(rX)      ctor: most-derived vptr
    stw <zero>,          base+0xc(rX)  ctor: mObject = 0
    ...
    lwz r,               base+0xc(rX)  dtor: if (mObject)          <-- the test
    ... ring unlink through base+4 / base+8 ...
    stw <??_7ObjRef@@6B@>, base(rX)    dtor: base-class vptr reset

Three states are possible and all three occur:
  BOTH     test and reset emitted           -- the ordinary case, 81 slots
  NEITHER  whole destructor deleted         -- MSVC proved mObject == 0
  THIRD    reset emitted, test absent       -- 13 slots, ALL ObjPtrVec<T>::operator=

The third state is the open question behind those 13 functions (3,848 B at 87.83784):
our MSVC produces BOTH or NEITHER but never THIRD. See the comment on
ObjPtrVec<T1,T2>::operator= in src/system/obj/ObjPtr_p.h.

WHY THIS IS DATAFLOW-AWARE, AND WHY A GREP IS NOT
-------------------------------------------------
MSVC reuses one register (usually r11) for many different `addi rX, rY, "sym"@l`
materialisations inside a single function. A sweep that collects "registers that ever
held a family vtable" and then matches every `stw` of those registers attributes
unrelated stores to the slot and manufactures third-state hits. Measured: the naive
form reported 20 third-state slots; 7 of them (Character::PostLoad, Flow::PostLoad x2,
EventTrigger::Load x2, UILabel PropSync, LightPreset::Load) were register-reuse
artifacts and vanish once each register's live vtable binding is killed at its next
definition. Two looser variants of the same sweep reported 268 and 102 -- both were
mostly constructors that never destroy, and out-of-line destructor calls.

Usage:  python3 scripts/analysis/objref_dtor_fold_census.py [--asm-dir DIR] [--verbose]
"""
import argparse
import glob
import os
import re
import sys

FAMILY = re.compile(
    r'\?\?_7(?:\?\$ObjRefConcrete@|\?\$ObjPtr@|\?\$ObjOwnerPtr@|Node@\?\$ObjPtr(?:Vec|List)@)'
)
OBJREF_VTABLE = '??_7ObjRef@@6B@'
VTABLE_DEF = re.compile(r'addi (r\d+), r\d+, "(\?\?_7[^"]*)"@l')
# any instruction that writes rD, other than the stores/compares that do not
INSN_DEF = re.compile(r'^\s*/\*[^*]*\*/\s*(\w+)\s+(r\d+)')
NON_DEFINING = {'stw', 'stb', 'sth', 'stfs', 'stfd',
                'cmplw', 'cmplwi', 'cmpw', 'cmpwi', 'cmpld', 'cmpldi'}
STORE_FRAME = re.compile(r'stw (r\d+), (0x[0-9a-f]+)\((r1|r31)\)')


def scan_function(body):
    """Return (third_state_slots, ordinary_slots) for one function body."""
    live = {}      # register -> vtable symbol it currently holds
    ctor = {}      # (offset, basereg) -> family vtable stored there
    reset = set()  # (offset, basereg) that received the ObjRef base vtable
    for line in body:
        m = VTABLE_DEF.search(line)
        if m:
            live[m.group(1)] = m.group(2)
            continue
        m = INSN_DEF.match(line)
        if m and m.group(1) not in NON_DEFINING:
            live.pop(m.group(2), None)
        m = STORE_FRAME.search(line)
        if m and m.group(1) in live:
            sym = live[m.group(1)]
            slot = (int(m.group(2), 16), m.group(3))
            if FAMILY.match(sym):
                ctor[slot] = sym
            elif sym == OBJREF_VTABLE:
                reset.add(slot)

    third, ordinary = [], []
    for slot, sym in ctor.items():
        if slot not in reset:
            continue  # no inlined base-vptr reset -> dtor was outlined or absent
        off, base = slot
        stores_null = any(re.search(r'stw r\d+, 0x%x\(%s\)' % (off + 0xc, base), l)
                          for l in body)
        if not stores_null:
            continue  # not an ObjRefConcrete ctor shape
        tests = any(re.search(r'lwz r\d+, 0x%x\(%s\)' % (off + 0xc, base), l)
                    for l in body)
        (ordinary if tests else third).append((hex(off), sym))
    return third, ordinary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--asm-dir', default='build/373307D9/asm',
                    help='dtk split listings (default: build/373307D9/asm)')
    ap.add_argument('--verbose', action='store_true',
                    help='also list every ordinary slot')
    args = ap.parse_args()

    if not os.path.isdir(args.asm_dir):
        sys.exit('no such asm dir: %s (run a full `ninja` first)' % args.asm_dir)

    third_all, ord_all, nfn = [], [], 0
    for path in sorted(glob.glob(os.path.join(args.asm_dir, '**', '*.s'), recursive=True)):
        with open(path, errors='replace') as fh:
            lines = fh.read().split('\n')
        cur, body = None, []
        for line in lines + ['.endfn']:
            if line.startswith('.fn '):
                cur = line.split('"')[1] if '"' in line else line
                body = []
            elif line.startswith('.endfn'):
                if cur:
                    nfn += 1
                    t, o = scan_function(body)
                    third_all += [(cur, path, s, v) for s, v in t]
                    ord_all += [(cur, path, s, v) for s, v in o]
                cur, body = None, []
            elif cur is not None:
                body.append(line)

    total = len(third_all) + len(ord_all)
    print('functions scanned:                 %d' % nfn)
    print('family locals, inlined ctor+reset: %d' % total)
    print('  THIRD STATE (reset, no test):    %d' % len(third_all))
    for fn, path, slot, sym in sorted(third_all):
        print('     %-64s %s  %s' % (fn[:64], slot, os.path.relpath(path, args.asm_dir)))
    print('  ordinary (reset and test):       %d' % len(ord_all))
    if args.verbose:
        for fn, path, slot, sym in sorted(ord_all):
            print('     %-64s %s  %s' % (fn[:64], slot, os.path.relpath(path, args.asm_dir)))


if __name__ == '__main__':
    main()

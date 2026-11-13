#include "typeinfo"
#include "string.h"

std::bad_cast::bad_cast(const char *msg) : exception(msg) {}

std::bad_typeid::bad_typeid(const char *msg) : exception(msg) {}

std::__non_rtti_object::__non_rtti_object(const char *msg) : bad_typeid(msg) {}

// RTTI data layout (vctools/crt/.../rttidata.h).  Every offset below is
// confirmed by the target: __RTtypeid reads +0xc of the complete object
// locator, and __RTDynamicCast reads +0x4 / +0x8 / +0x10 of it and
// +0x8 / +0xc / +0x10 of the base class descriptor.
struct TypeDescriptor {
    const void *pVFTable; // 0x0
    void *spare; // 0x4
    char name[1]; // 0x8
};

struct PMD {
    int mdisp; // 0x0
    int pdisp; // 0x4
    int vdisp; // 0x8
};

struct _s_RTTIBaseClassArray;
struct _s_RTTIClassHierarchyDescriptor;

struct _s_RTTIBaseClassDescriptor {
    TypeDescriptor *pTypeDescriptor; // 0x0
    unsigned long numContainedBases; // 0x4
    PMD where; // 0x8
    unsigned long attributes; // 0x14
    // Only present when attributes & BCD_HASPCHD.  FindMI/FindVI read it at
    // 0x18 (lwz r?, 0x18(pTargetBase)) after testing that bit.
    _s_RTTIClassHierarchyDescriptor *pClassDescriptor; // 0x18
};

struct _s_RTTIBaseClassArray {
    _s_RTTIBaseClassDescriptor *arrayOfBaseClassDescriptors[1];
};

struct _s_RTTIClassHierarchyDescriptor {
    unsigned long signature; // 0x0
    unsigned long attributes; // 0x4
    unsigned long numBaseClasses; // 0x8
    _s_RTTIBaseClassArray *pBaseClassArray; // 0xc
};

struct _s_RTTICompleteObjectLocator {
    unsigned long signature; // 0x0
    unsigned long offset; // 0x4
    unsigned long cdOffset; // 0x8
    TypeDescriptor *pTypeDescriptor; // 0xc
    _s_RTTIClassHierarchyDescriptor *pClassDescriptor; // 0x10
};

#define EXCEPTION_ACCESS_VIOLATION 0xC0000005

extern "C" unsigned long __cdecl _exception_code(void);
#define GetExceptionCode() _exception_code()

// Lane history (24 lines moved): docs/decomp/notes/xdk/LIBCMT/rtti.md
// > RESIDUAL w7-ax, 80.9 canonical, 188 B -- but the BODY is instruction-for- instruction
// > identical to the image (47 of 47 real instructions equal).
extern "C" void *__RTtypeid(void *inptr) {
    if (!inptr) {
        throw std::bad_typeid("Attempted a typeid of NULL pointer!");
    }

    __try {
        const _s_RTTICompleteObjectLocator *pCompleteLocator =
            (const _s_RTTICompleteObjectLocator *)((*((void ***)inptr))[-1]);
        // Read twice on purpose -- see the note above.
        if (pCompleteLocator->pTypeDescriptor) {
            return (void *)pCompleteLocator->pTypeDescriptor;
        }
        throw std::__non_rtti_object("Bad read pointer - no RTTI data!");
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) {
        throw std::__non_rtti_object("Access violation - no RTTI data!");
    }
}

#define CHD_MULTINH 0x01
#define CHD_VIRTINH 0x02

#define BCD_NOTVISIBLE 0x01
#define BCD_AMBIGUOUS 0x02
#define BCD_PRIVORPROTBASE 0x04
#define BCD_PRIVORPROTINCOMPOBJ 0x08
#define BCD_VBOFCONTOBJ 0x10
#define BCD_NONPOLYMORPHIC 0x20
#define BCD_HASPCHD 0x40

// Two TypeDescriptors name the same type when they are the same object, or when
// their decorated names compare equal.  Both halves are inlined in the target
// (the strcmp is the /Oi byte-loop intrinsic, never a call).
#define TYPEIDS_EQ(pID1, pID2) ((pID1) == (pID2) || !strcmp((pID1)->name, (pID2)->name))

// Single-inheritance hierarchy: the base class array is a flat, depth-first
// list, so the source sub-object must appear after the target sub-object and
// no private/protected base may separate them.
const _s_RTTIBaseClassDescriptor *FindSITargetTypeInstance(
    const _s_RTTICompleteObjectLocator *pCompleteLocator, TypeDescriptor *pSrcType,
    TypeDescriptor *pTargetType
) {
    _s_RTTIBaseClassDescriptor *const *pBases =
        pCompleteLocator->pClassDescriptor->pBaseClassArray->arrayOfBaseClassDescriptors;
    unsigned long numBaseClasses = pCompleteLocator->pClassDescriptor->numBaseClasses;
    unsigned long i;

    for (i = 0; i < numBaseClasses; i++) {
        const _s_RTTIBaseClassDescriptor *pTargetBase = pBases[i];
        if (TYPEIDS_EQ(pTargetBase->pTypeDescriptor, pTargetType)) {
            unsigned long j;
            for (j = i + 1; j < numBaseClasses; j++) {
                const _s_RTTIBaseClassDescriptor *pBase = pBases[j];
                if (pBase->attributes & BCD_PRIVORPROTBASE) {
                    return 0;
                }
                if (TYPEIDS_EQ(pBase->pTypeDescriptor, pSrcType)) {
                    return pTargetBase;
                }
            }
            return 0;
        }
    }
    return 0;
}

// CRT PMDtoOffset: RetOff is assigned pdisp and then incremented, which is
// what homes it (`stw r11, 0x58(r31)`, 0x8299E300) before the vbase read
// (86.0 with pdisp read twice, 87.7 with a named pdisp local, 88.9 with this
// shape written inline in the caller).  Written inline it also puts
// pCompleteObject first in `lwzx r10, r30, r11` / `add r30, r30, r11`
// (0x8299E2F8 / 0x8299E314 want r11 first); swapping the operands in source
// is inert, only the helper-call shape restores the order.
static int PMDtoOffset(void *pThis, const PMD &pmd) {
    int RetOff = 0;
    if (pmd.pdisp >= 0) {
        RetOff = pmd.pdisp;
        RetOff += *(int *)(*(char **)((char *)pThis + RetOff) + pmd.vdisp);
    }
    RetOff += pmd.mdisp;
    return RetOff;
}

// Multiple (non-virtual) inheritance: the same type can occur more than once,
// so a candidate is only accepted when the source sub-object it was found
// alongside actually lives at SrcOffset within the complete object.
//
// Lane history (24 lines moved): docs/decomp/notes/xdk/LIBCMT/rtti.md
const _s_RTTIBaseClassDescriptor *FindMITargetTypeInstance(
    void *pCompleteObject, const _s_RTTICompleteObjectLocator *pCompleteLocator,
    TypeDescriptor *pSrcType, int SrcOffset, TypeDescriptor *pTargetType
) {
    _s_RTTIBaseClassDescriptor *const *pBases =
        pCompleteLocator->pClassDescriptor->pBaseClassArray->arrayOfBaseClassDescriptors;
    const _s_RTTIBaseClassDescriptor *pTargetBase = 0;
    const _s_RTTIBaseClassDescriptor *pSrcBase = 0;
    unsigned long numTargetContained = 0;
    unsigned long iTarget = (unsigned long)-1;
    unsigned long numBaseClasses = pCompleteLocator->pClassDescriptor->numBaseClasses;
    unsigned long i;

    for (i = 0; i < numBaseClasses; i++) {
        const _s_RTTIBaseClassDescriptor *pBase = pBases[i];

        // Skip the bases contained inside a target we already committed to.
        if (i - iTarget > numTargetContained) {
            if (TYPEIDS_EQ(pBase->pTypeDescriptor, pTargetType)) {
                if (pSrcBase) {
                    // Source came first: the cast is a base-to-derived walk.
                    if ((pBase->attributes & (BCD_NOTVISIBLE | BCD_AMBIGUOUS)) ||
                        (pSrcBase->attributes & BCD_NOTVISIBLE)) {
                        return 0;
                    }
                    return pBase;
                }
                numTargetContained = pBase->numContainedBases;
                pTargetBase = pBase;
                iTarget = i;
            }
        }

        if (TYPEIDS_EQ(pBase->pTypeDescriptor, pSrcType)) {
            if (PMDtoOffset(pCompleteObject, pBase->where) == SrcOffset) {
                if (pTargetBase) {
                    if (i - iTarget <= numTargetContained) {
                        // The source sub-object lives inside the target one.
                        if (!(pTargetBase->attributes & BCD_HASPCHD)) {
                            if (iTarget != 0) {
                                return pTargetBase;
                            }
                        } else {
                            const _s_RTTIBaseClassDescriptor *pContained =
                                pTargetBase->pClassDescriptor->pBaseClassArray
                                    ->arrayOfBaseClassDescriptors[i - iTarget];
                            return (pContained->attributes & BCD_NOTVISIBLE) ? 0 : pTargetBase;
                        }
                    } else if (pTargetBase->attributes & (BCD_NOTVISIBLE | BCD_AMBIGUOUS)) {
                        return 0;
                    }
                    if (pBase->attributes & BCD_NOTVISIBLE) {
                        return 0;
                    }
                    return pTargetBase;
                }
                pSrcBase = pBase;
            }
        }
    }
    return 0;
}

// Virtual inheritance: several distinct base class descriptors can denote the
// same sub-object, so every accepted candidate has to agree on the offset it
// resolves to; a disagreement is an ambiguous cast.
const _s_RTTIBaseClassDescriptor *FindVITargetTypeInstance(
    void *pCompleteObject, const _s_RTTICompleteObjectLocator *pCompleteLocator,
    TypeDescriptor *pSrcType, int SrcOffset, TypeDescriptor *pTargetType
) {
    _s_RTTIBaseClassDescriptor *const *pBases =
        pCompleteLocator->pClassDescriptor->pBaseClassArray->arrayOfBaseClassDescriptors;
    const _s_RTTIBaseClassDescriptor *pMatch = 0;
    const _s_RTTIBaseClassDescriptor *pSrcOutsideTarget = 0;
    const _s_RTTIBaseClassDescriptor *pVisibleTargetBase = 0;
    const _s_RTTIBaseClassDescriptor *pTargetBase = 0;
    unsigned long numTargetContained = 0;
    unsigned long numBaseClasses = pCompleteLocator->pClassDescriptor->numBaseClasses;
    unsigned long iTarget = (unsigned long)-1;
    bool isVisible = true;
    int matchOffset = -1;
    unsigned long i;

    for (i = 0; i < numBaseClasses; i++) {
        const _s_RTTIBaseClassDescriptor *pBase = pBases[i];

        if (i - iTarget > numTargetContained) {
            if (TYPEIDS_EQ(pBase->pTypeDescriptor, pTargetType)) {
                if (!(pBase->attributes & (BCD_NOTVISIBLE | BCD_AMBIGUOUS))) {
                    pVisibleTargetBase = pBase;
                }
                numTargetContained = pBase->numContainedBases;
                pTargetBase = pBase;
                iTarget = i;
            }
        }

        if (TYPEIDS_EQ(pBase->pTypeDescriptor, pSrcType)) {
            if (PMDtoOffset(pCompleteObject, pBase->where) == SrcOffset) {
                if (i - iTarget <= numTargetContained) {
                    // The source sub-object lives inside the target one.
                    if (!isVisible) {
                        continue;
                    }
                    bool isAccessible;
                    if (!(pTargetBase->attributes & BCD_HASPCHD)) {
                        if (iTarget == 0) {
                            if (pBase->attributes & BCD_NOTVISIBLE) {
                                isVisible = false;
                            }
                        }
                        isAccessible = true;
                    } else {
                        const _s_RTTIBaseClassDescriptor *pContained =
                            pTargetBase->pClassDescriptor->pBaseClassArray
                                ->arrayOfBaseClassDescriptors[i - iTarget];
                        if (pContained->attributes & BCD_NOTVISIBLE) {
                            isVisible = false;
                        }
                        isAccessible = !(pContained->attributes & BCD_PRIVORPROTBASE);
                    }
                    if (isVisible && isAccessible) {
                        int offset = PMDtoOffset(pCompleteObject, pTargetBase->where);
                        if (pMatch && matchOffset != offset) {
                            return 0;
                        }
                        pMatch = pTargetBase;
                        matchOffset = offset;
                    }
                } else if (!(pBase->attributes & (BCD_NOTVISIBLE | BCD_PRIVORPROTBASE))) {
                    // Source sub-object sits outside the last target we saw.
                    pSrcOutsideTarget = pBase;
                }
            }
        }
    }

    if (isVisible && pMatch) {
        return pMatch;
    }
    if (pSrcOutsideTarget && pVisibleTargetBase) {
        return pVisibleTargetBase;
    }
    return 0;
}

// Lane history (8 lines moved): docs/decomp/notes/xdk/LIBCMT/rtti.md
// > A single named locator in the caller (the old shape) never produced that store.
// > (w7-bu)
static void *FindCompleteObject(void **inptr) {
    const _s_RTTICompleteObjectLocator *pCompleteLocator =
        (const _s_RTTICompleteObjectLocator *)((*((void ***)inptr))[-1]);
    char *pCompleteObject = (char *)inptr - pCompleteLocator->offset;
    if (pCompleteLocator->cdOffset) {
        pCompleteObject -= *(int *)((char *)inptr - pCompleteLocator->cdOffset);
    }
    return (void *)pCompleteObject;
}

extern "C" void *__RTDynamicCast(
    void *inptr, long VfDelta, void *SrcType, void *TargetType, int isReference
) {
    void *pResult;
    const _s_RTTIBaseClassDescriptor *pBaseClass;

    if (!inptr) {
        return 0;
    }

    __try {
        void *pCompleteObject = FindCompleteObject((void **)inptr);
        const _s_RTTICompleteObjectLocator *pCompleteLocator =
            (const _s_RTTICompleteObjectLocator *)((*((void ***)inptr))[-1]);

        inptr = (char *)inptr - VfDelta;
        int myoffset = (int)((char *)inptr - (char *)pCompleteObject);

        if (!(pCompleteLocator->pClassDescriptor->attributes & CHD_MULTINH)) {
            pBaseClass = FindSITargetTypeInstance(
                pCompleteLocator, (TypeDescriptor *)SrcType, (TypeDescriptor *)TargetType
            );
        } else if (!(pCompleteLocator->pClassDescriptor->attributes & CHD_VIRTINH)) {
            pBaseClass = FindMITargetTypeInstance(
                pCompleteObject, pCompleteLocator, (TypeDescriptor *)SrcType, myoffset,
                (TypeDescriptor *)TargetType
            );
        } else {
            pBaseClass = FindVITargetTypeInstance(
                pCompleteObject, pCompleteLocator, (TypeDescriptor *)SrcType, myoffset,
                (TypeDescriptor *)TargetType
            );
        }

        if (pBaseClass) {
            // Lane history (8 lines moved): docs/decomp/notes/xdk/LIBCMT/rtti.md
            // > RESIDUAL w7-bu, 90.0 canonical: all 90 body rows match.
            pResult = (char *)pCompleteObject
                + PMDtoOffset(pCompleteObject, pBaseClass->where);
        } else {
            pResult = 0;
            if (isReference) {
                throw std::bad_cast("Bad dynamic_cast!");
            }
        }
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) {
        pResult = 0;
        throw std::__non_rtti_object("Access violation - no RTTI data!");
    }

    return pResult;
}

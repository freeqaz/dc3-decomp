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

extern "C" void *__RTtypeid(void *inptr) {
    if (!inptr) {
        throw std::bad_typeid("Attempted a typeid of NULL pointer!");
    }

    const _s_RTTICompleteObjectLocator *pCompleteLocator;
    TypeDescriptor *pTypeDescriptor;

    __try {
        pCompleteLocator = (const _s_RTTICompleteObjectLocator *)((*((void ***)inptr))[-1]);
        pTypeDescriptor = pCompleteLocator->pTypeDescriptor;
        if (pTypeDescriptor) {
            return (void *)pTypeDescriptor;
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

// Multiple (non-virtual) inheritance: the same type can occur more than once,
// so a candidate is only accepted when the source sub-object it was found
// alongside actually lives at SrcOffset within the complete object.
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
            int adjustment = 0;
            if (pBase->where.pdisp >= 0) {
                adjustment = *(int *)(*(char **)((char *)pCompleteObject + pBase->where.pdisp) +
                                      pBase->where.vdisp);
                adjustment += pBase->where.pdisp;
            }
            if (adjustment + pBase->where.mdisp == SrcOffset) {
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
            int adjustment = 0;
            if (pBase->where.pdisp >= 0) {
                adjustment = *(int *)(*(char **)((char *)pCompleteObject + pBase->where.pdisp) +
                                      pBase->where.vdisp);
                adjustment += pBase->where.pdisp;
            }
            if (pBase->where.mdisp + adjustment == SrcOffset) {
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
                        int targetAdjustment = 0;
                        if (pTargetBase->where.pdisp >= 0) {
                            targetAdjustment =
                                *(int *)(*(char **)((char *)pCompleteObject +
                                                    pTargetBase->where.pdisp) +
                                         pTargetBase->where.vdisp);
                            targetAdjustment += pTargetBase->where.pdisp;
                        }
                        int offset = pTargetBase->where.mdisp + targetAdjustment;
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

extern "C" void *__RTDynamicCast(
    void *inptr, long VfDelta, void *SrcType, void *TargetType, int isReference
) {
    void *pResult;
    const _s_RTTICompleteObjectLocator *pCompleteLocator;
    void *pCompleteObject;
    const _s_RTTIBaseClassDescriptor *pBaseClass;
    int myoffset;

    if (!inptr) {
        return 0;
    }

    __try {
        pCompleteLocator = (const _s_RTTICompleteObjectLocator *)((*((void ***)inptr))[-1]);
        pCompleteObject = (char *)inptr - pCompleteLocator->offset;
        if (pCompleteLocator->cdOffset != 0) {
            pCompleteObject =
                (char *)pCompleteObject - *(int *)((char *)inptr - pCompleteLocator->cdOffset);
        }

        char *pvfptr = (char *)inptr - VfDelta;
        myoffset = (int)(pvfptr - (char *)pCompleteObject);

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
            int adj = 0;
            if (pBaseClass->where.pdisp >= 0) {
                adj = pBaseClass->where.pdisp +
                    *(int *)(*(char **)((char *)pCompleteObject + pBaseClass->where.pdisp) +
                             pBaseClass->where.vdisp);
            }
            pResult = (char *)pCompleteObject + pBaseClass->where.mdisp + adj;
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

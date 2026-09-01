#include "typeinfo"

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

struct _s_RTTIBaseClassDescriptor {
    TypeDescriptor *pTypeDescriptor; // 0x0
    unsigned long numContainedBases; // 0x4
    PMD where; // 0x8
    unsigned long attributes; // 0x14
};

struct _s_RTTIBaseClassArray;

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

const _s_RTTIBaseClassDescriptor *FindSITargetTypeInstance(
    const _s_RTTICompleteObjectLocator *, TypeDescriptor *, TypeDescriptor *
);
const _s_RTTIBaseClassDescriptor *FindMITargetTypeInstance(
    void *, const _s_RTTICompleteObjectLocator *, TypeDescriptor *, int, TypeDescriptor *
);
const _s_RTTIBaseClassDescriptor *FindVITargetTypeInstance(
    void *, const _s_RTTICompleteObjectLocator *, TypeDescriptor *, int, TypeDescriptor *
);

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

#pragma once
#include "utl/Symbol.h"

class PhysMemTypeTracker {
public:
    PhysMemTypeTracker(Symbol);
    ~PhysMemTypeTracker();
};

int PhysicalUsage();
void PhysicalFree(void *);
int ForceLinkXMemFuncs();

void *PhysicalAllocTracked(unsigned long size, unsigned long alignment, const char *file, int line, const char *name);
void PhysicalFreeTracked(void *, const char *, int, const char *);

#pragma once
#include "obj\Data.h"
#include "utl\Symbol.h"

void ContextCheckerInit();
Symbol RandomContextSensitiveItem(const DataArray *a1, bool fail = true);

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int *_errno(void);
#define errno (*_errno())

int *_errno(void);

#define EACCES 13
#define ENOSPC 28
#define ERANGE 34

#ifdef __cplusplus
}
#endif

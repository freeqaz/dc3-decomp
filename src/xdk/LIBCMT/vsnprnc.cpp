#include <stdarg.h>
#include <stdio.h>
#include "stddef.h"
#include "errno.h"
#include "string.h"

extern "C" {

typedef unsigned int uintptr_t;
typedef int (*output_fn_t)(FILE *, const char *, void *, va_list);

void _invalid_parameter_noinfo(void);
int _flsbuf(int, FILE *);

extern int _output_s_l(FILE *, const char *, void *, va_list);

#define EINVAL 22
#define ERANGE 34
#define EOF (-1)
#define INT_MAX 0x7fffffff
#define _IOWRT 0x0002
#define _IOSTRG 0x0040

/* CRT internal: push one byte into a string-backed FILE. */
#define _putc_nolock(_c, _stm) \
    (--(_stm)->_cnt >= 0 ? 0xff & (*(_stm)->_ptr++ = (char)(_c)) : _flsbuf((_c), (_stm)))

int _vsnprintf_helper(
    output_fn_t outfn, char *string, size_t count, const char *format, void *plocinfo,
    va_list ap
) {
    FILE str = {0};
    FILE *outfile = &str;
    int retval;

    if (format == NULL) {
        errno = EINVAL;
        _invalid_parameter_noinfo();
        return -1;
    }
    if (!(count == 0 || string != NULL)) {
        errno = EINVAL;
        _invalid_parameter_noinfo();
        return -1;
    }

    if (count > INT_MAX) {
        /* old-style functions allow any large value to mean unbounded */
        outfile->_cnt = INT_MAX;
    } else {
        outfile->_cnt = (int)count;
    }
    outfile->_flag = _IOWRT | _IOSTRG;
    outfile->_ptr = outfile->_base = string;

    retval = outfn(outfile, format, plocinfo, ap);

    if (string == NULL) {
        return retval;
    }

    if ((retval >= 0) && (_putc_nolock('\0', outfile) != EOF)) {
        return retval;
    }

    string[count - 1] = '\0';

    /* -2 means the buffer was too small; -1 means some other failure */
    return outfile->_cnt < 0 ? -2 : -1;
}

int _vsprintf_s_l(char *buffer, size_t sizeInBytes, const char *format, void *locale, va_list argptr) {
    int result;
    int *err_ptr;
    int err_val;

    if (format == NULL || buffer == NULL || sizeInBytes == 0) {
        err_ptr = _errno();
        err_val = EINVAL;
    } else {
        result = _vsnprintf_helper(_output_s_l, buffer, sizeInBytes, format, locale, argptr);

        if (result < 0) {
            buffer[0] = '\0';
        }

        if (result != -2) {
            return result;
        }

        err_ptr = _errno();
        err_val = ERANGE;
    }

    *err_ptr = err_val;
    _invalid_parameter_noinfo();
    return -1;
}

int vsprintf_s(char *buffer, size_t sizeInBytes, const char *format, va_list argptr) {
    return _vsprintf_s_l(buffer, sizeInBytes, format, 0, argptr);
}

}

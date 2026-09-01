#include "wchar.h"
#include "errno.h"
#include "stdio.h"
#include "string.h"
#include "cstdarg"

extern "C" {

typedef int (*woutput_func_t)(FILE *, const wchar_t *, void *, va_list);

extern int _woutput_s_l(FILE *, const wchar_t *, void *, va_list);
extern void _invalid_parameter_noinfo(void);
extern int _flsbuf(int, FILE *);

#define EINVAL 22
#define ERANGE 34
#define EOF (-1)
#define INT_MAX 0x7fffffff
#define _IOWRT 0x0002
#define _IOSTRG 0x0040

/* CRT internal: push one byte into a string-backed FILE. */
#define _putc_nolock(_c, _stm) \
    (--(_stm)->_cnt >= 0 ? 0xff & (*(_stm)->_ptr++ = (char)(_c)) : _flsbuf((_c), (_stm)))

int _vswprintf_helper(
    woutput_func_t outfn, wchar_t *string, size_t count, const wchar_t *format,
    void *plocinfo, va_list ap
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

    outfile->_flag = _IOWRT | _IOSTRG;
    outfile->_ptr = outfile->_base = (char *)string;
    if (count > (INT_MAX / sizeof(wchar_t))) {
        /* old-style functions allow any large value to mean unbounded */
        outfile->_cnt = INT_MAX;
    } else {
        outfile->_cnt = (int)(count * sizeof(wchar_t));
    }

    retval = outfn(outfile, format, plocinfo, ap);

    if (string == NULL) {
        return retval;
    }

    /* the terminating wide NUL is two bytes, hence two _putc_nolock calls */
    if ((retval >= 0) && (_putc_nolock('\0', outfile) != EOF) &&
        (_putc_nolock('\0', outfile) != EOF)) {
        return retval;
    }

    string[count - 1] = L'\0';

    /* -2 means the buffer was too small; -1 means some other failure */
    return outfile->_cnt < 0 ? -2 : -1;
}

int _vswprintf_s_l(
    wchar_t *string, size_t sizeInWords, const wchar_t *format, void *plocinfo, va_list ap
) {
    int retvalue;

    if (format == NULL) {
        errno = EINVAL;
        _invalid_parameter_noinfo();
        return -1;
    }
    if (!(string != NULL && sizeInWords > 0)) {
        errno = EINVAL;
        _invalid_parameter_noinfo();
        return -1;
    }

    retvalue = _vswprintf_helper(_woutput_s_l, string, sizeInWords, format, plocinfo, ap);

    if (retvalue < 0) {
        string[0] = 0;
    }

    if (retvalue == -2) {
        errno = ERANGE;
        _invalid_parameter_noinfo();
        return -1;
    }

    return retvalue;
}

}

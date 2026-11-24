/* Minimal i18n stubs for standalone gbads_core (avoids linking 1.1 MB i18n_data.c). */
#include "i18n.h"

const char* tr(const char* en)
{
    return en;
}

const char* trc(const char* ctx, const char* en)
{
    (void)ctx;
    return en;
}

const char* trn(const char* one, const char* other, long n)
{
    return n == 1 ? one : other;
}

const char* trnc(const char* ctx, const char* one, const char* other, long n)
{
    (void)ctx;
    return n == 1 ? one : other;
}

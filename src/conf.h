#ifndef __PYSOL_SOUNDSERVER_CONF_H
#define __PYSOL_SOUNDSERVER_CONF_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if 0 && defined(__GNUC__)
// needed for electric-fence
extern inline char *my_strdup(const char *n)
{
    char *s = malloc(strlen(n) + 1);
    if (s)
        strcpy(s, n);
    return s;
}
#undef strdup
#define strdup  my_strdup
#endif

#if 0
#include <dmalloc.h>
#endif

#endif

/*
vi:ts=4:et
*/

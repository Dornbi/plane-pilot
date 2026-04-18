#ifndef BOOL_H
#define BOOL_H

#ifdef __OSCAR64__
#include <stdbool.h>
#else
#define bool unsigned char
#define true 1
#define false 0
#endif

#endif /* BOOL_H */
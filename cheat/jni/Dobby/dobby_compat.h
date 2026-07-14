// Dobby compat preinclude — ensures standard C headers are available
// in all Dobby translation units before any Dobby header is parsed.
// Included via -include compiler flag in Android.mk.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

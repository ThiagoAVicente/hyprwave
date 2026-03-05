#ifndef DEBUG_H
#define DEBUG_H

#include <glib.h>

extern gboolean debug_mode;

#define debug_print(...) do { if (debug_mode) g_print(__VA_ARGS__); } while (0)

#endif // DEBUG_H

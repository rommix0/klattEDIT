/* klatt_internal.h - shared between the DLL's source files; not part of the public API. */
#ifndef KLATT_INTERNAL_H
#define KLATT_INTERNAL_H

/* Clamps v to the [min, max] of parameter (kind, index); NaN becomes min. Unknown parameters pass through. */
double klatt_clamp(int kind, int index, double v);

#endif

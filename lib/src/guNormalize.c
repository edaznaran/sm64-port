#include "libultra_internal.h"

void guNormalize(f32 *x, f32 *y, f32 *z) {
    f32 tmp = rsqrtf(*x * *x + *y * *y + *z * *z);
    *x = *x * tmp;
    *y = *y * tmp;
    *z = *z * tmp;
}

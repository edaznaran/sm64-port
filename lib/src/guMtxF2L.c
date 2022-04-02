#include "libultra_internal.h"
#ifdef GBI_FLOATS
#include <string.h>
#endif

#ifndef GBI_FLOATS
void guMtxF2L(float mf[4][4], Mtx *m) {
    int r, c;
    s32 tmp1;
    s32 tmp2;
    s32 *m1 = &m->m[0][0];
    s32 *m2 = &m->m[2][0];
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 2; c++) {
            tmp1 = mf[r][2 * c] * 65536.0f;
            tmp2 = mf[r][2 * c + 1] * 65536.0f;
            *m1++ = (tmp1 & 0xffff0000) | ((tmp2 >> 0x10) & 0xffff);
            *m2++ = ((tmp1 << 0x10) & 0xffff0000) | (tmp2 & 0xffff);
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx *m) {
    int r, c;
    u32 tmp1;
    u32 tmp2;
    u32 *m1;
    u32 *m2;
    s32 stmp1, stmp2;
    m1 = (u32 *) &m->m[0][0];
    m2 = (u32 *) &m->m[2][0];
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 2; c++) {
            tmp1 = (*m1 & 0xffff0000) | ((*m2 >> 0x10) & 0xffff);
            tmp2 = ((*m1++ << 0x10) & 0xffff0000) | (*m2++ & 0xffff);
            stmp1 = *(s32 *) &tmp1;
            stmp2 = *(s32 *) &tmp2;
            mf[r][c * 2 + 0] = stmp1 / 65536.0f;
            mf[r][c * 2 + 1] = stmp2 / 65536.0f;
        }
    }
}
#else
void guMtxF2L(float mf[4][4], Mtx *m) {
#ifdef TARGET_WII_U
    f32*       const pDst = &(m->m[0][0]);
    const f32* const pSrc = &(mf[0][0]);

    // Temporary variable
    f32 v0;

    asm volatile ("psq_l  %[v0],  0(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0],  0(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0],  8(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0],  8(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 16(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 16(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 24(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 24(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 32(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 32(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 40(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 40(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 48(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 48(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_l  %[v0], 56(%[pSrc]), 0, 0" : [v0] "=f"(v0) : [pSrc] "b"(pSrc));
    asm volatile ("psq_st %[v0], 56(%[pDst]), 0, 0" : : [v0] "f"(v0), [pDst] "b"(pDst) : "memory");
#else
    memcpy(m, mf, sizeof(Mtx));
#endif // TARGET_WII_U
}
#endif

void guMtxIdentF(float mf[4][4]) {
#ifdef TARGET_WII_U
    f32* const pDst = &(mf[0][0]);

    // Constants
    register const f32 v00 = 0.0f;
    register const f32 v11 = 1.0f;
    register       f32 v01;
    register       f32 v10;

    asm volatile ("ps_merge00 %[v01], %[v00], %[v11]" : [v01] "=f"(v01) : [v00] "f"(v00), [v11] "f"(v11));
    asm volatile ("ps_merge00 %[v10], %[v11], %[v00]" : [v10] "=f"(v10) : [v00] "f"(v00), [v11] "f"(v11));

    asm volatile ("psq_st %[v10],  0(%[pDst]), 0, 0" : : [v10] "f"(v10), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v00],  8(%[pDst]), 0, 0" : : [v00] "f"(v00), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v01], 16(%[pDst]), 0, 0" : : [v01] "f"(v01), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v00], 24(%[pDst]), 0, 0" : : [v00] "f"(v00), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v00], 32(%[pDst]), 0, 0" : : [v00] "f"(v00), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v10], 40(%[pDst]), 0, 0" : : [v10] "f"(v10), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v00], 48(%[pDst]), 0, 0" : : [v00] "f"(v00), [pDst] "b"(pDst) : "memory");
    asm volatile ("psq_st %[v01], 56(%[pDst]), 0, 0" : : [v01] "f"(v01), [pDst] "b"(pDst) : "memory");
#else
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (r == c) {
                mf[r][c] = 1.0f;
            } else {
                mf[r][c] = 0.0f;
            }
        }
    }
#endif // TARGET_WII_U
}

void guMtxIdent(Mtx *m) {
#ifndef GBI_FLOATS
    float mf[4][4];
    guMtxIdentF(mf);
    guMtxF2L(mf, m);
#else
    guMtxIdentF(m->m);
#endif
}

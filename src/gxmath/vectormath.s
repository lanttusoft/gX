.intel_syntax noprefix
# vectormath.s — x86-64 SSE optimized vector/matrix operations
# Ported from game3 gamemath (32-bit) to 64-bit with GAS Intel syntax.
# opencode: ported and rewritten for x86-64

# Calling convention: System V AMD64 (Linux)
#   Result in xmm0, arguments in xmm0-xmm7

.text

# gX_VM_AddV — add two 4D vectors
#   xmm0 = v1, xmm1 = v2 -> xmm0
.global gX_VM_AddV
gX_VM_AddV:
    addps xmm0, xmm1
    ret

# gX_VM_AddM — add two 4x4 matrices
#   xmm0-xmm3 = A rows, xmm4-xmm7 = B rows -> xmm0-xmm3
.global gX_VM_AddM
gX_VM_AddM:
    addps xmm0, xmm4
    addps xmm1, xmm5
    addps xmm2, xmm6
    addps xmm3, xmm7
    ret

# gX_VM_TransposeM — transpose 4x4 matrix
#   xmm0-xmm3 = 4 rows -> xmm0-xmm3 = 4 columns
# Uses standard SSE transpose idiom via unpcklps/unpckhps.
.global gX_VM_TransposeM
gX_VM_TransposeM:
    movaps    xmm4, xmm0
    unpcklps  xmm4, xmm1          # xmm4 = [a0 b0 a1 b1]
    unpckhps  xmm0, xmm1          # xmm0 = [a2 b2 a3 b3]
    movaps    xmm5, xmm2
    unpcklps  xmm5, xmm3          # xmm5 = [c0 d0 c1 d1]
    unpckhps  xmm2, xmm3          # xmm2 = [c2 d2 c3 d3]
    movaps    xmm1, xmm4
    unpcklps  xmm1, xmm5          # xmm1 = [a0 b0 c0 d0]
    unpckhps  xmm4, xmm5          # xmm4 = [a1 b1 c1 d1]
    movaps    xmm3, xmm0
    unpcklps  xmm3, xmm2          # xmm3 = [a2 b2 c2 d2]
    unpckhps  xmm0, xmm2          # xmm0 = [a3 b3 c3 d3]
    movaps    xmm2, xmm0
    movaps    xmm0, xmm1
    movaps    xmm1, xmm4
    ret

# gX_VM_MulM — multiply two 4x4 matrices
#   xmm0-xmm3 = A rows, xmm4-xmm7 = B rows -> xmm0-xmm3
# Transposes B first for column access, then does row*col dot products via dpps.
.global gX_VM_MulM
gX_VM_MulM:
    movaps    xmm8, xmm4
    movaps    xmm9, xmm5
    movaps    xmm10, xmm6
    movaps    xmm11, xmm7

    # Transpose B (rows xmm8-xmm11 -> columns xmm9,xmm13,xmm10,xmm14)
    movaps    xmm12, xmm8
    unpcklps  xmm12, xmm9
    unpckhps  xmm8, xmm9
    movaps    xmm13, xmm10
    unpcklps  xmm13, xmm11
    unpckhps  xmm10, xmm11
    movaps    xmm9, xmm12
    unpcklps  xmm9, xmm13
    unpckhps  xmm12, xmm13
    movaps    xmm11, xmm8
    unpcklps  xmm11, xmm10
    unpckhps  xmm8, xmm10
    movaps    xmm13, xmm12
    movaps    xmm10, xmm11
    movaps    xmm14, xmm8

    # Row 0: dot(A_row0, B_col[0..3])
    movaps    xmm4, xmm0
    dpps      xmm4, xmm9, 0xF1
    movaps    xmm5, xmm0
    dpps      xmm5, xmm13, 0xF1
    movaps    xmm6, xmm0
    dpps      xmm6, xmm10, 0xF1
    movaps    xmm7, xmm0
    dpps      xmm7, xmm14, 0xF1
    unpcklps  xmm4, xmm5
    unpcklps  xmm6, xmm7
    movaps    xmm0, xmm4
    unpcklps  xmm0, xmm6

    # Row 1
    movaps    xmm4, xmm1
    dpps      xmm4, xmm9, 0xF1
    movaps    xmm5, xmm1
    dpps      xmm5, xmm13, 0xF1
    movaps    xmm6, xmm1
    dpps      xmm6, xmm10, 0xF1
    movaps    xmm7, xmm1
    dpps      xmm7, xmm14, 0xF1
    unpcklps  xmm4, xmm5
    unpcklps  xmm6, xmm7
    movaps    xmm1, xmm4
    unpcklps  xmm1, xmm6

    # Row 2
    movaps    xmm4, xmm2
    dpps      xmm4, xmm9, 0xF1
    movaps    xmm5, xmm2
    dpps      xmm5, xmm13, 0xF1
    movaps    xmm6, xmm2
    dpps      xmm6, xmm10, 0xF1
    movaps    xmm7, xmm2
    dpps      xmm7, xmm14, 0xF1
    unpcklps  xmm4, xmm5
    unpcklps  xmm6, xmm7
    movaps    xmm2, xmm4
    unpcklps  xmm2, xmm6

    # Row 3
    movaps    xmm4, xmm3
    dpps      xmm4, xmm9, 0xF1
    movaps    xmm5, xmm3
    dpps      xmm5, xmm13, 0xF1
    movaps    xmm6, xmm3
    dpps      xmm6, xmm10, 0xF1
    movaps    xmm7, xmm3
    dpps      xmm7, xmm14, 0xF1
    unpcklps  xmm4, xmm5
    unpcklps  xmm6, xmm7
    movaps    xmm3, xmm4
    unpcklps  xmm3, xmm6
    ret

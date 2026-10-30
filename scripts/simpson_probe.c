//ai: standalone native witness; compile the actual gxmath translation units, not copied quadrature.
#include "../src/gxmath/calculus.h"
#include <stdio.h>

//ai: bounded probe coefficients are supplied by the Python contract tests.
static gX_F32 Coefficients[4];

//ai: finite cubic callback for degree-zero through degree-three witnesses.
static gX_F32 gX_SimpsonProbe_Cubic(gX_F32 X)
{
    return Coefficients[0] * X * X * X + Coefficients[1] * X * X + Coefficients[2] * X + Coefficients[3];
}

//ai: wrapper-only stdio protocol; the production implementation remains unchanged.
gX_I32 main(void)
{
    gX_U32 Steps;
    gX_F32 Low;
    gX_F32 High;
    gX_I32 Read;
    while ((Read = scanf("%f %f %f %f %u %f %f", &Coefficients[0], &Coefficients[1],
        &Coefficients[2], &Coefficients[3], &Steps, &Low, &High)) != EOF)
    {
        if (Read != 7) return 2;
        gX_F32 Value = gX_F32_Calculus_Simpson(gX_SimpsonProbe_Cubic, Steps, Low, High);
        printf("SIMPSON %a\n", (gX_F64)Value);
    }
    printf("SIMPSON_NULL %a\n", (gX_F64)gX_F32_Calculus_Simpson(NULL, 2, 0.0f, 1.0f));
    return 0;
}

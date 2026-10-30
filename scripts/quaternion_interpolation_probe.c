//ai: bounded probe that invokes actual gxmath Lerp/Nlerp/Slerp translation units.
#include "../src/gxmath/quaternion.h"
#include <stdio.h>

//ai: hexadecimal output preserves native float results for the Python witness.
static void gX_QuaternionInterpolationProbe_Print(gX_F32Qua Q)
{
    printf(" %a %a %a %a", (gX_F64)Q.x, (gX_F64)Q.y, (gX_F64)Q.z, (gX_F64)Q.w);
}

//ai: input consists only of bounded test fixtures; production interpolation is not reimplemented here.
gX_I32 main(void)
{
    gX_F32Qua Q1;
    gX_F32Qua Q2;
    gX_F32 Amount;
    gX_I32 Read;
    while ((Read = scanf("%f %f %f %f %f %f %f %f %f",
        &Q1.x, &Q1.y, &Q1.z, &Q1.w, &Q2.x, &Q2.y, &Q2.z, &Q2.w, &Amount)) != EOF)
    {
        if (Read != 9) return 2;
        printf("INTERPOLATION");
        gX_QuaternionInterpolationProbe_Print(gX_F32Qua_Lerp(Q1, Q2, Amount));
        gX_QuaternionInterpolationProbe_Print(gX_F32Qua_Nlerp(Q1, Q2, Amount));
        gX_QuaternionInterpolationProbe_Print(gX_F32Qua_Slerp(Q1, Q2, Amount));
        printf("\n");
    }
    return 0;
}

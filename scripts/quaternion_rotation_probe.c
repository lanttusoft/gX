//ai: standalone witness invoking actual gxmath rotation, inverse, product and matrix helpers.
#include "../src/gxmath/quaternion.h"
#include "../src/gxmath/vector3.h"
#include <stdio.h>

//ai: wrapper-only hex-float output preserves the native float results.
static void gX_QuaternionProbe_Print(gX_F32v3 V)
{
    printf(" %a %a %a", (gX_F64)V.x, (gX_F64)V.y, (gX_F64)V.z);
}

//ai: input is supplied only by bounded contract fixtures; no production behavior is reimplemented.
gX_I32 main(void)
{
    gX_F32Qua Q;
    gX_F32Qua R;
    gX_F32v3 V;
    gX_I32 Read;
    while ((Read = scanf("%f %f %f %f %f %f %f %f %f %f %f",
        &Q.x, &Q.y, &Q.z, &Q.w, &R.x, &R.y, &R.z, &R.w, &V.x, &V.y, &V.z)) != EOF)
    {
        if (Read != 11) return 2;
        gX_F32Qua Negative = {-Q.x, -Q.y, -Q.z, -Q.w};
        gX_F32Qua Inverse = gX_F32Qua_Invert(Q);
        gX_F32v3 Rotated = gX_F32v3_RotateByQuaternion(V, Q);
        printf("QUATERNION");
        gX_QuaternionProbe_Print(Rotated);
        gX_QuaternionProbe_Print(gX_F32v3_RotateByQuaternion(V, Negative));
        gX_QuaternionProbe_Print(gX_F32v3_RotateByQuaternion(Rotated, Inverse));
        gX_QuaternionProbe_Print(gX_F32v3_RotateByQuaternion(gX_F32v3_RotateByQuaternion(V, Inverse), Q));
        gX_QuaternionProbe_Print(gX_F32v3_Transform(V, gX_F32Qua_ToMatrix(Q)));
        gX_QuaternionProbe_Print(gX_F32v3_Transform(V, gX_F32Qua_ToMatrix(Negative)));
        gX_QuaternionProbe_Print(gX_F32v3_RotateByQuaternion(V, gX_F32Qua_Multiply(Q, R)));
        gX_QuaternionProbe_Print(gX_F32v3_RotateByQuaternion(gX_F32v3_RotateByQuaternion(V, R), Q));
        printf("\n");
    }
    return 0;
}

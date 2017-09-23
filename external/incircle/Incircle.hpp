#pragma once

extern "C"
{
    float orient2d(float *pa, float *pb, float *pc);
    float orient2dfast(float *pa, float *pb, float *pc);
    float orient3d(float *pa, float *pb, float *pc, float *pd);
    float orient3dfast(float *pa, float *pb, float *pc, float *pd);
    float incircle(float *pa, float *pb, float *pc, float *pd);
    float incirclefast(float *pa, float *pb, float *pc, float *pd);
    float insphere(float *pa, float *pb, float *pc, float *pd, float *pe);
    float inspherefast(float *pa, float *pb, float *pc, float *pd, float *pe);
}

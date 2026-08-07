#include "fastmath.h"

#define FM_INV_LN2 1.44269504088896341f   // 1 / ln2

#define FM_LN2_HI  0.693359375f          // 355/512
#define FM_LN2_LO  -2.12194440e-4f       // ln2 - FM_LN2_HI

static float fm_exp2i(int k)
{
    union
    {
        uint32_t u;
        float f;
    } v;

    if (k < -126)
    {
        return 0.0f;
    }
    if (k > 127)
    {
        return 3.4028235e38f;   // saturate at FLT_MAX rather than produce inf
    }

    v.u = (uint32_t)(k + 127) << 23;
    return v.f;
}

float fm_expf(float x)
{
    int k;
    float r, p;

    if (x > 88.0f)
    {
        return 3.4028235e38f;
    }
    if (x < -88.0f)
    {
        return 0.0f;
    }

    k = (int)(x * FM_INV_LN2 + (x >= 0.0f ? 0.5f : -0.5f));

    r = x - (float)k * FM_LN2_HI;
    r = r - (float)k * FM_LN2_LO;
    
    // taylor series for exp(r) around r=0, truncated at r^7 term
    p = 1.0f / 720.0f;
    p = 1.0f / 120.0f + r * p;
    p = 1.0f / 24.0f + r * p;
    p = 1.0f / 6.0f + r * p;
    p = 0.5f + r * p;
    p = 1.0f + r * p;
    p = 1.0f + r * p;

    return p * fm_exp2i(k);
}

float fm_sigmoidf(float z)
{
    if (z >= 0.0f)
    {
        return 1.0f / (1.0f + fm_expf(-z));
    }
    else
    {
        float e = fm_expf(z);
        return e / (1.0f + e);
    }
}

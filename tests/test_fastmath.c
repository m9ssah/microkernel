#include <math.h>
#include <stdio.h>

#include "../firmware/fastmath.h"

static int failures;

#define MAX_REPORTED 8
static void report(const char *fmt, double a, double b, double c, double d)
{
    if (failures < MAX_REPORTED)
    {
        printf(fmt, a, b, c, d);
    }
    else if (failures == MAX_REPORTED)
    {
        printf("further failures suppressed\n");
    }
    failures++;
}


static double rel_err(double got, double want)
{
    if (fabs(want) < 1e-30)
    {
        return fabs(got - want);
    }
    return fabs((got - want) / want);
}

static void check_exp(float x, double tol)
{
    double got = fm_expf(x);
    double want = exp((double)x);
    double e = rel_err(got, want);

    if (e > tol)
    {
        report("  FAIL fm_expf(%.6f) = %.9g, want %.9g, rel err %.3e\n",
               x, got, want, e);
    }
}

static void check_sigmoid(float z, double tol)
{
    double got = fm_sigmoidf(z);
    double want = 1.0 / (1.0 + exp(-(double)z));
    double e = rel_err(got, want);

    if (e > tol)
    {
        report("  FAIL fm_sigmoidf(%.6f) = %.9g, want %.9g, rel err %.3e\n",
               z, got, want, e);
    }
}

int main(void)
{
    const double TOL = 4e-7;

    double worst_exp = 0.0, worst_sig = 0.0;

    printf("fastmath: comparing firmware/fastmath.c against libm\n");

    for (double x = -20.0; x <= 20.0; x += 0.0007)
    {
        float xf = (float)x;

        check_exp(xf, TOL);
        check_sigmoid(xf, TOL);

        double e1 = rel_err(fm_expf(xf), exp((double)xf));
        double e2 = rel_err(fm_sigmoidf(xf), 1.0 / (1.0 + exp(-(double)xf)));
        if (e1 > worst_exp) worst_exp = e1;
        if (e2 > worst_sig) worst_sig = e2;
    }

    for (int k = -40; k <= 40; k++)
    {
        double centre = k * 0.69314718055994531;
        check_exp((float)(centre - 1e-4), TOL);
        check_exp((float)centre, TOL);
        check_exp((float)(centre + 1e-4), TOL);
    }

    if (isnan(fm_expf(1000.0f)) || isinf(fm_expf(1000.0f)))
    {
        printf("  FAIL fm_expf(1000) is not finite\n");
        failures++;
    }
    if (fm_expf(-1000.0f) != 0.0f)
    {
        printf("  FAIL fm_expf(-1000) = %g, want 0\n", (double)fm_expf(-1000.0f));
        failures++;
    }
    if (fm_sigmoidf(-1000.0f) < 0.0f || fm_sigmoidf(1000.0f) > 1.0f)
    {
        printf("  FAIL fm_sigmoidf left [0,1] at the extremes\n");
        failures++;
    }

    printf("  worst relative error: fm_expf %.3e, fm_sigmoidf %.3e\n",
           worst_exp, worst_sig);

    if (failures)
    {
        printf("fastmath: %d FAILURES\n", failures);
        return 1;
    }
    printf("fastmath: OK\n");
    return 0;
}

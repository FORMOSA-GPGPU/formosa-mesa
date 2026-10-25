#include "libm.h"

static float
inf_f(uint32_t sign)
{
   return sign ? -INFINITY : INFINITY;
}

static double
inf_d(uint32_t sign)
{
   return sign ? -INFINITY : INFINITY;
}

/* Helper to generate signed zero */
static float
zero_f(uint32_t sign)
{
   return sign ? -0.0f : 0.0f;
}

static double
zero_d(uint32_t sign)
{
   return sign ? -0.0 : 0.0;
}

float
__math_xflowf(uint32_t sign, float y)
{
   return y;
}

float
__math_uflowf(uint32_t sign)
{
   return zero_f(sign);
}

float
__math_oflowf(uint32_t sign)
{
   return inf_f(sign);
}

float
__math_divzerof(uint32_t sign)
{
   return inf_f(sign);
}

float
__math_invalidf(float x)
{
   return NAN;
}

double
__math_xflow(uint32_t sign, double y)
{
   return y;
}

double
__math_uflow(uint32_t sign)
{
   return zero_d(sign);
}

double
__math_oflow(uint32_t sign)
{
   return inf_d(sign);
}

double
__math_divzero(uint32_t sign)
{
   return inf_d(sign);
}

double
__math_invalid(double x)
{
   return NAN;
}
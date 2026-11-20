#include "natives_internal.h"
#include <Arduino.h>
#include <math.h>

static inline float dpop_f() {
    uint32_t bits = (uint32_t)dpop();
    float f;
    memcpy(&f, &bits, sizeof(float));
    return f;
}

static inline void dpush_f(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(float));
    dpush((DCELL)(int32_t)bits);
}

// f+ ( f1 f2 -- f3 )
static void fn_f_add() {
    float b = dpop_f();
    float a = dpop_f();
    dpush_f(a + b);
}

// f- ( f1 f2 -- f3 )
static void fn_f_sub() {
    float b = dpop_f();
    float a = dpop_f();
    dpush_f(a - b);
}

// f* ( f1 f2 -- f3 )
static void fn_f_mul() {
    float b = dpop_f();
    float a = dpop_f();
    dpush_f(a * b);
}

// f/ ( f1 f2 -- f3 )
static void fn_f_div() {
    float b = dpop_f();
    float a = dpop_f();
    if (b == 0.0f) { forth_output("ERR: div by zero\n"); dpush_f(0.0f); return; }
    dpush_f(a / b);
}

// f. ( f -- )
static void fn_f_dot() {
    float f = dpop_f();
    char buf[32];
    snprintf(buf, sizeof(buf), "%g ", f);
    forth_output(buf);
}

// fsqrt ( f -- res )
static void fn_f_sqrt() {
    float f = dpop_f();
    if (f < 0.0f) { forth_output("ERR: sqrt negative\n"); dpush_f(0.0f); return; }
    dpush_f(sqrtf(f));
}

// fsin ( f -- res )
static void fn_f_sin() {
    dpush_f(sinf(dpop_f()));
}

// fcos ( f -- res )
static void fn_f_cos() {
    dpush_f(cosf(dpop_f()));
}

// ftan ( f -- res )
static void fn_f_tan() {
    dpush_f(tanf(dpop_f()));
}

// fatan2 ( y x -- angle )
static void fn_f_atan2() {
    float x = dpop_f();
    float y = dpop_f();
    dpush_f(atan2f(y, x));
}

// fexp ( f -- res )
static void fn_f_exp() {
    dpush_f(expf(dpop_f()));
}

// fln ( f -- res )
static void fn_f_ln() {
    float f = dpop_f();
    if (f <= 0.0f) { forth_output("ERR: ln non-positive\n"); dpush_f(0.0f); return; }
    dpush_f(logf(f));
}

// fpow ( base exp -- res )
static void fn_f_pow() {
    float exp_val = dpop_f();
    float base = dpop_f();
    dpush_f(powf(base, exp_val));
}

// Conversion Bridges
// u>f ( u -- f )
static void fn_u_to_f() {
    DCELL u = dpop();
    dpush_f((float)((double)u / 1000000.0));
}

// f>u ( f -- u )
static void fn_f_to_u() {
    float f = dpop_f();
    dpush((DCELL)((double)f * 1000000.0));
}

// s>f ( n -- f )
static void fn_s_to_f() {
    DCELL n = dpop();
    dpush_f((float)n);
}

// f>s ( f -- n )
static void fn_f_to_s() {
    float f = dpop_f();
    dpush((DCELL)f);
}

void float_bindings() {
    forth_register("f+", fn_f_add);
    forth_register("f-", fn_f_sub);
    forth_register("f*", fn_f_mul);
    forth_register("f/", fn_f_div);
    forth_register("f.", fn_f_dot);
    forth_register("fsqrt", fn_f_sqrt);
    forth_register("fsin", fn_f_sin);
    forth_register("fcos", fn_f_cos);
    forth_register("ftan", fn_f_tan);
    forth_register("fatan2", fn_f_atan2);
    forth_register("fexp", fn_f_exp);
    forth_register("fln", fn_f_ln);
    forth_register("fpow", fn_f_pow);

    // Bridges
    forth_register("u>f", fn_u_to_f);
    forth_register("f>u", fn_f_to_u);
    forth_register("s>f", fn_s_to_f);
    forth_register("f>s", fn_f_to_s);
}

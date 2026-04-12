#include "math.h"
#include "natives.h"
#include "../uforth.h"
#include "../uforth-config.h"
#include <math.h>
#include <cstdio>

// Fixed-point Q32.32 helpers
static inline double fp_to_double(DCELL x) { return (double)x / (double)FIXED_PT_MULT; }
static inline DCELL  double_to_fp(double x) { return (DCELL)(x * (double)FIXED_PT_MULT); }

// f* ( a b -- a*b )   fixed-point multiply
static void fn_fmul() {
    DCELL b = dpop();
    DCELL a = dpop();
    dpush(double_to_fp(fp_to_double(a) * fp_to_double(b)));
}

// f/ ( a b -- a/b )   fixed-point divide
static void fn_fdiv() {
    DCELL b = dpop();
    DCELL a = dpop();
    if (b == 0) { forth_output("f/: division by zero "); return; }
    dpush(double_to_fp(fp_to_double(a) / fp_to_double(b)));
}

// f. ( f -- )   print fixed-point as decimal
static void fn_fdot() {
    DCELL x = dpop();
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g ", fp_to_double(x));
    forth_output(buf);
}

// f>d ( f -- d )   fixed-point → integer (truncated)
static void fn_ftod() {
    DCELL x = dpop();
    dpush((DCELL)(x >> 32));
}

// d>f ( d -- f )   integer → fixed-point
static void fn_dtof() {
    DCELL x = dpop();
    dpush(x << 32);
}

// fpi ( -- f )
static void fn_fpi() { dpush(double_to_fp(M_PI)); }

// fe  ( -- f )
static void fn_fe()  { dpush(double_to_fp(M_E));  }

// fpow ( base exp -- f )   both fixed-point
static void fn_fpow() {
    DCELL exp  = dpop();
    DCELL base = dpop();
    dpush(double_to_fp(pow(fp_to_double(base), fp_to_double(exp))));
}

// fln  ( f -- f )   natural log
static void fn_fln() {
    DCELL x = dpop();
    dpush(double_to_fp(log(fp_to_double(x))));
}

// flog ( f -- f )   log base 10
static void fn_flog() {
    DCELL x = dpop();
    dpush(double_to_fp(log10(fp_to_double(x))));
}

// flog2 ( f -- f )   log base 2
static void fn_flog2() {
    DCELL x = dpop();
    dpush(double_to_fp(log2(fp_to_double(x))));
}

void forth_register_math() {
    forth_register("f*",     fn_fmul);
    forth_register("f/",     fn_fdiv);
    forth_register("f.",     fn_fdot);
    forth_register("f>d",    fn_ftod);
    forth_register("d>f",    fn_dtof);
    forth_register("fpi",    fn_fpi);
    forth_register("fe",     fn_fe);
    forth_register("fpow",   fn_fpow);
    forth_register("fln",    fn_fln);
    forth_register("flog",   fn_flog);
    forth_register("flog2",  fn_flog2);
}

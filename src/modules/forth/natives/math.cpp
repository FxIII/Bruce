#include "natives_internal.h"
#include <Arduino.h>
#include <math.h>

#define CORDIC_N 22
#define CORDIC_SCALE_BITS 30
#define CORDIC_K_SCALED 652032874LL // 0.607252935 * 2^30 for N=22
#define PI_MICRO 3141593LL
#define TWO_PI_MICRO 6283185LL

// Angles in Binary Angular Measurement (BAM): round((atan(2^-i) / pi) * 2^31)
static const int32_t cordic_angles_bam[CORDIC_N] = {
    536870912, 316933406, 167458907, 85004756,
    42667331,  21354465,  10679838,  5340245,
    2670163,   1335087,   667544,    333772,
    166886,    83443,     41722,     20861,
    10430,     5215,      2608,      1304,
    652,       326
};

static void cordic_sincos(int64_t angle_micro, int64_t *out_sin, int64_t *out_cos) {
    if (angle_micro == 0) {
        *out_sin = 0;
        *out_cos = 1000000LL;
        return;
    }

    // Convert micro-radians to BAM: z = (angle_micro * 2^31) / 3141593
    int64_t z = (angle_micro * 2147483648LL) / PI_MICRO;

    // Wrap z into [-2^31, 2^31)
    int64_t two31 = 1LL << 31;
    int64_t two32 = 1LL << 32;
    z = (z + two31) % two32;
    if (z < 0) z += two32;
    z -= two31;

    int64_t x = CORDIC_K_SCALED;
    int64_t y = 0;

    int64_t one30 = 1LL << 30;
    if (z > one30) {
        x = -CORDIC_K_SCALED;
        z -= two31;
    } else if (z < -one30) {
        x = -CORDIC_K_SCALED;
        z += two31;
    }

    for (int i = 0; i < CORDIC_N; i++) {
        int64_t d = (z < 0) ? -1 : 1;
        int64_t x_new = x - d * (y >> i);
        int64_t y_new = y + d * (x >> i);
        z -= d * cordic_angles_bam[i];
        x = x_new;
        y = y_new;
    }

    // Convert back from 2^30 scale to 10^6 micro scale with proper rounding
    *out_sin = (y * 1000000LL + (1LL << 29)) >> CORDIC_SCALE_BITS;
    *out_cos = (x * 1000000LL + (1LL << 29)) >> CORDIC_SCALE_BITS;
}

static int64_t cordic_atan2(int64_t y, int64_t x) {
    if (x == 0 && y == 0) return 0;
    int64_t z = 0;
    int64_t two31 = 1LL << 31;

    if (x < 0) {
        if (y >= 0) {
            x = -x; y = -y; z = two31;
        } else {
            x = -x; y = -y; z = -two31;
        }
    }

    for (int i = 0; i < CORDIC_N; i++) {
        int64_t d = (y >= 0) ? 1 : -1;
        int64_t x_new = x + d * (y >> i);
        int64_t y_new = y - d * (x >> i);
        z += d * cordic_angles_bam[i];
        x = x_new;
        y = y_new;
    }

    return (z * PI_MICRO) / two31;
}

// usqrt ( u -- sqrt_u )
static void fn_usqrt() {
    DCELL u = dpop();
    if (u < 0) { forth_output("ERR: sqrt negative\n"); dpush(0); return; }
    double val = (double)u / 1000000.0;
    double res = sqrt(val);
    dpush((DCELL)(res * 1000000.0 + 0.5));
}

// usin ( angle -- sin )
static void fn_usin() {
    DCELL angle = dpop();
    int64_t s, c;
    cordic_sincos(angle, &s, &c);
    dpush(s);
}

// ucos ( angle -- cos )
static void fn_ucos() {
    DCELL angle = dpop();
    int64_t s, c;
    cordic_sincos(angle, &s, &c);
    dpush(c);
}

// utan ( angle -- tan )
static void fn_utan() {
    DCELL angle = dpop();
    int64_t s, c;
    cordic_sincos(angle, &s, &c);
    if (c == 0) { forth_output("ERR: tan undefined\n"); dpush(0); return; }
    int64_t prod;
    if (!__builtin_mul_overflow(s, 1000000LL, &prod)) {
        dpush(prod / c);
    } else {
        double res = ((double)s * 1000000.0) / (double)c;
        dpush((DCELL)res);
    }
}

// uatan2 ( y x -- angle )
static void fn_uatan2() {
    DCELL x = dpop();
    DCELL y = dpop();
    dpush(cordic_atan2(y, x));
}

// uln ( u -- ln_u )
static void fn_uln() {
    DCELL u = dpop();
    if (u <= 0) { forth_output("ERR: ln non-positive\n"); dpush(0); return; }
    double val = (double)u / 1000000.0;
    double res = log(val);
    dpush((DCELL)(res * 1000000.0));
}

// uexp ( u -- exp_u )
static void fn_uexp() {
    DCELL u = dpop();
    double val = (double)u / 1000000.0;
    double res = exp(val);
    dpush((DCELL)(res * 1000000.0));
}

// upow ( base exp -- result )
static void fn_upow() {
    DCELL exp_val = dpop();
    DCELL base = dpop();
    if (base <= 0) { dpush(0); return; }
    double b = (double)base / 1000000.0;
    double e = (double)exp_val / 1000000.0;
    double res = pow(b, e);
    dpush((DCELL)(res * 1000000.0));
}

void math_bindings() {
    forth_register("usqrt", fn_usqrt);
    forth_register("usin", fn_usin);
    forth_register("ucos", fn_ucos);
    forth_register("utan", fn_utan);
    forth_register("uatan2", fn_uatan2);
    forth_register("uln", fn_uln);
    forth_register("uexp", fn_uexp);
    forth_register("upow", fn_upow);
}

// Bit-exactness test: LENS FixedMul/FixedDiv vs the original 64-bit versions.
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int fixed_t;
fixed_t FixedMul(fixed_t a, fixed_t b);
fixed_t FixedDiv(fixed_t a, fixed_t b);
static fixed_t RefMul(fixed_t a, fixed_t b) { return (fixed_t)(((int64_t)a * (int64_t)b) >> 16); }
static fixed_t RefDiv(fixed_t a, fixed_t b) {
    if ((abs(a) >> 14) >= abs(b)) return (a ^ b) < 0 ? INT_MIN : INT_MAX;
    return (fixed_t)(((int64_t)a << 16) / b);
}
static uint64_t s = 88172645463325252ull;
static int32_t rnd(void) {  // mix of full-range and small values
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    int32_t v = (int32_t)s;
    switch ((s >> 40) & 3) { case 0: return v; case 1: return v >> 8; case 2: return v >> 16; default: return v >> 24; }
}
int main(void) {
    const int32_t edge[] = {0, 1, -1, 65536, -65536, 65535, -65535, INT_MAX, INT_MIN + 1, 32768, -32768, 3, -3, 0x7fff0000, -0x7fff0000};
    long n = 0, bad = 0;
    for (unsigned i = 0; i < sizeof(edge) / 4; i++)
        for (unsigned j = 0; j < sizeof(edge) / 4; j++) {
            int32_t a = edge[i], b = edge[j];
            n++; if (FixedMul(a, b) != RefMul(a, b)) { if (bad++ < 5) printf("mul %d %d\n", a, b); }
            if (b) { n++; if (FixedDiv(a, b) != RefDiv(a, b)) { if (bad++ < 5) printf("div %d %d\n", a, b); } }
        }
    for (long k = 0; k < 20000000; k++) {
        int32_t a = rnd(), b = rnd();
        if (a == INT_MIN) a++;
        n++; if (FixedMul(a, b) != RefMul(a, b)) { if (bad++ < 5) printf("mul %d %d: %d vs %d\n", a, b, FixedMul(a, b), RefMul(a, b)); }
        if (b && b != INT_MIN) { n++; if (FixedDiv(a, b) != RefDiv(a, b)) { if (bad++ < 5) printf("div %d %d: %d vs %d\n", a, b, FixedDiv(a, b), RefDiv(a, b)); } }
    }
    printf("test_fixed: %ld cases, %ld mismatches\n", n, bad);
    return bad != 0;
}

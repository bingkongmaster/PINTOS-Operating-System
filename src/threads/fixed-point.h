// Defines all macros supporting fixed-point real arithmetic.
// Used for implementing BSD4.4 scheduler.


#define FRACTION 1 << 14

#define FP_INT_ADD(x, n) (x) + (n) * (FRACTION)
#define FP_FP_MUL(x, y) ((int64_t)(x)) * (y) / (FRACTION)
#define FP_FP_DIV(x, y) ((int64_t)(x)) * (FRACTION) / (y)
#define INT2FP(x) (x) * (FRACTION)
#define FP2INT_NEAREST(x) ((x) >= 0 ? ((x) + (FRACTION) / 2) / (FRACTION) : ((x) - (FRACTION) / 2) / (FRACTION))

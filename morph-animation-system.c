// morph-animation-system.c
// Easing functions for smooth animations

#include "morph-animation-system.h"
#include <math.h>

// Linear easing (no acceleration)
gdouble ease_linear(gdouble t) {
    return t;
}

// Sine ease-out: gentle deceleration
// RECOMMENDED - Smoothest, no surprises
gdouble ease_out_sine(gdouble t) {
    return sin(t * M_PI / 2.0);
}

// Cubic ease-in-out: slow start, fast middle, slow end
// Dynamic Island style
gdouble ease_in_out_cubic(gdouble t) {
    if (t < 0.5) {
        return 4.0 * t * t * t;
    } else {
        gdouble f = (2.0 * t) - 2.0;
        return 0.5 * f * f * f + 1.0;
    }
}

// Exponential ease-out: very fast start, very slow end
// Most dramatic
gdouble ease_out_expo(gdouble t) {
    return (t == 1.0) ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}

// Spring easing: overshoots slightly then settles
// Most iOS-like, subtle bounce
gdouble ease_spring(gdouble t) {
    gdouble c4 = (2.0 * M_PI) / 3.0;
    return t == 0.0 ? 0.0 : 
           t == 1.0 ? 1.0 : 
           pow(2.0, -10.0 * t) * sin((t * 10.0 - 0.75) * c4) + 1.0;
}

// Apply selected easing curve
gdouble apply_easing(gdouble t, EasingCurve curve) {
    switch (curve) {
        case EASE_LINEAR:
            return ease_linear(t);
        case EASE_OUT_SINE:
            return ease_out_sine(t);
        case EASE_IN_OUT_CUBIC:
            return ease_in_out_cubic(t);
        case EASE_OUT_EXPO:
            return ease_out_expo(t);
        case EASE_SPRING:
            return ease_spring(t);
        default:
            return ease_out_sine(t);  // Default to smoothest
    }
}

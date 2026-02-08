// morph-animation-system.h
// Dynamic Island-style morphing animation system for HyprWave

#ifndef MORPH_ANIMATION_H
#define MORPH_ANIMATION_H

#include <gtk/gtk.h>
#include <glib.h>

// Animation state structure
typedef struct {
    gdouble progress;           // Current animation progress (0.0 to 1.0)
    gint64 start_time;          // Animation start time (microseconds)
    gint duration_ms;           // Animation duration (milliseconds)
    gboolean is_morphing_to_idle;  // Direction: true = to idle, false = to control
    
    // Start states
    gint start_width;
    gint start_height;
    gdouble start_button_opacity;
    gdouble start_viz_opacity;
    
    // End states
    gint end_width;
    gint end_height;
    gdouble end_button_opacity;
    gdouble end_viz_opacity;
} MorphAnimation;

// Easing curve types
typedef enum {
    EASE_LINEAR,           // No easing (constant speed)
    EASE_OUT_SINE,         // Gentle, smooth (RECOMMENDED)
    EASE_IN_OUT_CUBIC,     // Dynamic Island style
    EASE_OUT_EXPO,         // Fast start, slow end
    EASE_SPRING            // Subtle bounce (most iOS-like)
} EasingCurve;

// Easing functions
gdouble ease_linear(gdouble t);
gdouble ease_out_sine(gdouble t);
gdouble ease_in_out_cubic(gdouble t);
gdouble ease_out_expo(gdouble t);
gdouble ease_spring(gdouble t);

// Apply easing curve
gdouble apply_easing(gdouble t, EasingCurve curve);

#endif // MORPH_ANIMATION_H

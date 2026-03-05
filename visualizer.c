#include "visualizer.h"
#include "debug.h"
#include <math.h>
#include <string.h>

#define SMOOTHING_FACTOR 0.7

// Process audio samples into bar heights.
// Called from PulseAudio thread — writes ONLY to bar_heights[].
// update_visualizer() reads bar_heights[] on the GTK main thread.
// Each bar_heights[i] is a single double store; reading a stale value
// by one frame is fine for a visualizer.
static void process_audio_samples(VisualizerState *state, const float *samples, size_t n_samples) {
    if (n_samples == 0) return;

    size_t samples_per_bin = n_samples / state->num_bars;

    // Guard: if the fragment is smaller than the bar count, samples_per_bin
    // is 0 and we'd divide by zero on the RMS line.  Scatter what we have
    // across the first bins and decay the rest.
    if (samples_per_bin == 0) {
        for (int i = 0; i < state->num_bars; i++) {
            if ((size_t)i < n_samples) {
                gdouble val = (gdouble)(samples[i] * samples[i]);
                gdouble normalized = sqrt(val) * 10.0;
                if (normalized > 1.0) normalized = 1.0;
                state->bar_smoothed[i] = (SMOOTHING_FACTOR * state->bar_smoothed[i]) +
                                         ((1.0 - SMOOTHING_FACTOR) * normalized);
            } else {
                state->bar_smoothed[i] *= SMOOTHING_FACTOR;
            }
            state->bar_heights[i] = state->bar_smoothed[i];
        }
        return;
    }

    for (int i = 0; i < state->num_bars; i++) {
        gdouble sum = 0.0;
        size_t start = i * samples_per_bin;
        size_t end   = start + samples_per_bin;

        for (size_t j = start; j < end && j < n_samples; j++) {
            sum += samples[j] * samples[j];
        }

        gdouble rms        = sqrt(sum / samples_per_bin);
        gdouble normalized = rms * 10.0;
        if (normalized > 1.0) normalized = 1.0;

        state->bar_smoothed[i] = (SMOOTHING_FACTOR * state->bar_smoothed[i]) +
                                 ((1.0 - SMOOTHING_FACTOR) * normalized);
        state->bar_heights[i] = state->bar_smoothed[i];
    }
}

// PulseAudio stream read callback (runs on PA thread)
static void pa_stream_read_callback(pa_stream *stream, size_t nbytes, void *userdata) {
    VisualizerState *state = (VisualizerState *)userdata;
    const void *data;
    size_t length;

    if (pa_stream_peek(stream, &data, &length) < 0 || !data) {
        if (data) pa_stream_drop(stream);
        return;
    }

    const float *samples = (const float *)data;
    size_t n_samples = length / sizeof(float);

    process_audio_samples(state, samples, n_samples);
    pa_stream_drop(stream);
}

// Render loop — updates the GTK bar widgets from bar_heights[].
// Runs on the main thread via g_timeout at configured FPS.
static gboolean update_visualizer(gpointer user_data) {
    VisualizerState *state = (VisualizerState *)user_data;

    // Skip rendering when container is invisible or fully transparent
    if (!gtk_widget_get_visible(state->container) ||
        gtk_widget_get_opacity(state->container) < 0.01) {
        return G_SOURCE_CONTINUE;
    }

    for (int i = 0; i < state->num_bars; i++) {
        gint min_height = 2;
        gint max_height = 24;   // fits in the 32px idle bar

        gdouble h = state->bar_heights[i];

        if (h < 0.01) {
            h = 0.0;
            state->bar_heights[i] = 0.0;
        }

        gint bar_height = min_height + (gint)(h * (max_height - min_height));

        gtk_widget_set_size_request(state->bars[i], 3, bar_height);

        // Fade out bars at minimum so there is no flat line when silent
        gtk_widget_set_opacity(state->bars[i], (bar_height <= min_height) ? 0.0 : 1.0);
    }

    return G_SOURCE_CONTINUE;
}

// Fade-in animation used by visualizer_show() (the SIGUSR1 restore path)
static gboolean fade_visualizer(gpointer user_data) {
    VisualizerState *state = (VisualizerState *)user_data;

    if (state->is_showing) {
        state->fade_opacity += 0.05;
        if (state->fade_opacity >= 1.0) {
            state->fade_opacity = 1.0;
            gtk_widget_set_opacity(state->container, 1.0);
            state->fade_timer = 0;
            return G_SOURCE_REMOVE;
        }
        gtk_widget_set_opacity(state->container, state->fade_opacity);
    }

    return G_SOURCE_CONTINUE;
}

// PulseAudio context state callback
static void pa_context_state_callback(pa_context *context, void *userdata) {
    VisualizerState *state = (VisualizerState *)userdata;

    switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY: {
            pa_sample_spec sample_spec = {
                .format   = PA_SAMPLE_FLOAT32LE,
                .rate     = 44100,
                .channels = 1
            };

            state->pa_stream = pa_stream_new(context, "HyprWave Visualizer", &sample_spec, NULL);
            if (!state->pa_stream) {
                g_printerr("Failed to create PulseAudio stream\n");
                return;
            }

            pa_stream_set_read_callback(state->pa_stream, pa_stream_read_callback, state);

            pa_buffer_attr buffer_attr = {
                .maxlength = (uint32_t) -1,
                .fragsize  = 4096
            };

            // Monitor the default sink — captures playback, not mic
            const char *monitor_source = "@DEFAULT_MONITOR@";

            if (pa_stream_connect_record(state->pa_stream, monitor_source, &buffer_attr,
                                         PA_STREAM_ADJUST_LATENCY) < 0) {
                g_printerr("Failed to connect PulseAudio stream\n");
            } else {
                debug_print("✓ Visualizer capturing playback audio (monitor)\n");
            }
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            g_printerr("PulseAudio context failed/terminated\n");
            break;
        default:
            break;
    }
}

// Initialize visualizer
VisualizerState* visualizer_init(gint num_bars, gint fps) {
    VisualizerState *state = g_new0(VisualizerState, 1);
    state->is_showing  = FALSE;
    state->is_running  = FALSE;
    state->fade_opacity = 0.0;
    state->num_bars = CLAMP(num_bars, 1, VISUALIZER_MAX_BARS);
    state->fps = CLAMP(fps, 1, 144);

    for (int i = 0; i < state->num_bars; i++) {
        state->bar_heights[i]  = 0.0;
        state->bar_smoothed[i] = 0.0;
    }

    // Container: horizontal box, 275px wide, bottom-aligned inside the overlay.
    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state->container = container;

    gtk_widget_set_overflow(container, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_halign(container, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(container, GTK_ALIGN_END);
    gtk_widget_set_hexpand(container, FALSE);
    gtk_widget_set_vexpand(container, FALSE);
    gtk_widget_set_size_request(container, 275, -1);
    gtk_widget_add_css_class(container, "visualizer-container");

    debug_print("✓ Visualizer container: 275px fixed width\n");

    for (int i = 0; i < state->num_bars; i++) {
        GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        state->bars[i] = bar;

        gtk_widget_add_css_class(bar, "visualizer-bar");
        gtk_widget_set_size_request(bar, -1, 2);
        gtk_widget_set_visible(bar, TRUE);
        gtk_widget_set_valign(bar, GTK_ALIGN_END);   // grow upward from bottom
        gtk_widget_set_hexpand(bar, TRUE);
        gtk_widget_set_halign(bar, GTK_ALIGN_FILL);

        gtk_box_append(GTK_BOX(container), bar);
    }

    debug_print("✓ %d visualizer bars created at %d fps\n", state->num_bars, state->fps);

    // PulseAudio: create context, stream is created inside the state callback
    state->pa_mainloop = pa_threaded_mainloop_new();
    if (state->pa_mainloop) {
        state->pa_mainloop_api = pa_threaded_mainloop_get_api(state->pa_mainloop);
        state->pa_context      = pa_context_new(state->pa_mainloop_api, "HyprWave");
        if (state->pa_context) {
            pa_context_set_state_callback(state->pa_context, pa_context_state_callback, state);
        }
    }

    // Render loop; visibility check inside update_visualizer skips work when hidden
    state->render_timer = g_timeout_add(1000 / state->fps, update_visualizer, state);

    return state;
}

void visualizer_hide(VisualizerState *state) {
    if (!state || !state->is_showing) return;

    state->is_showing = FALSE;

    if (state->fade_timer > 0) {
        g_source_remove(state->fade_timer);
        state->fade_timer = 0;
    }

    gtk_widget_set_visible(state->container, FALSE);
    debug_print("Visualizer hidden\n");
}

void visualizer_show(VisualizerState *state) {
    if (!state || state->is_showing) return;

    state->is_showing = TRUE;

    if (state->fade_timer > 0) {
        g_source_remove(state->fade_timer);
    }

    gtk_widget_set_visible(state->container, TRUE);
    state->fade_opacity = 0.0;
    state->fade_timer   = g_timeout_add(16, fade_visualizer, state);

    debug_print("Visualizer fading in\n");
}

void visualizer_start(VisualizerState *state) {
    if (!state || state->is_running || !state->pa_context) return;

    if (pa_context_connect(state->pa_context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        g_printerr("Failed to connect to PulseAudio\n");
        return;
    }

    if (pa_threaded_mainloop_start(state->pa_mainloop) < 0) {
        g_printerr("Failed to start PulseAudio mainloop\n");
        return;
    }

    state->is_running = TRUE;
    debug_print("✓ Visualizer audio capture started\n");
}

void visualizer_stop(VisualizerState *state) {
    if (!state || !state->is_running) return;

    if (state->pa_stream) {
        pa_stream_disconnect(state->pa_stream);
        pa_stream_unref(state->pa_stream);
        state->pa_stream = NULL;
    }

    if (state->pa_mainloop) {
        pa_threaded_mainloop_stop(state->pa_mainloop);
    }

    state->is_running = FALSE;
    debug_print("Visualizer audio capture stopped\n");
}

void visualizer_cleanup(VisualizerState *state) {
    if (!state) return;

    if (state->render_timer > 0) {
        g_source_remove(state->render_timer);
    }
    if (state->fade_timer > 0) {
        g_source_remove(state->fade_timer);
    }

    visualizer_stop(state);

    if (state->pa_context) {
        pa_context_disconnect(state->pa_context);
        pa_context_unref(state->pa_context);
    }
    if (state->pa_mainloop) {
        pa_threaded_mainloop_free(state->pa_mainloop);
    }

    g_free(state);
}

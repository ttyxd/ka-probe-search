#include "composite_search.h"
#include <stdio.h>

void search_init(search_state_t *state, int initial_low, int initial_high, int initial_probe, int stable_ka) {
    state->low = initial_low;
    state->high = initial_high;
    state->current_probe_interval = initial_probe;
    state->last_successful_interval = stable_ka;
    state->is_exponential_phase = 1;
    state->retry_count = 0;
    printf("[SEARCH] Initializing: Range %d-%d, First Probe: %d\n", state->low, state->high, state->current_probe_interval);
}

void search_handle_success(search_state_t *state) {
    printf("[SEARCH] SUCCESS: Probe at %ds succeeded.\n", state->current_probe_interval);
    state->last_successful_interval = state->current_probe_interval;
    state->low = state->current_probe_interval;
    state->retry_count = 0;

    if (state->is_exponential_phase) {
        // keep doubling the probe to find the failure point.
        state->current_probe_interval = 2 * state->low;
        
        // safety rail to avoid probing past the absolute max.
        if (state->current_probe_interval > state->high) {
            state->current_probe_interval = state->high;
        }

    } else { // binary search phase
        state->current_probe_interval = state->low + (state->high - state->low) / 2;
    }

    if (state->low >= state->high) {
        printf("[SEARCH] CONVERGED: Optimal timeout is ~%ds.\n", state->last_successful_interval);
        state->current_probe_interval = state->last_successful_interval;
    } else {
        printf("[SEARCH] NEXT PROBE: %ds (Range: %d-%d).\n", state->current_probe_interval, state->low, state->high);
    }
}

void search_handle_failure(search_state_t *state) {
    printf("[SEARCH] FAILURE: Probe at %ds failed.\n", state->current_probe_interval);

    // on first failure, retry the same interval to guard against transient network errors.
    if (state->retry_count < 1) {
        state->retry_count++;
        printf("[SEARCH] RETRY: Retrying same interval (%ds) in case of transient failure.\n", state->current_probe_interval);
    } else {
        // failure is confirmed after a retry.
        state->high = state->current_probe_interval - 1;
        state->retry_count = 0;

        if (state->is_exponential_phase) {
            // overshoot detected, switch to binary search.
            printf("[SEARCH] Switching to Binary Search phase after overshooting.\n");
            state->is_exponential_phase = 0;
        }

        state->current_probe_interval = state->low + (state->high - state->low) / 2;

        if (state->low >= state->high) {
            printf("[SEARCH] CONVERGED: Optimal timeout is ~%ds.\n", state->last_successful_interval);
            state->current_probe_interval = state->last_successful_interval;
        } else {
             printf("[SEARCH] NEXT PROBE: %ds (Range: %d-%d).\n", state->current_probe_interval, state->low, state->high);
        }
    }
}

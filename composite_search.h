#ifndef COMPOSITE_SEARCH_H
#define COMPOSITE_SEARCH_H

// state for the composite search algorithm.
typedef struct {
    int low;                      // lower bound of the search range
    int high;                     // upper bound of the search range
    int current_probe_interval;   // the interval being tested now
    int last_successful_interval; // the longest known-good interval
    int is_exponential_phase;     // flag for exponential (1) vs binary (0) search
    int retry_count;              // counter for the single retry on failure
} search_state_t;

// initializes the search state.
void search_init(search_state_t *state, int initial_low, int initial_high, int initial_probe, int stable_ka);

// updates state after a successful keep-alive probe.
void search_handle_success(search_state_t *state);

// updates state after a keep-alive probe fails. handles the retry logic.
void search_handle_failure(search_state_t *state);

#endif // COMPOSITE_SEARCH_H

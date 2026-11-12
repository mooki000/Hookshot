/* The scenario runner's input goes through the real bridge, then its controller-read hook. Reuse the existing fake
   game and frame recorder, not a second timing parser; the receipt written at quit is what the game actually read. */
#define main control_fixture_main
#include "control_test.c"
#undef main

/* Only the scenario's explicit exclusive inputs ask this fixture to advance. Their scheduled delays count too
   (hos_control_input returns exclusive until the last input ends). No timer paces a frame, and idle time changes no
   frame number: the test can assert the complete schedule even on a loaded Windows runner. */
static void *scenario_game_thread(void *unused) {
    (void)unused;
    for (;;) {
        uint64_t buttons = 0;
        int32_t stick[4] = {0, 0, 0, 0};
        const int exclusive = hos_control_input(&buttons, stick);
        if (!exclusive) {
            sleep_ms(1);   /* (wait for work, never between the frames of an input) */
            continue;
        }
        const int64_t at = fake_frame();
        CHECK(at < FRAMES);
        seen_buttons[at] = buttons;
        memcpy(seen_stick[at], stick, sizeof stick);
        seen_exclusive[at] = exclusive;
        draws_total += (int64_t)draws_per_frame;
        __atomic_store_n(&frame, at + 1, __ATOMIC_RELEASE);
    }
    return NULL;
}

static void scenario_quit(void) {
    const char *path = getenv("SCENARIO_FRAME_FILE");
    CHECK(path && *path);
    FILE *f = fopen(path, "wb");
    CHECK(f);
    const int64_t end = fake_frame();   /* (the acquire publishes the completed frames' records) */
    /* The last completed request left the clock at its end. A real controller read there proves release; it does not
       need two extra wall-paced presents or a race with the next input to make a neutral record available. */
    uint64_t buttons = 0;
    int32_t stick[4] = {0, 0, 0, 0};
    const int exclusive = hos_control_input(&buttons, stick);
    fprintf(f, "frame,buttons,lx,ly,rx,ry,exclusive\n");
    for (int64_t n = 0; n < end && n < FRAMES; n++)
        fprintf(f, "%lld,%llu,%d,%d,%d,%d,%d\n", (long long)n, (unsigned long long)seen_buttons[n],
                (int)seen_stick[n][0], (int)seen_stick[n][1], (int)seen_stick[n][2], (int)seen_stick[n][3],
                seen_exclusive[n]);
    fprintf(f, "%lld,%llu,%d,%d,%d,%d,%d\n", (long long)end, (unsigned long long)buttons,
            (int)stick[0], (int)stick[1], (int)stick[2], (int)stick[3], exclusive);
    CHECK(fclose(f) == 0);
    fake_quit();
}

int main(void) {
    const char *path = getenv("HOS_CONTROL");
    CHECK(path && *path);
    game.quit = scenario_quit;
    CHECK(hos_control_serve(path, &game));
    start_thread(scenario_game_thread, NULL);
    for (;;) sleep_ms(10);
}

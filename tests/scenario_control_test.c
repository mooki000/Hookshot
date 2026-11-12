/* The scenario runner's input goes through the real bridge, then its controller-read hook. Reuse the existing fake
   game and frame recorder, not a second timing parser; the receipt written at quit is what the game actually read. */
#define _CRT_SECURE_NO_WARNINGS
#define main control_fixture_main
#include "control_test.c"
#undef main

static void scenario_quit(void) {
    const char *path = getenv("SCENARIO_FRAME_FILE");
    CHECK(path && *path);
    FILE *f = fopen(path, "wb");
    CHECK(f);
    const int64_t end = fake_frame();   /* (the acquire publishes the completed frames' records) */
    fprintf(f, "frame,buttons,lx,ly,rx,ry,exclusive\n");
    for (int64_t n = 0; n < end && n < FRAMES; n++)
        fprintf(f, "%lld,%llu,%d,%d,%d,%d,%d\n", (long long)n, (unsigned long long)seen_buttons[n],
                (int)seen_stick[n][0], (int)seen_stick[n][1], (int)seen_stick[n][2], (int)seen_stick[n][3],
                seen_exclusive[n]);
    CHECK(fclose(f) == 0);
    fake_quit();
}

int main(void) {
    const char *path = getenv("HOS_CONTROL");
    CHECK(path && *path);
    game.quit = scenario_quit;
    CHECK(hos_control_serve(path, &game));
    start_thread(game_thread, NULL);
    for (;;) sleep_ms(10);
}

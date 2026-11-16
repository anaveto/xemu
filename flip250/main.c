/*
 * Test guest for xemu: renders with PGRAPH and flips (pbkit, NV_PGRAPH_INCREMENT)
 * once every 15 vertical blanks, i.e. 4 times a second at 60 Hz.
 */
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    if (pb_init()) {
        debugPrint("pb_init failed\n");
        Sleep(5000);
        return 1;
    }
    pb_show_front_screen();

    int w = pb_back_buffer_width();
    int h = pb_back_buffer_height();
    unsigned int flips = 0;
    DWORD vbl = pb_get_vbl_counter();

    while (1) {
        while (pb_get_vbl_counter() - vbl < 15) {
            pb_wait_for_vbl();
        }
        vbl = pb_get_vbl_counter();

        pb_reset();
        pb_target_back_buffer();
        pb_fill(0, 0, w, h, 0xff202040);
        pb_fill((flips * 40) % (w - 40), h / 2 - 40, 40, 80, 0xffffc000);
        pb_erase_text_screen();
        pb_print("flip250: one flip every 15 vblanks (250 ms)\nflips: %u\n", flips);
        pb_draw_text_screen();
        while (pb_busy()) {
        }
        while (pb_finished()) {
        }
        flips++;
    }
    return 0;
}

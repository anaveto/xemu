/*
 * Test guest for xemu: four phases of 20 s each, then phase 4 again forever.
 * No phase uses NV_PGRAPH_INCREMENT (no pb_finished).
 *  1 red:    CPU draws into the one framebuffer, 30 updates a second, no flip
 *  2 green:  CPU draws into two buffers, flips by PCRTC_START, 20 flips a second
 *  3 blue:   PGRAPH draws into the displayed buffer, 30 updates a second, no flip
 *  4 yellow: PGRAPH draws into two buffers, flips by PCRTC_START, 20 flips a second
 */
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#define W 640
#define H 480
#define PHASE_VBL 1200
#define CRTC_START (*(volatile DWORD *)0xFD600800)

static void cpu_frame(DWORD *fb, int x, DWORD bar)
{
    for (int y = 0; y < H; y++) {
        DWORD *row = fb + y * W;
        for (int i = 0; i < W; i++) {
            row[i] = (y >= H / 2 - 40 && y < H / 2 + 40 && i >= x && i < x + 40) ? bar : 0xff202020;
        }
    }
}

static void pgraph_frame(int buffer, int x, DWORD bar, const char *label, unsigned int n)
{
    pb_reset();
    pb_target_extra_buffer(buffer);
    pb_fill(0, 0, W, H, 0xff202020);
    pb_fill(x, H / 2 - 40, 40, 80, bar);
    pb_erase_text_screen();
    pb_print("%s\nupdates: %u\n", label, n);
    pb_draw_text_screen();
    while (pb_busy()) {
    }
}

int main(void)
{
    XVideoSetMode(W, H, 32, REFRESH_DEFAULT);
    /* pbkit first: XVideoWaitForVBlank() keeps IRQ 3, and pb_init() then fails with -4 */
    pb_extra_buffers(2);
    if (pb_init()) {
        debugPrint("pb_init failed\n");
        Sleep(5000);
        return 1;
    }

    /* 1: CPU, one buffer */
    DWORD *fb0 = (DWORD *)XVideoGetFB();
    CRTC_START = (DWORD)fb0 & 0x03FFFFFF;
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 2, n++) {
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        cpu_frame(fb0, (n * 8) % (W - 40), 0xffff0000);
    }

    /* 2: CPU, two buffers, PCRTC_START flips */
    DWORD *fb1 = MmAllocateContiguousMemoryEx(W * H * 4, 0, 0x03FFAFFF, 0x1000,
                                              PAGE_READWRITE | PAGE_WRITECOMBINE);
    DWORD *cpu_buf[2] = { fb0, fb1 };
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 3, n++) {
        DWORD *back = cpu_buf[(n + 1) & 1];
        cpu_frame(back, (n * 12) % (W - 40), 0xff00ff00);
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        CRTC_START = (DWORD)back & 0x03FFFFFF;
    }
    CRTC_START = (DWORD)fb0 & 0x03FFFFFF;

    /* 3: PGRAPH into the displayed buffer, no flip */
    CRTC_START = (DWORD)pb_extra_buffer(0) & 0x03FFFFFF;
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 2, n++) {
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        pgraph_frame(0, (n * 8) % (W - 40), 0xff4060ff, "vramcrtc 3: PGRAPH, one buffer, no flip", n);
    }

    /* 4: PGRAPH into two buffers, PCRTC_START flips */
    for (unsigned int n = 0;; n++) {
        int back = (n + 1) & 1;
        pgraph_frame(back, (n * 12) % (W - 40), 0xffffc000, "vramcrtc 4: PGRAPH, two buffers, PCRTC_START flips", n);
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        pb_wait_for_vbl();
        CRTC_START = (DWORD)pb_extra_buffer(back) & 0x03FFFFFF;
    }
    return 0;
}

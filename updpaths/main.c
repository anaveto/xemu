/*
 * Test guest for xemu: picture updates that do not flip or draw with PGRAPH.
 * Phases of 20 s each; the shown surface is PGRAPH-rendered once per phase
 * (label), then only the phase's own path changes the picture, 30 times a
 * second. The bar's x position is (n * 8) % 600 for update n.
 *  1 PVIDEO:  overlay buffer switches (NV_PVIDEO_OFFSET + NV_PVIDEO_BUFFER),
 *             overlay content written by the CPU into the back overlay buffer
 *  2 blit:    NV09F image blit of a CPU-drawn band into the shown surface
 *  3 CPU:     CPU writes the band straight into the shown surface
 *  4 switch:  PGRAPH flips by PCRTC_START at 4 a second for 10 s, then PGRAPH
 *             draws into the shown buffer 30 times a second for 10 s; repeats
 */
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#define W 640
#define H 480
#define BAND_H 80
#define BAND_Y (H / 2 - BAND_H / 2)
#define PHASE_VBL 1200
#define REG(x) (*(volatile DWORD *)(0xFD000000 + (x)))
#define CRTC_START REG(0x600800)
#define PVIDEO_BUFFER REG(0x8700)
#define PVIDEO_STOP REG(0x8704)
#define PVIDEO_BASE REG(0x8900)
#define PVIDEO_LIMIT REG(0x8908)
#define PVIDEO_LUMINANCE REG(0x8910)
#define PVIDEO_CHROMINANCE REG(0x8918)
#define PVIDEO_OFFSET REG(0x8920)
#define PVIDEO_SIZE_IN REG(0x8928)
#define PVIDEO_POINT_IN REG(0x8930)
#define PVIDEO_DS_DX REG(0x8938)
#define PVIDEO_DT_DY REG(0x8940)
#define PVIDEO_POINT_OUT REG(0x8948)
#define PVIDEO_SIZE_OUT REG(0x8950)
#define PVIDEO_FORMAT REG(0x8958)
#define OV_W 320
#define OV_H 120
#define NV062_SET_COLOR_FORMAT 0x300
#define NV062_SET_PITCH 0x304
#define NV062_SET_OFFSET_SOURCE 0x308
#define NV062_SET_OFFSET_DESTIN 0x30c
#define NV09F_CONTROL_POINT_IN 0x300
#define NV09F_CONTROL_POINT_OUT 0x304
#define NV09F_SIZE 0x308

static DWORD phys(const void *p) { return (DWORD)p & 0x03FFFFFF; }

static void *alloc_contig(DWORD size)
{
    return MmAllocateContiguousMemoryEx(size, 0, 0x03FFAFFF, 0x1000, PAGE_READWRITE | PAGE_WRITECOMBINE);
}

static void pgraph_frame(int buffer, int x, DWORD bar, const char *label, unsigned int n)
{
    pb_reset();
    pb_target_extra_buffer(buffer);
    pb_fill(0, 0, W, H, 0xff202020);
    if (x >= 0) {
        pb_fill(x, BAND_Y, 40, BAND_H, bar);
    }
    pb_erase_text_screen();
    pb_print("%s\nupdates: %u\n", label, n);
    pb_draw_text_screen();
    while (pb_busy()) {
    }
}

/* the band: BAND_H rows of W pixels, bar at x */
static void cpu_band(DWORD *dst, DWORD pitch_px, int x, DWORD bar)
{
    for (int y = 0; y < BAND_H; y++) {
        DWORD *row = dst + y * pitch_px;
        for (int i = 0; i < W; i++) {
            row[i] = (i >= x && i < x + 40) ? bar : 0xff202020;
        }
    }
}

/* YUY2-like CR8YB8CB8YA8: one DWORD holds two pixels */
static void overlay_frame(DWORD *ov, int x)
{
    for (int y = 0; y < OV_H; y++) {
        DWORD *row = ov + y * (OV_W / 2);
        for (int i = 0; i < OV_W / 2; i++) {
            int px = i * 2;
            row[i] = (px >= x && px < x + 20) ? 0xF080F080 : 0x80408040;
        }
    }
}

static void wait_vbl(int n)
{
    while (n--) {
        pb_wait_for_vbl();
    }
}

int main(void)
{
    XVideoSetMode(W, H, 32, REFRESH_DEFAULT);
    /* pbkit first: XVideoWaitForVBlank() keeps IRQ 3, and pb_init() then fails with -4 */
    pb_extra_buffers(3);
    if (pb_init()) {
        debugPrint("pb_init failed\n");
        Sleep(5000);
        return 1;
    }
    DWORD *shown = pb_extra_buffer(0);
    DWORD pitch_px = pb_back_buffer_pitch() / 4;

    /* 1: PVIDEO overlay buffer switches */
    pgraph_frame(0, -1, 0, "updpaths 1: PVIDEO overlay switches, 30/s", 0);
    CRTC_START = phys(shown);
    DWORD *ov[2] = { alloc_contig(OV_W * 2 * OV_H), alloc_contig(OV_W * 2 * OV_H) };
    PVIDEO_LUMINANCE = 0x1000;
    PVIDEO_CHROMINANCE = 0x1000;
    PVIDEO_BASE = 0;
    PVIDEO_LIMIT = 0x03FFFFFF;
    PVIDEO_POINT_IN = 0;
    PVIDEO_SIZE_IN = OV_W | (OV_H << 16);
    PVIDEO_DS_DX = 1 << 20;
    PVIDEO_DT_DY = 1 << 20;
    PVIDEO_POINT_OUT = 160 | ((BAND_Y - 20) << 16);
    PVIDEO_SIZE_OUT = OV_W | (OV_H << 16);
    PVIDEO_FORMAT = (OV_W * 2) | (1 << 16);
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 2, n++) {
        DWORD *back = ov[n & 1];
        overlay_frame(back, (n * 8) % (OV_W - 20));
        wait_vbl(2);
        PVIDEO_OFFSET = phys(back);
        PVIDEO_BUFFER = 1;
    }
    PVIDEO_STOP = 1;
    PVIDEO_BUFFER = 0;

    /* 2: NV09F blit of a CPU-drawn band into the shown surface */
    pgraph_frame(0, -1, 0, "updpaths 2: NV09F blit into the shown surface, 30/s", 0);
    DWORD *band = alloc_contig(W * 4 * BAND_H);
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 2, n++) {
        cpu_band(band, W, (n * 8) % (W - 40), 0xff00c0c0);
        wait_vbl(2);
        pb_reset();
        DWORD *p = pb_begin();
        p = pb_push1_to(SUBCH_4, p, NV062_SET_COLOR_FORMAT, 0x07); /* LE_X8R8G8B8 */
        p = pb_push1_to(SUBCH_4, p, NV062_SET_PITCH, (W * 4) | ((pitch_px * 4) << 16));
        p = pb_push1_to(SUBCH_4, p, NV062_SET_OFFSET_SOURCE, phys(band));
        p = pb_push1_to(SUBCH_4, p, NV062_SET_OFFSET_DESTIN, phys(shown));
        p = pb_push1_to(SUBCH_3, p, NV09F_CONTROL_POINT_IN, 0);
        p = pb_push1_to(SUBCH_3, p, NV09F_CONTROL_POINT_OUT, BAND_Y << 16);
        p = pb_push1_to(SUBCH_3, p, NV09F_SIZE, W | (BAND_H << 16));
        pb_end(p);
        while (pb_busy()) {
        }
    }

    /* 3: CPU writes into the shown, PGRAPH-rendered surface */
    pgraph_frame(0, -1, 0, "updpaths 3: CPU writes into the shown surface, 30/s", 0);
    for (unsigned int v = 0, n = 0; v < PHASE_VBL; v += 2, n++) {
        wait_vbl(2);
        cpu_band(shown + BAND_Y * pitch_px, pitch_px, (n * 8) % (W - 40), 0xffc000c0);
    }

    /* 4: PGRAPH flips at 4/s for 10 s, then PGRAPH draws into the shown buffer at 30/s for 10 s */
    for (unsigned int round = 0;; round++) {
        int front = 0;
        for (unsigned int v = 0, n = 0; v < PHASE_VBL / 2; v += 15, n++) {
            int back = 1 + (n & 1);
            pgraph_frame(back, (n * 8) % (W - 40), 0xffffc000, "updpaths 4a: PGRAPH flips by PCRTC_START, 4/s", n);
            wait_vbl(15);
            CRTC_START = phys(pb_extra_buffer(back));
            front = back;
        }
        for (unsigned int v = 0, n = 0; v < PHASE_VBL / 2; v += 2, n++) {
            wait_vbl(2);
            pgraph_frame(front, (n * 8) % (W - 40), 0xff40ff40, "updpaths 4b: PGRAPH into the shown buffer, 30/s", n);
        }
    }
    return 0;
}

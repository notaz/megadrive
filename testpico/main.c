/*
 * This software is released into the public domain.
 * See UNLICENSE file in top level directory.
 */
#include <stdlib.h>
#include <stdarg.h>
#include "common.h"
#include "asmtools.h"

#define VDP_DATA_PORT    0xC00000
#define VDP_CTRL_PORT    0xC00004
#define VDP_HV_COUNTER   0xC00008

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END - FONT_LEN * 32)

/* note: using ED menu's layout here.. */
#define WPLANE           (TILE_MEM_END + 0x0000)
#define HSCRL            (TILE_MEM_END + 0x0800)
#define SLIST            (TILE_MEM_END + 0x0C00)
#define APLANE           (TILE_MEM_END + 0x1000)
#define BPLANE           (TILE_MEM_END + 0x3000)

#define write16_z80le(a, d) \
    ((volatile u8 *)(a))[0] = (u8)(d), \
    ((volatile u8 *)(a))[1] = ((d) >> 8)

static inline u16 read16_z80le(const void *a_)
{
    volatile const u8 *a = (volatile const u8 *)a_;
    return a[0] | ((u16)a[1] << 8);
}

#define CTL_WRITE_VRAM(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define CTL_WRITE_VSRAM(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x10)
#define CTL_WRITE_CRAM(adr) \
    (((0xC000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define CTL_READ_VRAM(adr) \
    (((0x0000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define CTL_READ_VSRAM(adr) \
    (((0x0000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x10)
#define CTL_READ_CRAM(adr) \
    (((0x0000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x20)

#define CTL_WRITE_DMA 0x80

#define VDP_setReg(r, v) \
    write16(VDP_CTRL_PORT, 0x8000 | ((r) << 8) | ((v) & 0xff))

enum {
    VDP_MODE1 = 0x00,
    VDP_MODE2 = 0x01,
    VDP_NT_SCROLLA = 0x02,
    VDP_NT_WIN = 0x03,
    VDP_NT_SCROLLB = 0x04,
    VDP_SAT_BASE = 0x05,
    VDP_BACKDROP = 0x07,
    VDP_MODE3 = 0x0b,
    VDP_MODE4 = 0x0c,
    VDP_HSCROLL = 0x0d,
    VDP_AUTOINC = 0x0f,
    VDP_SCROLLSZ = 0x10,
    VDP_DMA_LEN0 = 0x13,
    VDP_DMA_LEN1 = 0x14,
    VDP_DMA_SRC0 = 0x15,
    VDP_DMA_SRC1 = 0x16,
    VDP_DMA_SRC2 = 0x17,
};

#define VDP_MODE1_PS   0x04
#define VDP_MODE1_IE1  0x10 // h int
#define VDP_MODE2_MD   0x04
#define VDP_MODE2_PAL  0x08 // 30 col
#define VDP_MODE2_DMA  0x10
#define VDP_MODE2_IE0  0x20 // v int
#define VDP_MODE2_DISP 0x40
#define VDP_MODE2_128K 0x80

#define SR_PAL        (1 << 0)
#define SR_DMA        (1 << 1)
#define SR_HB         (1 << 2)
#define SR_VB         (1 << 3)
#define SR_ODD        (1 << 4)
#define SR_C          (1 << 5)
#define SR_SOVR       (1 << 6)
#define SR_F          (1 << 7)
#define SR_FULL       (1 << 8)
#define SR_EMPT       (1 << 9)

/* cell counts */
#define LEFT_BORDER 1   /* lame TV */
#define PLANE_W 64
#define PLANE_H 32
#define CSCREEN_H 28

/* data.s */
extern const u32 font_base[];
extern const u8 z80_test[];
extern const u8 z80_test_end[];

static int text_pal;

static noinline void VDP_drawTextML(const char *str, u16 plane_base,
    u16 x, u16 y)
{
    const u8 *src = (const u8 *)str;
    u16 basetile = text_pal << 13;
    int max_len = 40 - LEFT_BORDER;
    int len;
    u32 addr;

    x += LEFT_BORDER;

    for (len = 0; str[len] && len < max_len; len++)
        ;
    if (len > (PLANE_W - x))
        len = PLANE_W - x;

    addr = plane_base + ((x + (PLANE_W * y)) << 1);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(addr));

    while (len-- > 0) {
        write16(VDP_DATA_PORT,
            basetile | ((*src++) - 32 + TILE_FONT_BASE / 32));
    }
}

static int printf_ypos;

static void printf_line(int x, const char *buf)
{
    u32 addr;
    int i;

    VDP_drawTextML(buf, APLANE, x, printf_ypos++ & (PLANE_H - 1));

    if (printf_ypos >= CSCREEN_H) {
        /* clear next line */
        addr = APLANE;
        addr += (PLANE_W * (printf_ypos & (PLANE_H - 1))) << 1;
        write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(addr));
        for (i = 0; i < 40 / 2; i++)
            write32(VDP_DATA_PORT, 0);

        /* scroll plane */
        write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
        write16(VDP_DATA_PORT, (printf_ypos - CSCREEN_H + 1) * 8);
    }
}

#define PRINTF_LEN 40

static int printf_xpos;

static noinline int printf(const char *fmt, ...)
{
    static const char hexchars[] = "0123456789abcdef";
    char c, buf[PRINTF_LEN + 11 + 1];
    const char *s;
    va_list ap;
    int ival;
    u32 uval;
    int d = 0;
    int i, j;

    va_start(ap, fmt);
    for (d = 0; *fmt; ) {
        int prefix0 = 0;
        int fwidth = 0;

        c = *fmt++;
        if (d < PRINTF_LEN)
            buf[d] = c;

        if (c != '%') {
            if (c == '\n') {
                buf[d] = 0;
                printf_line(printf_xpos, buf);
                d = 0;
                printf_xpos = 0;
                continue;
            }
            d++;
            continue;
        }
        if (d >= PRINTF_LEN)
            continue;

        if (*fmt == '0') {
            prefix0 = 1;
            fmt++;
        }

        while ('1' <= *fmt && *fmt <= '9') {
            fwidth = fwidth * 10 + *fmt - '0';
            fmt++;
        }

        switch (*fmt++) {
        case '%':
            d++;
            break;
        case 'd':
        case 'i':
            ival = va_arg(ap, int);
            if (ival < 0) {
                buf[d++] = '-';
                ival = -ival;
            }
            for (i = 1000000000; i >= 10; i /= 10)
                if (ival >= i)
                    break;
            for (; i >= 10; i /= 10) {
                buf[d++] = '0' + ival / i;
                ival %= i;
            }
            buf[d++] = '0' + ival;
            break;
        case 'x':
            uval = va_arg(ap, int);
            while (fwidth > 1 && uval < (1 << (fwidth - 1) * 4)) {
                buf[d++] = prefix0 ? '0' : ' ';
                fwidth--;
            }
            for (j = 1; j < 8 && uval >= (1 << j * 4); j++)
                ;
            for (j--; j >= 0; j--)
                buf[d++] = hexchars[(uval >> j * 4) & 0x0f];
            break;
        case 's':
            s = va_arg(ap, char *);
            while (*s && d < PRINTF_LEN)
                buf[d++] = *s++;
            break;
        default:
            // don't handle, for now
            d++;
            va_arg(ap, void *);
            break;
        }
    }
    buf[d] = 0;
    va_end(ap);

    if (d != 0) {
        // line without \n
        VDP_drawTextML(buf, APLANE, printf_xpos,
            printf_ypos & (PLANE_H - 1));
        printf_xpos += d;
    }

    return d; // wrong..
}

static const char *exc_names[] = {
    NULL,
    NULL,
    "Bus Error",
    "Address Error",
    "Illegal Instruction",
    "Zero Divide",
    "CHK Instruction",
    "TRAPV Instruction",
    "Privilege Violation",  /*  8  8 */
    "Trace",
    "Line 1010 Emulator",
    "Line 1111 Emulator",
    NULL,
    NULL,
    NULL,
    "Uninitialized Interrupt",
    NULL,                   /* 10 16 */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "Spurious Interrupt",   /* 18 24 */
    "l1 irq",
    "l2 irq",
    "l3 irq",
    "l4 irq",
    "l5 irq",
    "l6 irq",
    "l7 irq",
};

struct exc_frame {
    u32 dr[8];
    u32 ar[8];
    u16 ecxnum; // from handler
    union {
        struct {
            u16 sr;
            u32 pc;
        } g _packed;
        struct {
            u16 fc;
            u32 addr;
            u16 ir;
            u16 sr;
            u32 pc;
        } bae _packed; // bus/address error frame
    };
} _packed;

void exception(const struct exc_frame *f)
{
    int i;

    while (read16(VDP_CTRL_PORT) & 2)
        ;
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DISP);
    /* adjust scroll */
    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write16(VDP_DATA_PORT,
      printf_ypos >= CSCREEN_H ?
        (printf_ypos - CSCREEN_H + 1) * 8 : 0);

    printf("exception %i ", f->ecxnum);
    if (f->ecxnum < ARRAY_SIZE(exc_names) && exc_names[f->ecxnum] != NULL)
        printf("(%s)", exc_names[f->ecxnum]);
    if (f->ecxnum < 4)
        printf(" (%s)", (f->bae.fc & 0x10) ? "r" : "w");
    printf("    \n");

    if (f->ecxnum < 4) {
        printf("  PC: %08x SR: %04x    \n", f->bae.pc, f->bae.sr);
        printf("addr: %08x IR: %04x FC: %02x   \n",
               f->bae.addr, f->bae.ir, f->bae.fc);
    }
    else {
        printf("  PC: %08x SR: %04x    \n", f->g.pc, f->g.sr);
    }
    for (i = 0; i < 8; i++)
        printf("  D%d: %08x A%d: %08x    \n", i, f->dr[i], i, f->ar[i]);
    printf("                       \n");
}

// ---

static void setup_default_palette(void)
{
    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(0));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(15 * 2)); // font normal
    write16(VDP_DATA_PORT, 0xeee);
    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(31 * 2)); // green
    write16(VDP_DATA_PORT, 0x0e0);
    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(47 * 2)); // red
    write16(VDP_DATA_PORT, 0x00e);
}

static void do_setup_dma(const void *src_, u16 words)
{
    u32 src = (u32)src_;
    // VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA);
    VDP_setReg(VDP_DMA_LEN0, words);
    VDP_setReg(VDP_DMA_LEN1, words >> 8);
    VDP_setReg(VDP_DMA_SRC0, src >> 1);
    VDP_setReg(VDP_DMA_SRC1, src >> 9);
    VDP_setReg(VDP_DMA_SRC2, src >> 17);
    // write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(addr) | CTL_WRITE_DMA);
}

static void vdp_wait_for_fifo_empty(void)
{
    while (!(read16(VDP_CTRL_PORT) & 0x200))
        /* fifo not empty */;
}

static void vdp_wait_for_dma_idle(void)
{
    while (read16(VDP_CTRL_PORT) & 2)
        /* dma busy */;
}

static void vdp_wait_for_line_0(void)
{
    // in PAL vcounter reports 0 twice in a frame,
    // so wait for vblank to clear first
    while (!(read16(VDP_CTRL_PORT) & 8))
        /* not blanking */;
    while (read16(VDP_CTRL_PORT) & 8)
        /* blanking */;
    while (read8(VDP_HV_COUNTER) != 0)
        ;
}

static void t_dma_zero_wrap_early(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    u32 *ram = (u32 *)0xff0000;

    do_setup_dma(src + 4, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0) | CTL_WRITE_DMA);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0) | CTL_WRITE_DMA);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0));
    ram[0] = read32(VDP_DATA_PORT);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0xfffc));
    ram[1] = read32(VDP_DATA_PORT);
}

static void t_dma_zero_fill_early(void)
{
    u32 *ram = (u32 *)0xff0000;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);

    VDP_setReg(VDP_AUTOINC, 1);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(1) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    ram[2] = read16(VDP_CTRL_PORT);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0));
    ram[3] = read32(VDP_DATA_PORT);
}

#define expect(ok_, v0_, v1_) \
if ((v0_) != (v1_)) { \
    printf("%s: %08x %08x\n", #v0_, v0_, v1_); \
    ok_ = 0; \
}

#define expect_range(ok_, v0_, vmin_, vmax_) \
if ((v0_) < (vmin_) || (v0_) > (vmax_)) { \
    printf("%s: %02x /%02x-%02x\n", #v0_, v0_, vmin_, vmax_); \
    ok_ = 0; \
}

#define expect_bits(ok_, v0_, val_, mask_) \
if (((v0_) & (mask_)) != (val_)) { \
    printf("%s: %04x & %04x != %04x\n", #v0_, v0_, mask_, val_); \
    ok_ = 0; \
}

static int t_dma_zero_wrap(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    const u32 *ram = (const u32 *)0xff0000;
    int ok = 1;

    expect(ok, ram[0], src[5 + 0x10000/4]);
    expect(ok, ram[1], src[4]);
    return ok;
}

static int t_dma_zero_fill(void)
{
    const u32 *ram = (const u32 *)0xff0000;
    u32 v0 = ram[2] & 2;
    int ok = 1;

    expect(ok, v0, 2);
    expect(ok, ram[3], 0x11111111);
    return ok;
}

static int t_dma_ram_wrap(void)
{
    u32 *ram = (u32 *)0xff0000;
    u32 saved, v0, v1;
    int ok = 1;

    saved = read32(&ram[0x10000/4 - 1]);
    ram[0x10000/4 - 1] = 0x01020304;
    ram[0] = 0x05060708;
    do_setup_dma(&ram[0x10000/4 - 1], 4);
    mem_barrier();
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);

    mem_barrier();
    write32(&ram[0x10000/4 - 1], saved);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x01020304);
    expect(ok, v1, 0x05060708);
    return ok;
}

// test no src reprogram, only len0
static int t_dma_multi(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    u32 v0, v1;
    int ok = 1;

    do_setup_dma(src, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    VDP_setReg(VDP_DMA_LEN0, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x104) | CTL_WRITE_DMA);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, src[0]);
    expect(ok, v1, src[1]);
    return ok;
}

static int t_dma_cram_wrap(void)
{
    u32 *ram = (u32 *)0xff0000;
    u32 v0, v1;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(0));
    write32(VDP_DATA_PORT, 0);

    ram[0] = 0x0ec20ec4;
    ram[1] = 0x0ec60ec8;
    mem_barrier();
    do_setup_dma(ram, 4);
    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(0x7c | 0xff81) | CTL_WRITE_DMA);

    write32(VDP_CTRL_PORT, CTL_READ_CRAM(0x7c));
    v0 = read32(VDP_DATA_PORT) & 0x0eee0eee;
    write32(VDP_CTRL_PORT, CTL_READ_CRAM(0));
    v1 = read32(VDP_DATA_PORT) & 0x0eee0eee;

    setup_default_palette();

    expect(ok, v0, ram[0]);
    expect(ok, v1, ram[1]);
    return ok;
}

static int t_dma_vsram_wrap(void)
{
    u32 *ram32 = (u32 *)0xff0000;
    u16 *ram16 = (u16 *)0xff0000;
    u32 v0, v1;
    int ok = 1;
    int i;

    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write32(VDP_DATA_PORT, 0);

    for (i = 0; i < 0x48/2; i++)
        ram16[i] = i + 1;
    mem_barrier();
    do_setup_dma(ram16, 0x48/2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0x3c | 0xff81) | CTL_WRITE_DMA);

    write32(VDP_CTRL_PORT, CTL_READ_VSRAM(0x3c));
    v0 = read32(VDP_DATA_PORT) & 0x03ff03ff;
    write32(VDP_CTRL_PORT, CTL_READ_VSRAM(0));
    v1 = read32(VDP_DATA_PORT) & 0x03ff03ff;

    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write32(VDP_DATA_PORT, 0);

    expect(ok, v0, ram32[0]);
    expect(ok, v1, ram32[0x48/4 - 1]);
    return ok;
}

static int t_dma_and_data(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    u32 v0, v1;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);

    do_setup_dma(src, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0xfc) | CTL_WRITE_DMA);
    write32(VDP_DATA_PORT, 0x5ec8a248);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0xfc));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, src[0]);
    expect(ok, v1, 0x5ec8a248);
    return ok;
}

static int t_dma_short_cmd(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    u32 v0, v1, v2;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x3ff4));
    write32(VDP_DATA_PORT, 0x10111213);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0xfff0));
    write32(VDP_DATA_PORT, 0x20212223);
    write32(VDP_DATA_PORT, 0x30313233);
    vdp_wait_for_fifo_empty();

    do_setup_dma(src, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0xfff0) | CTL_WRITE_DMA);
    write16(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x3ff4) >> 16);
    write32(VDP_DATA_PORT, 0x40414243);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x3ff4));
    v0 = read32(VDP_DATA_PORT);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0xfff0));
    v1 = read32(VDP_DATA_PORT);
    v2 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x10111213);
    expect(ok, v1, src[0]);
    expect(ok, v2, 0x40414243);
    return ok;
}

static int t_dma_fill3_odd(void)
{
    u32 v0, v1, v2;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x101) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);
    v2 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x22110000);
    expect(ok, v1, 0x00111100);
    expect(ok, v2, 0x00000011);
    return ok;
}

static int t_dma_fill3_even(void)
{
    u32 v0, v1, v2;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);
    v2 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x11221100);
    expect(ok, v1, 0x00000011);
    expect(ok, v2, 0x11000000);
    return ok;
}

static unused int t_dma_fill3_vsram(void)
{
    u32 v0, v1, v2;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);

    write16(VDP_DATA_PORT, 0x0111);
    write16(VDP_DATA_PORT, 0x0222);
    write16(VDP_DATA_PORT, 0x0333);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(1) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x0102);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VSRAM(0));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);
    v2 = read32(VDP_DATA_PORT);

    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write32(VDP_DATA_PORT, 0);

    expect(ok, v0, 0x01020000);
    expect(ok, v1, 0x01110111);
    expect(ok, v2, 0x00000111);
    return ok;
}

static int t_dma_fill_dis(void)
{
    u32 v0, v1;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_DATA_PORT, 0);

    VDP_setReg(VDP_DMA_LEN0, 1);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD);
    write16(VDP_DATA_PORT, 0x1122);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write16(VDP_DATA_PORT, 0x3344);
    vdp_wait_for_dma_idle();

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0);
    expect(ok, v1, 0);
    return ok;
}

static int t_dma_fill_src(void)
{
    const u32 *src = (const u32 *)0x3c0000;
    u32 v0, v1;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);

    // do_setup_dma(src, 2); // hang, can't write src2 twice
    VDP_setReg(VDP_DMA_LEN0, 2);
    VDP_setReg(VDP_DMA_SRC0, (u32)src >> 1);
    VDP_setReg(VDP_DMA_SRC1, (u32)src >> 9);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    vdp_wait_for_dma_idle();

    VDP_setReg(VDP_DMA_LEN0, 2);
    VDP_setReg(VDP_DMA_SRC2, (u32)src >> 17);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x104) | CTL_WRITE_DMA);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x11220011);
    expect(ok, v1, src[1]);
    return ok;
}

// (((a & 2) >> 1) ^ 1) | ((a & $400) >> 9) | (a & $3FC) | ((a & $1F800) >> 1)
static int t_dma_128k(void)
{
    u16 *ram = (u16 *)0xff0000;
    u32 v0, v1;
    int ok = 1;

    ram[0] = 0x5a11;
    ram[1] = 0x5a22;
    ram[2] = 0x5a33;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0x01020304);
    write32(VDP_DATA_PORT, 0x05060708);
    vdp_wait_for_fifo_empty();

    mem_barrier();
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_128K);
    do_setup_dma(ram, 3);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);
    v1 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x22110304);
    expect(ok, v1, 0x05330708);
    return ok;
}

static int t_vdp_128k_b16(void)
{
    u32 v0, v1;
    int ok = 1;

    VDP_setReg(VDP_AUTOINC, 0);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x8100));
    write32(VDP_DATA_PORT, 0x01020304);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x10100));
    write32(VDP_DATA_PORT, 0x05060708);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_128K);
    write16(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) >> 16); // note: upper cmd
    write32(VDP_DATA_PORT, 0x11223344);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x10102));
    write32(VDP_DATA_PORT, 0x55667788);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x8100));
    v0 = read16(VDP_DATA_PORT);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x0100));
    v1 = read16(VDP_DATA_PORT);

    VDP_setReg(VDP_AUTOINC, 2);

    expect(ok, v0, 0x8844);
    expect(ok, v1, 0x0708);
    return ok;
}

static unused int t_vdp_128k_b16_inc(void)
{
    u32 v0, v1;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0));
    write32(VDP_DATA_PORT, 0x01020304);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x8000));
    write32(VDP_DATA_PORT, 0x05060708);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0xfffe));
    write32(VDP_DATA_PORT, 0x090a0b0c);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_128K);
    write16(VDP_CTRL_PORT, CTL_WRITE_VRAM(0) >> 16); // note: upper cmd
    write16(VDP_DATA_PORT, 0x1122);
    vdp_wait_for_fifo_empty();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0));
    v0 = read32(VDP_DATA_PORT);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x8000));
    v1 = read32(VDP_DATA_PORT);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0));
    write32(VDP_DATA_PORT, 0);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x8000));
    write32(VDP_DATA_PORT, 0);

    expect(ok, v0, 0x0b0c0304); // XXX: no 22 anywhere?
    expect(ok, v1, 0x05060708);
    return ok;
}

static int t_vdp_reg_cmd(void)
{
    u32 v0;
    int ok = 1;

    VDP_setReg(VDP_AUTOINC, 0);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0x01020304);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write32(VDP_DATA_PORT, 0x05060708);

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x0100));
    v0 = read16(VDP_DATA_PORT);

    expect(ok, v0, 0x0304);
    return ok;
}

static int t_vdp_sr_vb(void)
{
    u16 sr[4];
    int ok = 1;

    while (read8(VDP_HV_COUNTER) != 242)
        ;
    sr[0] = read16(VDP_CTRL_PORT);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD);
    sr[1] = read16(VDP_CTRL_PORT);
    while (read8(VDP_HV_COUNTER) != 4)
        ;
    sr[2] = read16(VDP_CTRL_PORT);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    sr[3] = read16(VDP_CTRL_PORT);

    expect_bits(ok, sr[0], SR_VB, SR_VB);
    expect_bits(ok, sr[1], SR_VB, SR_VB);
    expect_bits(ok, sr[2], SR_VB, SR_VB);
    expect_bits(ok, sr[3], 0, SR_VB);
    return ok;
}

/* z80 tests assume busreq state */
static int t_z80mem_long_mirror(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    write8(&zram[0x1100], 0x11);
    write8(&zram[0x1101], 0x22);
    write8(&zram[0x1102], 0x33);
    write8(&zram[0x1103], 0x44);
    mem_barrier();
    write32(&zram[0x3100], 0x55667788);
    mem_barrier();

    expect(ok, zram[0x1100], 0x55);
    expect(ok, zram[0x1101], 0x22);
    expect(ok, zram[0x1102], 0x77);
    expect(ok, zram[0x1103], 0x44);
    return ok;
}

static int t_z80mem_noreq_w(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    write8(&zram[0x1100], 0x11);
    mem_barrier();
    write16(0xa11100, 0x000);
    write8(&zram[0x1100], 0x22);
    mem_barrier();

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;

    expect(ok, zram[0x1100], 0x11);
    return ok;
}

#define Z80_CP_CYCLES(b) (118 + ((b) - 1) * 21 + 26 + 17)

static int t_z80mem_vdp_r(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0x11223344);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));

    zram[0x1000] = 1; // cp
    write16_z80le(&zram[0x1002], 0x7f00); // src
    write16_z80le(&zram[0x1004], 0x1100); // dst
    write16_z80le(&zram[0x1006], 2); // len
    zram[0x1100] = zram[0x1101] = zram[0x1102] = 0x5a;
    mem_barrier();
    write16(0xa11100, 0x000);
    burn10(Z80_CP_CYCLES(2) * 15 / 7 * 2 / 10);

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;

    expect(ok, zram[0x1000], 0);
    expect(ok, zram[0x1100], 0x11);
    expect(ok, zram[0x1101], 0x44);
    expect(ok, zram[0x1102], 0x5a);
    return ok;
}

static unused int t_z80mem_vdp_w(void)
{
    u8 *zram = (u8 *)0xa00000;
    u32 v0;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0x11223344);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    vdp_wait_for_fifo_empty();

    zram[0x1000] = 1; // cp
    write16_z80le(&zram[0x1002], 0x1100); // src
    write16_z80le(&zram[0x1004], 0x7f00); // dst
    write16_z80le(&zram[0x1006], 2); // len
    zram[0x1100] = 0x55;
    zram[0x1101] = 0x66;
    mem_barrier();
    write16(0xa11100, 0x000);
    burn10(Z80_CP_CYCLES(2) * 15 / 7 * 2 / 10);

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);

    expect(ok, zram[0x1000], 0);
    expect(ok, v0, 0x55556666);
    return ok;
}

static int t_tim_loop(void)
{
    u8 vcnt;
    int ok = 1;

    vdp_wait_for_line_0();
    burn10(488*220/10);
    vcnt = read8(VDP_HV_COUNTER);
    mem_barrier();

    //expect_range(ok, vcnt, 0x80, 0x80);
    expect(ok, vcnt, 223);
    return ok;
}

#define Z80_RD_V_CYCLES(b) (132 + (b) * 38 + 50 + 17)

// 80 80 91 95-96
static void z80_read_loop(u8 *zram, u16 src)
{
    const int pairs = 512 + 256;

    zram[0x1000] = 2; // read loop, save vcnt
    write16_z80le(&zram[0x1002], src); // src
    write16_z80le(&zram[0x1004], 0x1100); // vcnt dst
    write16_z80le(&zram[0x1006], pairs); // reads/2
    zram[0x1100] = 0;
    mem_barrier();

    vdp_wait_for_line_0();
    write16(0xa11100, 0x000);
    burn10(Z80_RD_V_CYCLES(pairs) * 15 / 7 * 4 / 10);

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;
}

static int t_tim_z80_ram(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    z80_read_loop(zram, 0);

    expect(ok, zram[0x1000], 0);
    expect_range(ok, zram[0x1100], 0x80, 0x80);
    return ok;
}

static int t_tim_z80_ym(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    z80_read_loop(zram, 0x4000);

    expect(ok, zram[0x1000], 0);
    expect_range(ok, zram[0x1100], 0x80, 0x80);
    return ok;
}

static int t_tim_z80_vdp(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    z80_read_loop(zram, 0x7f08);

    expect(ok, zram[0x1000], 0);
#ifndef PICO
    expect_range(ok, zram[0x1100], 0x91, 0x91);
#else
    expect_range(ok, zram[0x1100], 0x8e, 0x91);
#endif
    return ok;
}

static int t_tim_z80_bank_rom(void)
{
    u8 *zram = (u8 *)0xa00000;
    int i, ok = 1;

    for (i = 0; i < 17; i++)
        write8(0xa06000, 0); // bank 0

    z80_read_loop(zram, 0x8000);

    expect(ok, zram[0x1000], 0);
#ifndef PICO
    expect_range(ok, zram[0x1100], 0x95, 0x96);
#else
    expect_range(ok, zram[0x1100], 0x93, 0x96);
#endif
    return ok;
}

/* borderline too slow */
#if 0
static void test_vcnt_vb(void)
{
    const u32 *srhv = (u32 *)0xc00006; // to read SR and HV counter
    u32 *ram = (u32 *)0xff0000;
    u16 vcnt, vcnt_expect = 0;
    u16 sr, count = 0;
    u32 val, old;

    vdp_wait_for_line_0();
    old = read32(srhv);
    *ram++ = old;
    for (;;) {
        val = read32(srhv);
        vcnt = val & 0xff00;
        if (vcnt == vcnt_expect)
            continue;
        sr = val >> 16;
        if (vcnt == 0 && !(sr & SR_VB)) // not VB
            break; // wrapped to start of frame
//        count++;
        vcnt_expect += 0x100;
        if (vcnt == vcnt_expect && !((sr ^ (old >> 16)) & SR_VB)) {
            old = val;
            continue;
        }
        // should have a vcnt jump here
        *ram++ = old;
        *ram++ = val;
        vcnt_expect = vcnt;
        old = val;
    }
    *ram++ = val;
    *ram = count;
    mem_barrier();
}
#endif

static int t_tim_vcnt(void)
{
    const u32 *ram32 = (u32 *)0xff0000;
    const u8 *ram = (u8 *)0xff0000;
    u8 pal = read8(0xa10001) & 0x40;
    u8 vc_jmp_b = pal ? 0x02 : 0xea;
    u8 vc_jmp_a = pal ? 0xca : 0xe5;
    u16 lines = pal ? 313 : 262;
    int ok = 1;

    test_vcnt_vb();
    expect(ok, ram[0*4+2], 0); // line 0
    expect_bits(ok, ram[0*4+1], 0, SR_VB);
    expect(ok, ram[1*4+2], 223); // last no blank
    expect_bits(ok, ram[1*4+1], 0, SR_VB);
    expect(ok, ram[2*4+2], 224); // 1st blank
    expect_bits(ok, ram[2*4+1], SR_VB, SR_VB);
    expect(ok, ram[3*4+2], vc_jmp_b); // before jump
    expect_bits(ok, ram[3*4+1], SR_VB, SR_VB);
    expect(ok, ram[4*4+2], vc_jmp_a); // after jump
    expect_bits(ok, ram[4*4+1], SR_VB, SR_VB);
    expect(ok, ram[5*4+2], 0xfe); // before vb clear
    expect_bits(ok, ram[5*4+1], SR_VB, SR_VB);
    expect(ok, ram[6*4+2], 0xff); // after vb clear
    expect_bits(ok, ram[6*4+1], 0, SR_VB);
    expect(ok, ram[7*4+2], 0); // next line 0
    expect_bits(ok, ram[7*4+1], 0, SR_VB);
    expect(ok, ram32[8], lines - 1);
    return ok;
}

static int t_tim_hblank_h40(void)
{
    const u8 *r = (u8 *)0xff0000;
    int ok = 1;

    test_hb();

    // set: 0-2
    expect_bits(ok, r[2], SR_HB, SR_HB);
    expect_bits(ok, r[5], SR_HB, SR_HB);
    // <wait>
    expect_bits(ok, r[7], SR_HB, SR_HB);
    // clear: 8-11
    expect_bits(ok, r[12], 0, SR_HB);
    return ok;
}

static int t_tim_hblank_h32(void)
{
    const u8 *r = (u8 *)0xff0000;
    int ok = 1;

    VDP_setReg(VDP_MODE4, 0x00);
    test_hb();
    VDP_setReg(VDP_MODE4, 0x81);

#ifndef PICO
    expect_bits(ok, r[0], 0, SR_HB);
#endif
    // set: 1-4
    expect_bits(ok, r[4], SR_HB, SR_HB);
    expect_bits(ok, r[5], SR_HB, SR_HB);
    // <wait>
    expect_bits(ok, r[8], SR_HB, SR_HB);
    // clear: 9-11
    expect_bits(ok, r[12], 0, SR_HB);
    return ok;
}

static int t_tim_vdp_as_vram_w(void)
{
    int ok = 1;
    u8 vcnt;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    vdp_wait_for_line_0();
    write16_x16(VDP_DATA_PORT, 112*18 / 16, 0);
    vcnt = read8(VDP_HV_COUNTER);
    mem_barrier();

    expect(ok, vcnt, 112*2-1);
    return ok;
}

static int t_tim_vdp_as_cram_w(void)
{
    int ok = 1;
    u8 vcnt;

    write32(VDP_CTRL_PORT, CTL_WRITE_CRAM(0));
    vdp_wait_for_line_0();
    write16_x16(VDP_DATA_PORT, 112*18 / 16, 0);
    vcnt = read8(VDP_HV_COUNTER);
    mem_barrier();

    setup_default_palette();

#ifndef PICO
    expect(ok, vcnt, 112);
#else
    expect_range(ok, vcnt, 111, 112);
#endif
    return ok;
}

static int t_irq_hint(void)
{
    u16 *ram = (u16 *)0xfff000;
    u8 *ram8 = (u8 *)0xfff000;
    u16 v_p, cnt_p;
    int ok = 1;

    // for more fun, disable the display
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD);

    ram[0] = ram[1] = ram[2] = 0;
    memcpy_((void *)0xff0100, test_hint, test_hint_end - test_hint);
    VDP_setReg(10, 0);
    while (read8(VDP_HV_COUNTER) != 100)
        ;
    while (read8(VDP_HV_COUNTER) != 229)
        ;
    // take the pending irq
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS | VDP_MODE1_IE1);
    move_sr(0x2000);
    burn10(488 * 2 / 10);
    move_sr(0x2700);
    v_p = ram8[2];
    cnt_p = ram[0];
    ram[0] = ram[1] = ram[2] = 0;
    // count irqs
    move_sr(0x2000);
    while (read8(VDP_HV_COUNTER) != 4)
        ;
    while (read8(VDP_HV_COUNTER) != 228)
        ;
    move_sr(0x2700);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    expect(ok, v_p, 229);      // pending irq trigger
    expect(ok, cnt_p, 1);
    expect(ok, ram[0], 225);   // count
    expect(ok, ram8[2], 0);    // first line
    expect(ok, ram8[4], 224);  // last line
    return ok;
}

static int t_irq_ack_v_h(void)
{
    u16 *ram = (u16 *)0xfff000;
    u8 *ram8 = (u8 *)0xfff000;
    u16 s0, s1, s2;
    int ok = 1;

    ram[0] = ram[1] = ram[2] =
    ram[4] = ram[5] = ram[6] = 0;
    memcpy_((void *)0xff0100, test_hint, test_hint_end - test_hint);
    memcpy_((void *)0xff0140, test_vint, test_vint_end - test_vint);
    VDP_setReg(10, 0);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS | VDP_MODE1_IE1);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_IE0);
    while (read8(VDP_HV_COUNTER) != 100)
        ;
    while (read8(VDP_HV_COUNTER) != 226)
        ;
    s0 = read16(VDP_CTRL_PORT);
    s1 = move_sr_and_read(0x2500, VDP_CTRL_PORT);
    burn10(666 / 10);
    s2 = move_sr_and_read(0x2000, VDP_CTRL_PORT);
    burn10(488 / 10);
    move_sr(0x2700);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    expect(ok, ram[4], 1);     // vint count
    expect(ok, ram8[10], 226); // vint line
    expect(ok, ram[0], 1);     // hint count
    expect(ok, ram8[2], 228);  // hint line
    expect_bits(ok, s0, SR_F, SR_F);
    expect_bits(ok, s1, 0, SR_F);
    expect_bits(ok, s2, 0, SR_F);
    return ok;
}

static int t_irq_ack_v_h_2(void)
{
    u16 *ram = (u16 *)0xfff000;
    u8 *ram8 = (u8 *)0xfff000;
    u16 s0, s1;
    int ok = 1;

    ram[0] = ram[1] = ram[2] =
    ram[4] = ram[5] = ram[6] = 0;
    memcpy_((void *)0xff0100, test_hint, test_hint_end - test_hint);
    memcpy_((void *)0xff0140, test_vint, test_vint_end - test_vint);
    VDP_setReg(10, 0);
    while (read8(VDP_HV_COUNTER) != 100)
        ;
    while (read8(VDP_HV_COUNTER) != 226)
        ;
    s0 = read16(VDP_CTRL_PORT);
    test_v_h_2();
    s1 = read16(VDP_CTRL_PORT);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    expect(ok, ram[4], 2);     // vint count
    expect(ok, ram8[10], 226); // vint line
    expect(ok, ram[0], 1);     // hint count
    expect(ok, ram8[2], 227);  // hint line
    expect_bits(ok, s0, SR_F, SR_F);
    expect_bits(ok, s1, 0, SR_F);
    return ok;
}

static int t_irq_ack_h_v(void)
{
    u16 *ram = (u16 *)0xfff000;
    u8 *ram8 = (u8 *)0xfff000;
    u16 s0, s1, s[4];
    int ok = 1;

    ram[0] = ram[1] = ram[2] =
    ram[4] = ram[5] = ram[6] = 0;
    memcpy_((void *)0xff0100, test_hint, test_hint_end - test_hint);
    memcpy_((void *)0xff0140, test_vint, test_vint_end - test_vint);
    VDP_setReg(10, 0);
    while (read8(VDP_HV_COUNTER) != 100)
        ;
    while (read8(VDP_HV_COUNTER) != 226)
        ;
    s0 = read16(VDP_CTRL_PORT);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS | VDP_MODE1_IE1);
    move_sr(0x2000);
    burn10(666 / 10);
    s1 = read16(VDP_CTRL_PORT);
    write_and_read1(VDP_CTRL_PORT, 0x8000 | (VDP_MODE2 << 8)
                     | VDP_MODE2_MD | VDP_MODE2_IE0, s);
    move_sr(0x2700);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    expect(ok, ram[0], 1);     // hint count
    expect(ok, ram8[2], 226);  // hint line
    expect(ok, ram[4], 1);     // vint count
    expect(ok, ram8[10], 228); // vint line
    expect_bits(ok, s0, SR_F, SR_F);
    expect_bits(ok, s1, SR_F, SR_F);
    expect_bits(ok, s[0], SR_F, SR_F);
    expect_bits(ok, s[1], SR_F, SR_F);
    expect_bits(ok, s[2], 0, SR_F);
    expect_bits(ok, s[3], 0, SR_F);
    return ok;
}

static int t_irq_ack_h_v_2(void)
{
    u16 *ram = (u16 *)0xfff000;
    u8 *ram8 = (u8 *)0xfff000;
    u16 s0, s1;
    int ok = 1;

    ram[0] = ram[1] = ram[2] =
    ram[4] = ram[5] = ram[6] = 0;
    memcpy_((void *)0xff0100, test_hint, test_hint_end - test_hint);
    memcpy_((void *)0xff0140, test_vint, test_vint_end - test_vint);
    VDP_setReg(10, 0);
    while (read8(VDP_HV_COUNTER) != 100)
        ;
    while (read8(VDP_HV_COUNTER) != 226)
        ;
    s0 = read16(VDP_CTRL_PORT);
    test_h_v_2();
    s1 = read16(VDP_CTRL_PORT);
    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    expect(ok, ram[0], 2);     // hint count
    expect(ok, ram8[2], 226);  // hint first line
    expect(ok, ram8[4], 226);  // hint last line
    expect(ok, ram[4], 0);     // vint count
    expect(ok, ram8[10], 0);   // vint line
    expect_bits(ok, s0, SR_F, SR_F);
    expect_bits(ok, s1, 0, SR_F);
    return ok;
}

static void t_irq_f_flag(void)
{
    memcpy_((void *)0xff0140, test_f_vint, test_f_vint_end - test_f_vint);
    memset_((void *)0xff0000, 0, 10);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_IE0 | VDP_MODE2_DISP);
    test_f();
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
}

static int t_irq_f_flag_h40(void)
{
    u8 f, *r = (u8 *)0xff0000;
    int ok = 1;

    t_irq_f_flag();

    expect_bits(ok, r[0], 0, SR_F);
    expect_bits(ok, r[1], 0, SR_F);
    expect_bits(ok, r[2], 0, SR_F);
    // hits 1-3 times in range 3-9, usually ~5
    f = r[3] | r[4] | r[5] | r[6] | r[7];

    expect_bits(ok, r[10], 0, SR_F);
    expect_bits(ok, r[11], 0, SR_F);
    expect_bits(ok, f, SR_F, SR_F);
    return ok;
}

static int t_irq_f_flag_h32(void)
{
    u8 f, *r = (u8 *)0xff0000;
    int ok = 1;

    VDP_setReg(VDP_MODE4, 0x00);
    t_irq_f_flag();
    VDP_setReg(VDP_MODE4, 0x81);

    expect_bits(ok, r[0], 0, SR_F);
    expect_bits(ok, r[1], 0, SR_F);
    // hits 1-3 times in range 2-7, usually 3
    f = r[2] | r[3] | r[4] | r[5] | r[6] | r[7];

    expect_bits(ok, r[8], 0, SR_F);
    expect_bits(ok, r[9], 0, SR_F);
    expect_bits(ok, r[10], 0, SR_F);
    expect_bits(ok, r[11], 0, SR_F);
    expect_bits(ok, f, SR_F, SR_F);
    return ok;
}

// 32X

static int t_32x_init(void)
{
    void (*do_32x_enable)(void) = (void *)0xff0040;
    u32 M_OK = MKLONG('M','_','O','K');
    u32 S_OK = MKLONG('S','_','O','K');
    u32 *r = (u32 *)0xa15100;
    u16 *r16 = (u16 *)r;
    int i, ok = 1;

    //v1070 = read32(0x1070);

    /* what does REN mean exactly?
     * Seems to be sometimes clear after reset */
    for (i = 0; i < 1000000; i++)
        if (read16(r16) & 0x80)
            break;
    expect(ok, r16[0x00/2], 0x82);
    expect(ok, r16[0x02/2], 0);
    expect(ok, r16[0x04/2], 0);
    expect(ok, r16[0x06/2], 0);
    expect(ok, r[0x14/4], 0);
    expect(ok, r[0x18/4], 0);
    expect(ok, r[0x1c/4], 0);
    write32(&r[0x20/4], 0); // master resp
    write32(&r[0x24/4], 0); // slave resp
    write32(&r[0x28/4], 0);
    write32(&r[0x2c/4], 0);

    // could just set RV, but BIOS reads ROM, so can't
    memcpy_(do_32x_enable, x32x_enable,
            x32x_enable_end - x32x_enable);
    do_32x_enable();

    expect(ok, r16[0x00/2], 0x83);
    expect(ok, r16[0x02/2], 0);
    expect(ok, r16[0x04/2], 0);
    expect(ok, r16[0x06/2], 1); // RV
    expect(ok, r[0x14/4], 0);
    expect(ok, r[0x18/4], 0);
    expect(ok, r[0x1c/4], 0);
    expect(ok, r[0x20/4], M_OK);
    while (!read16(&r16[0x24/2]))
        ;
    expect(ok, r[0x24/4], S_OK);
    return ok;
}

static void x32_cmd(enum x32x_cmd cmd, u16 is_slave)
{
    u16 v, *r = (u16 *)0xa15120;
    u16 cmd_s = cmd | (is_slave << 15);
    int i;
    write16(r, cmd_s);
    mem_barrier();
    for (i = 0; i < 10000 && (v = read16(r)) == cmd_s; i++)
        burn10(1);
    if (v != 0) {
        printf("cmd clr: %x\n", v);
        write16(r, 0);
    }
}

static int t_32x_echo(void)
{
    u16 *r = (u16 *)0xa15120;
    int ok = 1;

    write16(&r[0x02/2], 0x1234);
    x32_cmd(CMD_ECHO, 0);
    expect(ok, r[0x04/2], 0x1234);
    write16(&r[0x02/2], 0x2345);
    write16(&r[0x04/2], 0);
    x32_cmd(CMD_ECHO, 1);
    expect(ok, r[0x04/2], 0xa345);
    expect(ok, r[0x0c/2], 0);
    expect(ok, r[0x0e/2], 0);
    return ok;
}

static int t_32x_md_rom(void)
{
    u32 *rl = (u32 *)0;
    int ok = 1;

    expect(ok, rl[0x004/4], 0x880200);
    expect(ok, rl[0x100/4], 0x53454741);
    expect(ok, rl[0x70/4], 0);
    write32(&rl[0x70/4], 0xa5123456);
    write32(&rl[0x78/4], ~0);
    mem_barrier();
    expect(ok, rl[0x78/4], 0x8802ae);
    expect(ok, rl[0x70/4], 0xa5123456);
    //expect(ok, rl[0x1070/4], v1070);
    write32(&rl[0x70/4], 0);
    // with RV 0x880000/0x900000 hangs, can't test
    return ok;
}

enum {
    T_MD = 0,
    T_32 = 1, // 32X
};

static const struct {
    u8 type;
    int (*test)(void);
    const char *name;
} g_tests[] = {
    { T_MD, t_dma_zero_wrap,       "dma zero len + wrap" },
    { T_MD, t_dma_zero_fill,       "dma zero len + fill" },
    { T_MD, t_dma_ram_wrap,        "dma ram wrap" },
    { T_MD, t_dma_multi,           "dma multi" },
    { T_MD, t_dma_cram_wrap,       "dma cram wrap" },
    { T_MD, t_dma_vsram_wrap,      "dma vsram wrap" },
    { T_MD, t_dma_and_data,        "dma and data" },
    { T_MD, t_dma_short_cmd,       "dma short cmd" },
    { T_MD, t_dma_fill3_odd,       "dma fill3 odd" },
    { T_MD, t_dma_fill3_even,      "dma fill3 even" },
#ifndef PICO // later
    { T_MD, t_dma_fill3_vsram,     "dma fill3 vsram" },
#endif
    { T_MD, t_dma_fill_dis,        "dma fill disabled" },
    { T_MD, t_dma_fill_src,        "dma fill src incr" },
    { T_MD, t_dma_128k,            "dma 128k mode" },
    { T_MD, t_vdp_128k_b16,        "vdp 128k addr bit16" },
    // { t_vdp_128k_b16_inc,    "vdp 128k bit16 inc" }, // mystery
    { T_MD, t_vdp_reg_cmd,         "vdp reg w cmd reset" },
    { T_MD, t_vdp_sr_vb,           "vdp status reg vb" },
    { T_MD, t_z80mem_long_mirror,  "z80 ram long mirror" },
    { T_MD, t_z80mem_noreq_w,      "z80 ram noreq write" },
    { T_MD, t_z80mem_vdp_r,        "z80 vdp read" },
    // { t_z80mem_vdp_w,        "z80 vdp write" }, // hang
    { T_MD, t_tim_loop,            "time loop" },
    { T_MD, t_tim_z80_ram,         "time z80 ram" },
    { T_MD, t_tim_z80_ym,          "time z80 ym2612" },
    { T_MD, t_tim_z80_vdp,         "time z80 vdp" },
    { T_MD, t_tim_z80_bank_rom,    "time z80 bank rom" },
    { T_MD, t_tim_vcnt,            "time V counter" },
    { T_MD, t_tim_hblank_h40,      "time hblank h40" },
    { T_MD, t_tim_hblank_h32,      "time hblank h32" },
    { T_MD, t_tim_vdp_as_vram_w,   "time vdp vram w" },
    { T_MD, t_tim_vdp_as_cram_w,   "time vdp cram w" },
    { T_MD, t_irq_hint,            "irq4 / line" },
    { T_MD, t_irq_ack_v_h,         "irq ack v-h" },
    { T_MD, t_irq_ack_v_h_2,       "irq ack v-h 2" },
    { T_MD, t_irq_ack_h_v,         "irq ack h-v" },
    { T_MD, t_irq_ack_h_v_2,       "irq ack h-v 2" },
    { T_MD, t_irq_f_flag_h40,      "irq f flag h40" },
    { T_MD, t_irq_f_flag_h32,      "irq f flag h32" },

    // the first one enables 32X, so should be kept
    { T_32, t_32x_init,            "32x init" },
    { T_32, t_32x_echo,            "32x echo" },
    { T_32, t_32x_md_rom,          "32x rom" },
};

static void setup_z80(void)
{
    u8 *zram = (u8 *)0xa00000;
    int i, len;

    /* z80 */
    write16(0xa11100, 0x100);
    write16(0xa11200, 0x100);

    while (read16(0xa11100) & 0x100)
        ;

    // load the default test program, clear it's data
    len = z80_test_end - z80_test;
    for (i = 0; i < len; i++)
        write8(&zram[i], z80_test[i]);
    for (i = 0x1000; i < 0x1007; i++)
        write8(&zram[i], 0);

    // reset
    write16(0xa11200, 0x000);
    write16(0xa11100, 0x000);
    burn10(1);
    write16(0xa11200, 0x100);
    burn10(1);

    // take back the bus
    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;
}

static void wait_next_vsync(void)
{
    while (read16(VDP_CTRL_PORT) & 8)
        /* blanking */;
    while (!(read16(VDP_CTRL_PORT) & 8))
        /* not blanking */;
}

static unused int hexinc(char *c)
{
    (*c)++;
    if (*c > 'f') {
        *c = '0';
        return 1;
    }
    if (*c == '9' + 1)
        *c = 'a';
    return 0;
}

int main()
{
    int passed = 0;
    int skipped = 0;
    int have_32x;
    int ret;
    u8 v8;
    int i;

    setup_z80();

    /* io */
    write8(0xa10009, 0x40);

    /* setup VDP */
    while (read16(VDP_CTRL_PORT) & 2)
        ;

    VDP_setReg(VDP_MODE1, VDP_MODE1_PS);
    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA);
    VDP_setReg(VDP_MODE3, 0x00);
    VDP_setReg(VDP_MODE4, 0x81);
    VDP_setReg(VDP_NT_SCROLLA, APLANE >> 10);
    VDP_setReg(VDP_NT_SCROLLB, BPLANE >> 13);
    VDP_setReg(VDP_SAT_BASE, SLIST >> 9);
    VDP_setReg(VDP_HSCROLL, HSCRL >> 10);
    VDP_setReg(VDP_AUTOINC, 2);
    VDP_setReg(VDP_SCROLLSZ, 0x01);
    VDP_setReg(VDP_BACKDROP, 0);

    // early tests
    t_dma_zero_wrap_early();
    t_dma_zero_fill_early();

    /* pattern 0 */
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0));
    for (i = 0; i < 32 / 4; i++)
        write32(VDP_DATA_PORT, 0);

    /* clear name tables */
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(APLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(VDP_DATA_PORT, 0);

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(BPLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(VDP_DATA_PORT, 0);

    /* SAT, h. scroll */
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(SLIST));
    write32(VDP_DATA_PORT, 0);

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(HSCRL));
    write32(VDP_DATA_PORT, 0);

    /* scroll plane vscroll */
    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
    write32(VDP_DATA_PORT, 0);
    printf_ypos = 1;

    /* load font */
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(TILE_FONT_BASE));
    for (i = 0; i < FONT_LEN * 32 / 4; i++)
        write32(VDP_DATA_PORT, font_base[i]);

    /* set colors */
    setup_default_palette();

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);

    have_32x = read32(0xa130ec) == MKLONG('M','A','R','S');
    v8 = read8(0xa10001);
    printf("MD version: %02x %s %s %s\n", v8,
        (v8 & 0x80) ? "world" : "jap",
        (v8 & 0x40) ? "pal" : "ntsc",
        have_32x ? "32X" : "");

    for (i = 0; i < ARRAY_SIZE(g_tests); i++) {
        // print test number if we haven't scrolled away
        if (printf_ypos < CSCREEN_H) {
            int old_ypos = printf_ypos;
            printf_ypos = 0;
            printf("%02d/%02d", i, ARRAY_SIZE(g_tests));
            printf_ypos = old_ypos;
            printf_xpos = 0;
        }
        if ((g_tests[i].type & T_32) && !have_32x) {
            skipped++;
            continue;
        }
        ret = g_tests[i].test();
        if (ret != 1) {
            text_pal = 2;
            printf("failed %d: %s\n", i, g_tests[i].name);
            text_pal = 0;
        }
        else
            passed++;
    }

    text_pal = 0;
    printf("%d/%d passed, %d skipped.\n",
           passed, ARRAY_SIZE(g_tests), skipped);

    printf_ypos = 0;
    printf("     ");

    while (!(get_input() & BTNM_A))
        wait_next_vsync();


    {
        char c[3] = { '0', '0', '0' };
        short hscroll = 0, vscroll = 0;
        short hsz = 1, vsz = 0;
        short cellmode = 0;

        write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(APLANE));

#if 0
        for (i = 0, c[0] = 'a'; i < 8 * 1024 / 2; i++) {
            write16(VDP_DATA_PORT, (u16)c[0] - 32 + TILE_FONT_BASE / 32);
            c[0]++;
            if (c[0] == 'z' + 1)
                c[0] = 'a';
        }
#else
        for (i = 0; i < 8 * 1024 / 2 / 4; i++) {
            write16(VDP_DATA_PORT, (u16)'.'  - 32 + TILE_FONT_BASE / 32);
            write16(VDP_DATA_PORT, (u16)c[2] - 32 + TILE_FONT_BASE / 32);
            write16(VDP_DATA_PORT, (u16)c[1] - 32 + TILE_FONT_BASE / 32);
            write16(VDP_DATA_PORT, (u16)c[0] - 32 + TILE_FONT_BASE / 32);
            if (hexinc(&c[0]))
                if (hexinc(&c[1]))
                    hexinc(&c[2]);
        }
#endif
        while (get_input() & BTNM_A)
            wait_next_vsync();

        wait_next_vsync();
        for (;;) {
            int b = get_input();

            if (b & BTNM_C) {
                hscroll = 1, vscroll = -1;
                do {
                    wait_next_vsync();
                } while (get_input() & BTNM_C);
                cellmode ^= 1;
            }
            if (b & (BTNM_L | BTNM_R | BTNM_C)) {
                hscroll += (b & BTNM_L) ? 1 : -1;
                write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(HSCRL));
                write16(VDP_DATA_PORT, hscroll);
            }
            if (b & (BTNM_U | BTNM_D | BTNM_C)) {
                vscroll += (b & BTNM_U) ? -1 : 1;
                write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(0));
                if (cellmode) {
                    int end = (int)vscroll + 21;
                    for (i = vscroll; i < end; i++)
                        write32(VDP_DATA_PORT, i << 17);
                    VDP_setReg(VDP_MODE3, 0x04);
                }
                else {
                    write16(VDP_DATA_PORT, vscroll);
                    VDP_setReg(VDP_MODE3, 0x00);
                }
            }
            if (b & BTNM_A) {
                hsz = (hsz + 1) & 3;
                do {
                    wait_next_vsync();
                } while (get_input() & BTNM_A);
            }
            if (b & BTNM_B) {
                vsz = (vsz + 1) & 3;
                do {
                    wait_next_vsync();
                } while (get_input() & BTNM_B);
            }
            VDP_setReg(VDP_SCROLLSZ, (vsz << 4) | hsz);

            printf_xpos = 1;
            printf_ypos = 0;
            text_pal = 1;
            printf(" %d %d ", hsz, vsz);

            wait_next_vsync();
        }
    }

    for (;;)
        ;

    return 0;
}

// vim:ts=4:sw=4:expandtab

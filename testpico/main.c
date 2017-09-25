/*
 * This software is released into the public domain.
 * See UNLICENSE file in top level directory.
 */
#include <stdlib.h>
#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define noinline __attribute__((noinline))
#define unused   __attribute__((unused))
#define _packed  __attribute__((packed))

#define mem_barrier() \
    asm volatile("":::"memory")

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#include "asmtools.h"

#define VDP_DATA_PORT    0xC00000
#define VDP_CTRL_PORT    0xC00004

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END / 32  - FONT_LEN)

/* note: using ED menu's layout here.. */
#define WPLANE           (TILE_MEM_END + 0x0000)
#define HSCRL            (TILE_MEM_END + 0x0800)
#define SLIST            (TILE_MEM_END + 0x0C00)
#define APLANE           (TILE_MEM_END + 0x1000)
#define BPLANE           (TILE_MEM_END + 0x3000)

#define read8(a) \
    *((volatile u8 *) (a))
#define read16(a) \
    *((volatile u16 *) (a))
#define read32(a) \
    *((volatile u32 *) (a))
#define write8(a, d) \
    *((volatile u8 *) (a)) = (d)
#define write16(a, d) \
    *((volatile u16 *) (a)) = (d)
#define write32(a, d) \
    *((volatile u32 *) (a)) = (d)

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

int xtttt(void) { return sizeof(struct exc_frame); }

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
    while (read16(VDP_CTRL_PORT) & 2)
        ;

    VDP_setReg(VDP_AUTOINC, 2);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0));
    ram[3] = read32(VDP_DATA_PORT);
}

#define expect(ok_, v0_, v1_) \
if ((v0_) != (v1_)) { \
    printf("%s: %08x %08x\n", #v0_, v0_, v1_); \
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
    u32 v0;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0);

    do_setup_dma(src, 2);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0xfc) | CTL_WRITE_DMA);
    write32(VDP_DATA_PORT, 0x5ec8a248);

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);

    expect(ok, v0, 0x5ec8a248);
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

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x101) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    while (read16(VDP_CTRL_PORT) & 2)
        ;

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

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x1122);
    while (read16(VDP_CTRL_PORT) & 2)
        ;

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

    VDP_setReg(VDP_AUTOINC, 3);
    VDP_setReg(VDP_DMA_LEN0, 3);
    VDP_setReg(VDP_DMA_SRC2, 0x80);
    write32(VDP_CTRL_PORT, CTL_WRITE_VSRAM(1) | CTL_WRITE_DMA);
    write16(VDP_DATA_PORT, 0x0102);
    while (read16(VDP_CTRL_PORT) & 2)
        ;

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
    while (read16(VDP_CTRL_PORT) & 2)
        ;

    VDP_setReg(VDP_MODE2, VDP_MODE2_MD | VDP_MODE2_DMA | VDP_MODE2_DISP);
    write16(VDP_DATA_PORT, 0x3344);
    while (read16(VDP_CTRL_PORT) & 2)
        ;

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
    while (read16(VDP_CTRL_PORT) & 2)
        ;

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

static int t_z80mem_vdp_r(void)
{
    u8 *zram = (u8 *)0xa00000;
    int ok = 1;

    write32(VDP_CTRL_PORT, CTL_WRITE_VRAM(0x100));
    write32(VDP_DATA_PORT, 0x11223344);
    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));

    zram[0x1000] = 1; // cp
    zram[0x1001] = 2; // len
    write16_z80le(&zram[0x1002], 0x7f00); // src
    write16_z80le(&zram[0x1004], 0x1006); // dst
    zram[0x1006] = zram[0x1007] = zram[0x1008] = 0x5a;
    mem_barrier();
    write16(0xa11100, 0x000);
    burn10((98 + 40*2 + 27) * 15 / 7 * 2 / 10);

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;

    expect(ok, zram[0x1000], 0);
    expect(ok, zram[0x1006], 0x11);
    expect(ok, zram[0x1007], 0x44);
    expect(ok, zram[0x1008], 0x5a);
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

    zram[0x1000] = 1; // cp
    zram[0x1001] = 2; // len
    write16_z80le(&zram[0x1002], 0x1006); // src
    write16_z80le(&zram[0x1004], 0x7f00); // dst
    zram[0x1006] = 0x55;
    zram[0x1007] = 0x66;
    mem_barrier();
    write16(0xa11100, 0x000);
    burn10((98 + 40*2 + 27) * 15 / 7 * 2 / 10);

    write16(0xa11100, 0x100);
    while (read16(0xa11100) & 0x100)
        ;

    write32(VDP_CTRL_PORT, CTL_READ_VRAM(0x100));
    v0 = read32(VDP_DATA_PORT);

    expect(ok, zram[0x1000], 0);
    expect(ok, v0, 0x55556666);
    return ok;
}

static const struct {
    int (*test)(void);
    const char *name;
} g_tests[] = {
    { t_dma_zero_wrap,       "dma zero len + wrap" },
    { t_dma_zero_fill,       "dma zero len + fill" },
    { t_dma_ram_wrap,        "dma ram wrap" },
    { t_dma_multi,           "dma multi" },
    { t_dma_cram_wrap,       "dma cram wrap" },
    { t_dma_vsram_wrap,      "dma vsram wrap" },
    { t_dma_and_data,        "dma and data" },
    { t_dma_fill3_odd,       "dma fill3 odd" },
    { t_dma_fill3_even,      "dma fill3 even" },
    // { t_dma_fill3_vsram,     "dma fill3 vsram" }, // later
    { t_dma_fill_dis,        "dma fill disabled" },
    { t_dma_fill_src,        "dma fill src incr" },
    { t_z80mem_long_mirror,  "z80 ram long mirror" },
    { t_z80mem_vdp_r,        "z80 vdp read" },
    // { t_z80mem_vdp_w,        "z80 vdp write" }, // hang
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
}

int main()
{
    int passed = 0;
    int ret;
    int i;

    setup_z80();

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

    printf("\n");
    printf("MD version: %02x\n", read8(0xa10001));

    for (i = 0; i < ARRAY_SIZE(g_tests); i++) {
        // print test number if we haven't scrolled away
        if (printf_ypos < CSCREEN_H) {
            int old_ypos = printf_ypos;
            printf_ypos = 0;
            text_pal = 0;
            printf("%02d/%02d", i, ARRAY_SIZE(g_tests));
            printf_ypos = old_ypos;
            printf_xpos = 0;
        }
        text_pal = 2;
        ret = g_tests[i].test();
        if (ret != 1)
            printf("failed %d: %s\n", i, g_tests[i].name);
        else
            passed++;
    }

    text_pal = 0;
    printf("%d/%d passed.\n", passed, ARRAY_SIZE(g_tests));

    printf_ypos = 0;
    printf("     ");

    for (;;)
        ;

    return 0;
}

// vim:ts=4:sw=4:expandtab

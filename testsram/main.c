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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#include "asmtools.h"

#define GFX_DATA_PORT    0xC00000
#define GFX_CTRL_PORT    0xC00004

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

#define GFX_WRITE_VRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define GFX_WRITE_VSRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x10)
#define GFX_WRITE_CRAM_ADDR(adr) \
    (((0xC000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)

#define VDP_setReg(r, v) \
    write16(GFX_CTRL_PORT, 0x8000 | ((r) << 8) | (v))

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
};

/* cell counts */
#define LEFT_BORDER 1   /* lame TV */
#define PLANE_W 64
#define PLANE_H 32
#define CSCREEN_H 28

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
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(addr));

    while (len-- > 0) {
        write16(GFX_DATA_PORT,
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
        write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(addr));
        for (i = 0; i < 40 / 2; i++)
            write32(GFX_DATA_PORT, 0);

        /* scroll plane */
        write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
        write16(GFX_DATA_PORT, (printf_ypos - CSCREEN_H + 1) * 8);
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

    while (read16(GFX_CTRL_PORT) & 2)
        ;
    write16(GFX_CTRL_PORT, 0x8000 | (VDP_MODE1 << 8) | 0x04);
    write16(GFX_CTRL_PORT, 0x8000 | (VDP_MODE2 << 8) | 0x44);
    /* adjust scroll */
    write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
    write16(GFX_DATA_PORT,
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

extern u32 font_base[];
extern u8 test_data[];
extern u8 test_data_end[];

static void simple_test(int *odd, int *even, int *rom)
{
    u32 old, new, v0, v1;

    *odd = *even = *rom = 0;
    old = read16(0x200000);
    new = old ^ 0xa55a;
    write16(0x200000, new);
    v0 = read16(0x200000);
    write8(0x200000, ~new >> 8);
    write8(0x200001, ~new);
    //write16(0x200000, ~new);
    v1 = read16(0x200000);
    if (((v0 ^ new) & 0xff00) == 0 && ((v1 ^ ~new) & 0xff00) == 0) {
        printf(" even");
        *even = 1;
    }
    if (((v0 ^ new) & 0x00ff) == 0 && ((v1 ^ ~new) & 0x00ff) == 0) {
        printf(" odd");
        *odd = 1;
    }
    if (v0 == old && v1 == old) {
        printf(" ROM");
        *rom = 1;
    }
    else if (!(*odd | *even)) {
        text_pal = 2;
        printf(" bad value");
        text_pal = 0;
    }
}

static int detect_size(u8 *a)
{
    int i, v;

    write8(a, 0);
    for (i = 2, v = 1; i < 0x200000; i <<= 1, v++) {
        write8(a + i, v);
        if (read8(a) || read8(a + i) != v)
            break;
    }
    return i > 2 ? i / 2 : 0;
}

static unused void fill(u8 *d, unsigned int size, u8 val)
{
    unsigned int i;

    for (i = 0; i < size * 2; i += 2)
        d[i] = val;
}

static int check(u8 *d, unsigned int size, u8 val)
{
    unsigned int i;

    for (i = 0; i < size * 2; i += 2)
        if (d[i] != val)
            break;

    if (i == size * 2)
        return 1;

    text_pal = 2;
    printf("\nfailed at byte %x, val %02x vs %02x\n",
        i / 2, d[i], val);
    text_pal = 0;
    return 0;
}

static void do_test(u8 *d, unsigned int size)
{
    int spos = printf_xpos;
    int i;

    for (i = 0; i < 0x100; i++) {
        printf_xpos = spos;
        printf("%02x", i);
        //fill(d, size, i);
        fillpx16(d, size / 16, i);
        if (!checkpx4(d, size / 4, i)) {
            check(d, size, i); // for log
            break;
        }
    }
}

int main()
{
    volatile u32 *vptr32;
    int odd, even, rom;
    u32 old;
    int i;

    /* z80 */
    write16(0xa11100, 0x100);
    write16(0xa11200, 0);

    /* setup VDP */
    while (read16(GFX_CTRL_PORT) & 2)
        ;

    VDP_setReg(VDP_MODE1, 0x04); 
    VDP_setReg(VDP_MODE2, 0x44); 
    VDP_setReg(VDP_MODE3, 0x00); 
    VDP_setReg(VDP_MODE4, 0x81); 
    VDP_setReg(VDP_NT_SCROLLA, APLANE >> 10); 
    VDP_setReg(VDP_NT_SCROLLB, BPLANE >> 13); 
    VDP_setReg(VDP_SAT_BASE, SLIST >> 9); 
    VDP_setReg(VDP_HSCROLL, HSCRL >> 10); 
    VDP_setReg(VDP_AUTOINC, 2); 
    VDP_setReg(VDP_SCROLLSZ, 0x01); 
    VDP_setReg(VDP_BACKDROP, 0); 

    /* clear name tables */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(APLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(BPLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    /* SAT, h. scroll */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(SLIST));
    write32(GFX_DATA_PORT, 0);

    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(HSCRL));
    write32(GFX_DATA_PORT, 0);

    /* scroll plane vscroll */
    write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
    write32(GFX_DATA_PORT, 0);
    printf_xpos = printf_ypos = 0;

    /* load font */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(TILE_FONT_BASE));
    for (i = 0; i < FONT_LEN * 32 / 4; i++)
        write32(GFX_DATA_PORT, font_base[i]);

    /* set colors */
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(0));
    write32(GFX_DATA_PORT, 0);
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(15 * 2)); // font normal
    write16(GFX_DATA_PORT, 0xeee);
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(31 * 2)); // green
    write16(GFX_DATA_PORT, 0x0e0);
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(47 * 2)); // red
    write16(GFX_DATA_PORT, 0x00e);
    text_pal = 0;

    printf("\n");
    printf("MD version: %02x\n", read8(0xa10001));
    printf("ROM writable? ");

    vptr32 = (void *)0x120;
    old = *vptr32;
    *vptr32 ^= ~0;
    printf("%s\n", *vptr32 == old ? "no" : "yes");

    printf("200000 initial state:");
    simple_test(&odd, &even, &rom);

    printf("\nenable with i0: ");
    write_rreg_i0(1);
    simple_test(&odd, &even, &rom);

    printf("\ndisable with i0:");
    write_rreg_i0(0);
    simple_test(&odd, &even, &rom);

    printf("\nenable with i1: ");
    write_rreg_i1(1);
    simple_test(&odd, &even, &rom);

    printf("\ndisable with i1:");
    write_rreg_i1(0);
    simple_test(&odd, &even, &rom);

    printf("\nenable with 16: ");
    write16(0xa130f0, 1);
    simple_test(&odd, &even, &rom);
    printf("\n");

    if (even) {
        even = detect_size((void *)0x200000);
        printf("detected even size: %d\n", even);
    }
    if (odd) {
        odd = detect_size((void *)0x200001);
        printf("detected odd size:  %d\n", odd);
    }
    if (even) {
        printf("testing even: ", even);
        do_test((void *)0x200000, even);
        printf("\n");
    }
    if (odd) {
        printf("testing odd:  ", odd);
        do_test((void *)0x200001, odd);
        printf("\n");
    }

    if (!odd && !even) {
        text_pal = 2;
        printf("no RAM\n");
        text_pal = 0;
    }

    printf("done.\n");

    for (;;)
        ;

    return 0;
}

// vim:ts=4:sw=4:expandtab

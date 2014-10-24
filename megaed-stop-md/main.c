/*
 * This software is released into the public domain.
 * See UNLICENSE file in top level directory.
 */
#include <stdlib.h>
#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define GFX_DATA_PORT    0xC00000
#define GFX_CTRL_PORT    0xC00004

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END - FONT_LEN * 32)

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

static void VDP_drawTextML(const char *str, u16 plane_base,
    u16 x, u16 y)
{
    const u8 *src = (const u8 *)str;
    u16 basetile = 0;
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

static int printf(const char *fmt, ...)
{
    static const char hexchars[] = "0123456789abcdef";
    static int printf_xpos;
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

extern u32 font_base[];
extern u8 test_data[];
extern u8 test_data_end[];

int main()
{
    volatile u32 *vptr;
    u32 old;
    u8 bad = 0;
    u8 *p, val;
    int i, t, len;

    /* z80 */
    write16(0xa11100, 0x100);
    write16(0xa11200, 0);

    /* setup VDP */
    while (read16(GFX_CTRL_PORT) & 2)
        ;

    VDP_setReg(VDP_MODE1, 0x04); 
    VDP_setReg(VDP_MODE2, 0x64); 
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
    printf_ypos = 0;

    /* load font */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(TILE_FONT_BASE));
    for (i = 0; i < FONT_LEN * 32 / 4; i++)
        write32(GFX_DATA_PORT, font_base[i]);

    /* set colors */
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(0));
    write32(GFX_DATA_PORT, 0);
    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(15 * 2)); // font
    write16(GFX_DATA_PORT, 0xeee);

    printf("\n");
    printf("MD version: %02x\n", read8(0xa10001));
    printf("ROM witable? ");

    vptr = (void *)0x120;
    old = *vptr;
    *vptr ^= ~0;
    printf(*vptr == old ? "no" : "yes");
    printf("\n\n");

    p = test_data;
    len = test_data_end - test_data;

    for (t = 1; ; t++) {
        printf("executing stop.. ");
        for (i = 0; i < 5 * 60; i++)
            asm volatile("stop #0x2000" ::: "cc");
        printf("done\n");

        printf("checking memory..\n");

        val = 0;
        for (i = 0; i < len; i++) {
            if (p[i] != val) {
                printf("bad: %06x: got %02x, expected %02x\n",
                       &p[i], p[i], val);
                bad = 1;
            }
            val++;
        }

        printf("done. Try %d: test ", t);
        if (bad) {
            printf("FAILED\n");
            break;
        }
        printf("PASSED\n");
    }

    write32(GFX_CTRL_PORT, GFX_WRITE_CRAM_ADDR(0));
    write16(GFX_DATA_PORT, 0x008);

    /* there are not enough STOP opcodes in this world :D */
    while (1)
        asm volatile("stop #0x2000" ::: "cc");

    return 0;
}

// vim:ts=4:sw=4:expandtab

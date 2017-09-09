/*
 * This software is released into the public domain.
 * See UNLICENSE file in top level directory.
 */
#include <stdlib.h>
#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#include "edos.h"

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
#define write16(a, d) \
    *((volatile u16 *) (a)) = (d)
#define write32(a, d) \
    *((volatile u32 *) (a)) = (d)

#define GFX_WRITE_VRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x00)
#define GFX_WRITE_VSRAM_ADDR(adr) \
    (((0x4000 | ((adr) & 0x3FFF)) << 16) | ((adr) >> 14) | 0x10)

enum {
    VDP_MODE1 = 0x00,
    VDP_MODE2 = 0x01,
    VDP_BACKDROP = 0x07,
    VDP_MODE3 = 0x0b,
    VDP_MODE4 = 0x0c,
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

int main()
{
    OsRoutine *ed;
    u8 seed, bad = 0;
    u8 *p, val;
    int i, t, len;

    ed = (OsRoutine *) *(u32 *)0x1A0;
    ed->memInitDmaCode(); 

    ed->VDP_setReg(VDP_MODE1, 0x04); 
    ed->VDP_setReg(VDP_MODE2, 0x64); 
    ed->VDP_setReg(VDP_AUTOINC, 2); 
    ed->VDP_setReg(VDP_SCROLLSZ, 0x01); 

    /* clear name tables */
    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(APLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    write32(GFX_CTRL_PORT, GFX_WRITE_VRAM_ADDR(BPLANE));
    for (i = 0; i < PLANE_W * PLANE_H / 2; i++)
        write32(GFX_DATA_PORT, 0);

    /* scroll planes */
    write32(GFX_CTRL_PORT, GFX_WRITE_VSRAM_ADDR(0));
    write32(GFX_DATA_PORT, 0);

    /* note: relying on ED menu's font setup here.. */

    printf("\n");
    printf("MD version: %02x\n", read8(0xa10001));
    printf("ED os/fw: %d/%d\n\n", ed->osGetOsVersion(),
           ed->osGetFirmVersion());

    seed = read8(0xC00009);
    p = (void *)0x200000;
    len = 0x200000;
    printf("filling SDRAM, seed=%02x.. ", seed);

    val = seed;
    for (i = 0; i < len; i++)
        p[i] = val++;

    printf("done.\n");

    for (t = 1; ; t++) {
        printf("executing stop.. ");
        for (i = 0; i < 5 * 60; i++)
            asm volatile("stop #0x2000");
        printf("done\n");

        printf("checking memory..\n");

        val = seed;
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

    /* there are not enough STOP opcodes in this world :D */
    while (1)
        asm volatile("stop #0x2000");

    return 0;
}

// vim:ts=4:sw=4:expandtab

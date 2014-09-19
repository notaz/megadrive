#include <stdarg.h>

#define u8      unsigned char
#define u16     unsigned short
#define u32     unsigned int

#define noinline __attribute__((noinline))

#include "edos.h"

#define GFX_DATA_PORT    0xC00000
#define GFX_CTRL_PORT    0xC00004

#define TILE_MEM_END     0xB000

#define FONT_LEN         128
#define TILE_FONT_BASE   (TILE_MEM_END / 32  - FONT_LEN)

/* note: using ED menu's layout here.. */
#define WPLAN            (TILE_MEM_END + 0x0000)
#define HSCRL            (TILE_MEM_END + 0x0800)
#define SLIST            (TILE_MEM_END + 0x0C00)
#define APLANE           (TILE_MEM_END + 0x1000)
#define BPLANE           (TILE_MEM_END + 0x3000)

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

static noinline void VDP_drawTextML(const char *str, u16 plane_base,
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
            basetile | ((*src++) - 32 + TILE_FONT_BASE));
    }
}

static int printf_ypos;

static void printf_line(int x, const char *buf)
{
    u32 addr;
    int i;

    if (printf_ypos >= CSCREEN_H) {
    }

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
        write16(GFX_DATA_PORT, (printf_ypos - 27) * 8);
    }
}

static noinline int printf(const char *fmt, ...)
{
    static int printf_xpos;
    char buf[40+11];
    va_list ap;
    int ival;
    int d = 0;
    int i;

    va_start(ap, fmt);
    for (d = 0; *fmt; ) {
        buf[d] = *fmt++;
        if (buf[d] != '%') {
            if (buf[d] == '\n') {
                buf[d] = 0;
                if (d != 0)
                    printf_line(printf_xpos, buf);
                d = 0;
                printf_xpos = 0;
                continue;
            }
            d++;
            continue;
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
        case 's':
            // s = va_arg(ap, char *);
        default:
            // don't handle, for now
            d++;
            buf[d++] = *fmt++;
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

void exception(void)
{
    VDP_drawTextML("============", APLANE, 0, 0);
    VDP_drawTextML(" exception! ", APLANE, 0, 1);
    VDP_drawTextML("============", APLANE, 0, 2);
    while (1)
        ;
}

void vbl(void)
{
}

int main()
{
    OsRoutine *ed;
    int i, j = 0;

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

    /* note: relying on ED menu's font setup here.. */

    // VDP_drawTextML("hello", APLANE, 0, 0);
    printf("hello1");
    printf(" hello2\n");
    printf("hello3\n");

    for (;;) {
        for (i = 0; i < 30; i++)
            asm volatile("stop #0x2000");

        printf("hello %d\n", j++);
    }

    return 0;
}

// vim:ts=4:sw=4:expandtab

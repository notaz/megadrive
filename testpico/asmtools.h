void burn10(unsigned short val);
void write16_x16(unsigned int addr, unsigned short count, short data);

/* SACB RLDU */
#define BTNM_S (1 << 7)
#define BTNM_A (1 << 6)
#define BTNM_C (1 << 5)
#define BTNM_B (1 << 4)
#define BTNM_R (1 << 3)
#define BTNM_L (1 << 2)
#define BTNM_D (1 << 1)
#define BTNM_U (1 << 0)

int   get_input(void);
void  write_and_read1(unsigned int a, unsigned short d, void *dst);
void  move_sr(unsigned short sr);
short move_sr_and_read(unsigned short sr, unsigned int a);
void  memcpy_(void *dst, const void *src, unsigned short size);
void  memset_(void *dst, int d, unsigned short size);

void  do_vcnt_vb(void);

extern const char test_hint[];
extern const char test_hint_end[];
extern const char test_vint[];
extern const char test_vint_end[];

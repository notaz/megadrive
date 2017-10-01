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

int  get_input(void);
void do_vcnt_vb(void);

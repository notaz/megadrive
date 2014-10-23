void read_joy_responses(u8 resp[8*5]);
void test_joy_read_log(u8 *dest, int size, int do_sync);
void test_joy_read_log_vsync(u8 *dest, int size);
void test_byte_write(u8 *dest, int size, int seed);
void run_game(u16 mapper, int tas_sync);

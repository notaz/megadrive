
enum tas_pkt_type {
	PKT_FIXED_STATE       = 0xef01,
	PKT_STREAM_ENABLE     = 0xef02,
	PKT_STREAM_REQ        = 0xef03,
	PKT_STREAM_DATA_TO_P1 = 0xef04,
	PKT_STREAM_DATA_TO_P2 = 0xef05,
	PKT_STREAM_DATA_FROM  = 0xef06,
	PKT_STREAM_END        = 0xef07,
	PKT_STREAM_ABORT      = 0xef08,
};

struct tas_pkt {
	uint16_t type;
	uint16_t size; // for DATA_FROM/TO
	union {
		uint8_t data[60];
		struct {
			uint32_t frame; // just fyi
			uint8_t is_p2;
		} req;
		struct {
			uint8_t stream_to;
			uint8_t stream_from;
			// frame increment on read
			uint8_t inc_mode;
			uint8_t no_start_seq;
		} enable;
	};
} __attribute__((packed));

enum inc_mode {
	INC_MODE_VSYNC = 0,
	// shared stream index incremented by pl1 or pl2
	INC_MODE_SHARED_PL1 = 1,
	INC_MODE_SHARED_PL2 = 2,
	INC_MODE_SEPARATE = 3,
};


enum tas_pkt_type {
	PKT_FIXED_STATE      = 0xef01,
	PKT_STREAM_ENABLE    = 0xef02,
	PKT_STREAM_REQ       = 0xef03,
	PKT_STREAM_DATA_TO   = 0xef04,
	PKT_STREAM_DATA_FROM = 0xef05,
	PKT_STREAM_END       = 0xef06,
	PKT_STREAM_ABORT     = 0xef07,
};

struct tas_pkt {
	uint16_t type;
	uint16_t size; // for DATA_FROM/TO
	union {
		uint8_t data[60];
		struct {
			uint32_t frame; // just fyi
		} req;
		struct {
			uint8_t stream_to;
			uint8_t stream_from;
			// frame increment on read
			uint8_t use_readinc;
			uint8_t no_start_seq;
		} enable;
	};
} __attribute__((packed));

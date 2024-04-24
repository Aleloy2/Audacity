#include "ltc.h"
#ifndef SAMPLE_CENTER // also defined in decoder.h
#define SAMPLE_CENTER 128 // unsigned 8 bit.
#endif

struct LTCEncoder {
	double fps;
	double sample_rate;
	double filter_const;
	int flags;
	enum LTC_TV_STANDARD standard;
	ltcsnd_sample_t enc_lo, enc_hi;

	size_t offset;
	size_t bufsize;
	ltcsnd_sample_t *buf;

	char state;

	double samples_per_clock;
	double samples_per_clock_2;
	double sample_remainder;

	LTCFrame f;
};

int encode_byte(LTCEncoder *e, int byte, double speed);
int encode_transition(LTCEncoder *e);

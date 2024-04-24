#include "ltc.h"
#ifndef SAMPLE_CENTER // also defined in encoder.h
#define SAMPLE_CENTER 128 // unsigned 8 bit.
#endif

struct LTCDecoder {
	LTCFrameExt* queue;
	int queue_len;
	int queue_read_off;
	int queue_write_off;

	unsigned char biphase_state;
	unsigned char biphase_prev;
	unsigned char snd_to_biphase_state;
	int snd_to_biphase_cnt;		///< counts the samples in the current period
	int snd_to_biphase_lmt;	///< specifies when a state-change is considered biphase-clock or 2*biphase-clock
	double snd_to_biphase_period;	///< track length of a period - used to set snd_to_biphase_lmt

	ltcsnd_sample_t snd_to_biphase_min;
	ltcsnd_sample_t snd_to_biphase_max;

	unsigned short decoder_sync_word;
	LTCFrame ltc_frame;
	int bit_cnt;

	ltc_off_t frame_start_off;
	ltc_off_t frame_start_prev;

	float biphase_tics[LTC_FRAME_BIT_COUNT];
	int biphase_tic;
};


void decode_ltc(LTCDecoder *d, ltcsnd_sample_t *sound, size_t size, ltc_off_t posinfo);

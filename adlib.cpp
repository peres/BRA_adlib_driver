#include <math.h>
#include <memory.h>

typedef unsigned char 	uint8;
typedef signed char		int8;
typedef unsigned short	uint16;
typedef signed short	int16;
typedef unsigned int 	uint32;
typedef signed int		int32;

#define NUM_MIDI_CHANNELS		15

struct MidiChannel {
	uint8 program;
	uint8 volume;
	uint8 pedal;
} midi_channels[NUM_MIDI_CHANNELS];

#define NUM_MELODIC_VOICES		6		// adlib FM voices 0-5	(2 operators each)
#define NUM_PERCUSSIONS			5		// adlib FM voice 6 	(2 operators), and voices 7-8 (1 operator each)

struct MelodicVoice {
	int8 key;			// the note being played
	int8 program;		// the midi instrument? (see voice)
	int8 channel;		// the midi channel
	int32 timestamp;
	uint16 fnumber;		// frequency id (see lookup table)
	int8 octave;
	bool in_use;
} melodic[NUM_MELODIC_VOICES];

// notes being currently played for each percussion (0xFF if none)
uint8 notes_per_percussion[NUM_PERCUSSIONS];

struct OplOperator {
	uint8 characteristic;	// amplitude modulation, vibrato, envelope, keyboard scaling, modulator frequency
	uint8 levels;
	uint8 attack_decay;
	uint8 sustain_release;
	uint8 waveform;
};

// (almost) static info about notes played by percussions
// fields with _2 are used only by the bass drum (2 operators)
struct PercussionNote {
	OplOperator	op[2];
	uint8 feedback_algo;	// only used by the bass drum
	uint8 percussion;
	uint8 valid;
	uint16 fnumber;
	uint8 octave;
};

struct MelodicProgram {
	OplOperator	op[2];
	uint8 feedback_algo;
};

uint8 driver_percussion_mask;

enum DriverStatus {
	kStatusStopped,
	kStatusPlaying,
	kStatusPaused
};

DriverStatus driver_status;
bool driver_installed;

uint32 midi_buffer_pos;

uint16 midi_division;	// in ppqn
uint16 midi_event_delta;
uint8  midi_event_type, last_midi_event_type;
uint8  midi_tempo;
uint8  midi_event_channel;
uint8  midi_onoff_note;
uint8  midi_onoff_velocity;
int16 midi_pitch_bend;
uint8 midi_fade_volume_change_rate;

int32 driver_timestamp;
uint8 driver_assigned_voice;

// set by the client
uint32 midi_buffer_size;
bool midi_loop;
uint8 midi_volume;	// coarse volume
bool midi_fade_out_flag;
bool midi_fade_in_flag;


// fade in
bool driver_fading_in;
uint32 fadein_volume_cur;
uint32 fadein_volume_inc;

// fade out
bool driver_fading_out;
uint32 fadeout_volume_cur;
uint32 fadeout_volume_dec;

uint32 driver_lin_volume[128];
uint32 ADLIB_log_volume[129];

// internal fine volume
uint16 full_volume;

#define COARSE_VOL(x)	((x)>>8)
#define FINE_VOL(x)		((x)<<8)

/**********************************
	prototypes
*/

// midi driver
uint8 read_midi_byte();
uint16 read_midi_word();
uint32 read_midi_VLQ();
void midi_fadeout_and_stop();
void midi_stop();
void midi_pause();
void midi_resume();
void midi_set_tempo();
void process_midi_meta_event();
void process_midi_channel_event();
void process_meta_tempo_event();

// timers

void set_hw_timer(uint16 clock);
void reset_hw_timer();

// OPL
void ADLIB_init();
void ADLIB_play_note(uint8 voice, uint8 octave, uint16 fnumber);
void ADLIB_play_melodic_note(uint8 voice);
void ADLIB_mute_melodic_voice(uint8 voice);
void ADLIB_program_melodic_voice(uint8 voice, uint8 program);
void ADLIB_turn_on_melodic();
void ADLIB_play_percussion(PercussionNote *note, uint8 velocity);
void ADLIB_setup_percussion(PercussionNote *note);
void ADLIB_onoff_percussion(bool onoff);
void ADLIB_turn_on_voice();
void ADLIB_turn_off_voice();
void ADLIB_init_voices();
void ADLIB_mute_voices();
void ADLIB_pitch_bend(int amount, uint8 midi_channel);
void ADLIB_modulation(int value);
void ADLIB_out(uint8 command, uint8 value);

/**********************************
	static data
*/


MelodicProgram melodic_programs[128] = {
	{   0x1, 0x51, 0xf2, 0xb2,  0x0, 0x11,  0x0, 0xf2, 0xa2,  0x0,  0x0 },
	{  0xc2, 0x4b, 0xf1, 0x53,  0x0, 0xd2,  0x0, 0xf2, 0x74,  0x0,  0x4 },
	{  0x81, 0x9d, 0xf2, 0x74,  0x0, 0x13,  0x0, 0xf2, 0xf1,  0x0,  0x6 },
	{   0x3, 0x4f, 0xf1, 0x53,  0x0, 0x17,  0x3, 0xf2, 0x74,  0x0,  0x6 },
	{  0xd1, 0x81, 0x81, 0x73,  0x2, 0xd4,  0x0, 0xe1, 0x34,  0x0,  0x3 },
	{   0x1,  0x0, 0x94, 0xa6,  0x0,  0x2,  0x0, 0x83, 0x26,  0x0,  0x1 },
	{  0xf3, 0x84, 0x81,  0x2,  0x1, 0x55, 0x80, 0xdd,  0x3,  0x0,  0x4 },
	{   0x5, 0x8a, 0xf2, 0x26,  0x0,  0x1, 0x80, 0xf3, 0x48,  0x0,  0x0 },
	{  0x32,  0x0, 0xb1, 0x14,  0x0, 0x12,  0x0, 0xfd, 0x36,  0x0,  0x3 },
	{   0x1,  0x0, 0x82,  0xa,  0x2,  0x2,  0x0, 0x85, 0x15,  0x0,  0x3 },
	{  0xd1,  0x1, 0x97, 0xaa,  0x0,  0x4,  0xd, 0xf3, 0xa5,  0x1,  0x9 },
	{  0x17,  0x0, 0xf2, 0x62,  0x0, 0x12,  0x0, 0xf2, 0x72,  0x0,  0x8 },
	{   0x6,  0x0, 0xff, 0xf4,  0x0, 0xc4,  0x0, 0xf8, 0xb5,  0x0,  0xe },
	{  0xc0, 0x81, 0xf2, 0x13,  0x2, 0xc0, 0xc1, 0xf3, 0x14,  0x2,  0xb },
	{  0x44, 0x53, 0xf5, 0x31,  0x0, 0x60, 0x80, 0xfd, 0x22,  0x0,  0x6 },
	{  0xe0, 0x80, 0xf4, 0xf2,  0x0, 0x61,  0x0, 0xf2,  0x6,  0x0,  0x8 },
	{  0xc1,  0x6, 0x83, 0x23,  0x0, 0xc1,  0x4, 0xf0, 0x26,  0x0,  0x1 },
	{  0x26,  0x0, 0xf4, 0xb6,  0x0, 0x21,  0x0, 0x81, 0x4b,  0x0,  0x1 },
	{  0x24, 0x80, 0xff,  0xf,  0x0, 0x21, 0x80, 0xff,  0xf,  0x0,  0x1 },
	{  0x24, 0x4f, 0xf2,  0xb,  0x0, 0x31,  0x0, 0x52,  0xb,  0x0,  0xb },
	{  0x31,  0x8, 0x81,  0xb,  0x0, 0xa1, 0x80, 0x92, 0x3b,  0x0,  0x0 },
	{  0x70, 0xc5, 0x52, 0x11,  0x1, 0x71, 0x80, 0x31, 0xfe,  0x1,  0x0 },
	{  0x51, 0x88, 0x10, 0xf0,  0x0, 0x42, 0x83, 0x40, 0xfc,  0x0,  0x8 },
	{  0xf0, 0xd9, 0x81,  0x3,  0x0, 0xb1, 0x80, 0xf1,  0x5,  0x0,  0xa },
	{  0x21, 0x4f, 0xf1, 0x31,  0x0,  0x2, 0x80, 0xc3, 0x45,  0x0,  0x0 },
	{   0x7, 0x8f, 0x9c, 0x33,  0x1,  0x1, 0x80, 0x8a, 0x13,  0x0,  0x0 },
	{  0x21, 0x40, 0xf1, 0x31,  0x0,  0x6, 0x80, 0xf4, 0x44,  0x0,  0x0 },
	{  0x21, 0x40, 0xf1, 0x31,  0x3, 0x81,  0x0, 0xf4, 0x44,  0x2,  0x2 },
	{  0x11, 0x8d, 0xfd, 0x11,  0x0, 0x11, 0x80, 0xfd, 0x11,  0x0,  0x8 },
	{  0xf0,  0x1, 0x97, 0x17,  0x0, 0x21,  0xd, 0xf1, 0x18,  0x0,  0x8 },
	{  0xf1,  0x1, 0x97, 0x17,  0x0, 0x21,  0xd, 0xf1, 0x18,  0x0,  0x8 },
	{  0xcd, 0x9e, 0x55, 0xd1,  0x0, 0xd1,  0x0, 0xf2, 0x71,  0x0,  0xe },
	{   0x1,  0x0, 0xf2, 0x88,  0x0,  0x1,  0x0, 0xf5, 0x88,  0x0,  0x1 },
	{  0x30,  0xd, 0xf2, 0xef,  0x0, 0x21,  0x0, 0xf5, 0x78,  0x0,  0x6 },
	{   0x0, 0x10, 0xf4, 0xd9,  0x0,  0x0,  0x0, 0xf5, 0xd7,  0x0,  0x4 },
	{   0x1, 0x4c, 0xf2, 0x50,  0x0,  0x1, 0x40, 0xd2, 0x59,  0x0,  0x8 },
	{  0x20, 0x11, 0xe2, 0x8a,  0x0, 0x20,  0x0, 0xe4, 0xa8,  0x0,  0xa },
	{  0x21, 0x40, 0x7b,  0x4,  0x1, 0x21,  0x0, 0x75, 0x72,  0x0,  0x2 },
	{  0x31,  0xd, 0xf2, 0xef,  0x0, 0x21,  0x0, 0xf5, 0x78,  0x0,  0xa },
	{   0x1,  0xc, 0xf5, 0x2f,  0x1,  0x0, 0x80, 0xf5, 0x5c,  0x0,  0x0 },
	{  0xb0, 0x1c, 0x81,  0x3,  0x2, 0x20,  0x0, 0x54, 0x67,  0x2,  0xe },
	{   0x1,  0x0, 0xf1, 0x65,  0x0,  0x1, 0x80, 0xa3, 0xa8,  0x2,  0x1 },
	{  0xe1, 0x4f, 0xc1, 0xd3,  0x2, 0x21,  0x0, 0x32, 0x74,  0x1,  0x0 },
	{   0x2,  0x0, 0xf6, 0x16,  0x0, 0x12,  0x0, 0xf2, 0xf8,  0x0,  0x1 },
	{  0xe0, 0x63, 0xf8, 0xf3,  0x0, 0x70, 0x80, 0xf7, 0xf3,  0x0,  0x4 },
	{   0x1,  0x6, 0xf3, 0xff,  0x0,  0x8,  0x0, 0xf7, 0xff,  0x0,  0x4 },
	{  0x21, 0x16, 0xb0, 0x81,  0x1, 0x22,  0x0, 0xb3, 0x13,  0x1,  0xc },
	{   0x1, 0x4f, 0xf0, 0xff,  0x0, 0x30,  0x0, 0x90,  0xf,  0x0,  0x6 },
	{   0x0, 0x10, 0xf1, 0xf2,  0x2,  0x1,  0x0, 0xf1, 0xf2,  0x3,  0x0 },
	{   0x1, 0x4f, 0xf1, 0x50,  0x0, 0x21, 0x80, 0xa3,  0x5,  0x3,  0x6 },
	{  0xb1,  0x3, 0x55,  0x3,  0x0, 0xb1,  0x3,  0x8,  0xa,  0x0,  0x9 },
	{  0x22,  0x0, 0xa9, 0x34,  0x1,  0x1,  0x0, 0xa2, 0x42,  0x2,  0x2 },
	{  0xa0, 0xdc, 0x81, 0x31,  0x3, 0xb1, 0x80, 0xf1,  0x1,  0x3,  0x0 },
	{   0x1, 0x4f, 0xf1, 0x50,  0x0, 0x21, 0x80, 0xa3,  0x5,  0x3,  0x6 },
	{  0xf1, 0x80, 0xa0, 0x72,  0x0, 0x74,  0x0, 0x90, 0x22,  0x0,  0x9 },
	{  0xe1, 0x13, 0x71, 0xae,  0x0, 0xe1,  0x0, 0xf0, 0xfc,  0x1,  0xa },
	{  0x31, 0x1c, 0x41,  0xb,  0x0, 0xa1, 0x80, 0x92, 0x3b,  0x0,  0xe },
	{  0x71, 0x1c, 0x41, 0x1f,  0x0, 0xa1, 0x80, 0x92, 0x3b,  0x0,  0xe },
	{  0x21, 0x1c, 0x53, 0x1d,  0x0, 0xa1, 0x80, 0x52, 0x3b,  0x0,  0xc },
	{  0x21, 0x1d, 0xa4, 0xae,  0x1, 0x21,  0x0, 0xb1, 0x9e,  0x0,  0xc },
	{  0xe1, 0x16, 0x71, 0xae,  0x0, 0xe1,  0x0, 0x81, 0x9e,  0x0,  0xa },
	{  0xe1, 0x15, 0x71, 0xae,  0x0, 0xe2,  0x0, 0x81, 0x9e,  0x0,  0xe },
	{  0x21, 0x16, 0x71, 0xae,  0x0, 0x21,  0x0, 0x81, 0x9e,  0x0,  0xe },
	{  0x71, 0x1c, 0x41, 0x1f,  0x0, 0xa1, 0x80, 0x92, 0x3b,  0x0,  0xe },
	{  0x21, 0x4f, 0x81, 0x53,  0x0, 0x32,  0x0, 0x22, 0x2c,  0x0,  0xa },
	{  0x22, 0x4f, 0x81, 0x53,  0x0, 0x32,  0x0, 0x22, 0x2c,  0x0,  0xa },
	{  0x23, 0x4f, 0x81, 0x53,  0x0, 0x34,  0x0, 0x22, 0x2c,  0x0,  0xa },
	{  0xe1, 0x16, 0x71, 0xae,  0x0, 0xe1,  0x0, 0x81, 0x9e,  0x0,  0xa },
	{  0x71, 0xc5, 0x6e, 0x17,  0x0, 0x22,  0x5, 0x8b,  0xe,  0x0,  0x2 },
	{  0xe6, 0x27, 0x70,  0xf,  0x1, 0xe3,  0x0, 0x60, 0x9f,  0x0,  0xa },
	{  0x30, 0xc8, 0xd5, 0x19,  0x0, 0xb1, 0x80, 0x61, 0x1b,  0x0,  0xc },
	{  0x32, 0x9a, 0x51, 0x1b,  0x0, 0xa1, 0x82, 0xa2, 0x3b,  0x0,  0xc },
	{  0xad,  0x3, 0x74, 0x29,  0x0, 0xa2, 0x82, 0x73, 0x29,  0x0,  0x7 },
	{  0x21, 0x83, 0x74, 0x17,  0x0, 0x62, 0x8d, 0x65, 0x17,  0x0,  0x7 },
	{  0x94,  0xb, 0x85, 0xff,  0x1, 0x13,  0x0, 0x74, 0xff,  0x0,  0xc },
	{  0x74, 0x87, 0xa4,  0x2,  0x0, 0xd6, 0x80, 0x45, 0x42,  0x0,  0x2 },
	{  0xb3, 0x85, 0x76, 0x21,  0x1, 0x20,  0x0, 0x3d, 0xc1,  0x0,  0x6 },
	{  0x17, 0x4f, 0xf2, 0x61,  0x0, 0x12,  0x8, 0xf1, 0xb4,  0x0,  0x8 },
	{  0x4f, 0x86, 0x65,  0x1,  0x0, 0x1f,  0x0, 0x32, 0x74,  0x0,  0x4 },
	{  0xe1, 0x23, 0x71, 0xae,  0x0, 0xe4,  0x0, 0x82, 0x9e,  0x0,  0xa },
	{  0x11, 0x86, 0xf2, 0xbd,  0x0,  0x4, 0x80, 0xa0, 0x9b,  0x1,  0x8 },
	{  0x20, 0x90, 0xf5, 0x9e,  0x2, 0x11,  0x0, 0xf4, 0x5b,  0x3,  0xc },
	{  0xf0, 0x80, 0x34, 0xe4,  0x0, 0x7e,  0x0, 0xa2,  0x6,  0x0,  0x8 },
	{  0x90,  0xf, 0xff,  0x1,  0x3,  0x0,  0x0, 0x1f,  0x1,  0x0,  0xe },
	{   0x1, 0x4f, 0xf0, 0xff,  0x0, 0x33,  0x0, 0x90,  0xf,  0x0,  0x6 },
	{  0x1e,  0x0, 0x1f,  0xf,  0x0, 0x10,  0x0, 0x1f, 0x7f,  0x0,  0x0 },
	{  0xbe,  0x0, 0xf1,  0x1,  0x3, 0x31,  0x0, 0xf1,  0x1,  0x0,  0x4 },
	{  0xbe,  0x0, 0xf1,  0x1,  0x3, 0x31,  0x0, 0xf1,  0x1,  0x0,  0x4 },
	{  0x93,  0x6, 0xc1,  0x4,  0x1, 0x82,  0x0, 0x51,  0x9,  0x0,  0x6 },
	{  0xa0,  0x0, 0x96, 0x33,  0x0, 0x20,  0x0, 0x55, 0x2b,  0x0,  0x6 },
	{   0x0, 0xc0, 0xff,  0x5,  0x0,  0x0,  0x0, 0xff,  0x5,  0x3,  0x0 },
	{   0x4,  0x8, 0xf8,  0x7,  0x0,  0x1,  0x0, 0x82, 0x74,  0x0,  0x8 },
	{   0x0,  0x0, 0x2f,  0x5,  0x0, 0x20,  0x0, 0xff,  0x5,  0x3,  0xa },
	{  0x93,  0x0, 0xf7,  0x7,  0x2,  0x0,  0x0, 0xf7,  0x7,  0x0,  0xa },
	{   0x0, 0x40, 0x80, 0x7a,  0x0, 0xc4,  0x0, 0xc0, 0x7e,  0x0,  0x8 },
	{  0x90, 0x80, 0x55, 0xf5,  0x0,  0x0,  0x0, 0x55, 0xf5,  0x0,  0x8 },
	{  0xe1, 0x80, 0x34, 0xe4,  0x0, 0x69,  0x0, 0xf2,  0x6,  0x0,  0x8 },
	{   0x3,  0x2, 0xf0, 0xff,  0x3, 0x11, 0x80, 0xf0, 0xff,  0x2,  0x2 },
	{  0x1e,  0x0, 0x1f,  0xf,  0x0, 0x10,  0x0, 0x1f, 0x7f,  0x0,  0x0 },
	{   0x0,  0x0, 0x2f,  0x1,  0x0,  0x0,  0x0, 0xff,  0x1,  0x0,  0x4 },
	{  0xbe,  0x0, 0xf1,  0x1,  0x3, 0x31,  0x0, 0xf1,  0x1,  0x0,  0x4 },
	{  0x93, 0x85, 0x3f,  0x6,  0x1,  0x0,  0x0, 0x5f,  0x7,  0x0,  0x6 },
	{   0x6,  0x0, 0xa0, 0xf0,  0x0, 0x44,  0x0, 0xc5, 0x75,  0x0,  0xe },
	{  0x60,  0x0, 0x10, 0x81,  0x0, 0x20, 0x8c, 0x12, 0x91,  0x0,  0xe },
	{   0x1, 0x40, 0xf1, 0x53,  0x0,  0x8, 0x40, 0xf1, 0x53,  0x0,  0x0 },
	{  0x31,  0x0, 0x56, 0x31,  0x0, 0x16,  0x0, 0x7d, 0x41,  0x0,  0x0 },
	{   0x0, 0x10, 0xf2, 0x72,  0x0, 0x13,  0x0, 0xf2, 0x72,  0x0,  0xc },
	{  0x10,  0x0, 0x75, 0x93,  0x1,  0x1,  0x0, 0xf5, 0x82,  0x1,  0x0 },
	{   0x0,  0x0, 0xf6, 0xff,  0x2,  0x0,  0x0, 0xf6, 0xff,  0x0,  0x8 },
	{  0x30,  0x0, 0xff, 0xa0,  0x3, 0x63,  0x0, 0x65,  0xb,  0x2,  0x0 },
	{  0x2a,  0x0, 0xf6, 0x87,  0x0, 0x2b,  0x0, 0x76, 0x25,  0x0,  0x0 },
	{  0x85,  0x0, 0xb8, 0x84,  0x0, 0x43,  0x0, 0xe5, 0x8f,  0x0,  0x6 },
	{   0x7, 0x4f, 0xf2, 0x60,  0x0, 0x12,  0x0, 0xf2, 0x72,  0x0,  0x8 },
	{   0x5, 0x40, 0xb3, 0xd3,  0x0, 0x86, 0x80, 0xf2, 0x24,  0x0,  0x2 },
	{  0xd0,  0x0, 0x11, 0xcf,  0x0, 0xd1,  0x0, 0xf4, 0xe8,  0x3,  0x0 },
	{   0x5, 0x4e, 0xda, 0x25,  0x2,  0x1,  0x0, 0xf9, 0x15,  0x0,  0xa },
	{   0x3,  0x0, 0x8f,  0x7,  0x2,  0x2,  0x0, 0xff,  0x6,  0x0,  0x0 },
	{  0x13,  0x0, 0x8f,  0x7,  0x2,  0x2,  0x0, 0xf9,  0x5,  0x0,  0x0 },
	{  0xf0,  0x1, 0x97, 0x17,  0x0, 0x21,  0xd, 0xf1, 0x18,  0x0,  0x8 },
	{  0xf1, 0x41, 0x11, 0x11,  0x0, 0xf1, 0x41, 0x11, 0x11,  0x0,  0x2 },
	{  0x13,  0x0, 0x8f,  0x7,  0x2,  0x2,  0x0, 0xff,  0x6,  0x0,  0x0 },
	{   0x1,  0x0, 0x2f,  0x1,  0x0,  0x1,  0x0, 0xaf,  0x1,  0x3,  0xf },
	{   0x1,  0x6, 0xf3, 0xff,  0x0,  0x8,  0x0, 0xf7, 0xff,  0x0,  0x4 },
	{  0xc0, 0x4f, 0xf1,  0x3,  0x0, 0xbe,  0xc, 0x10,  0x1,  0x0,  0x2 },
	{   0x0,  0x2, 0xf0, 0xff,  0x0, 0x11, 0x80, 0xf0, 0xff,  0x0,  0x6 },
	{  0x81, 0x47, 0xf1, 0x83,  0x0, 0xa2,  0x4, 0x91, 0x86,  0x0,  0x6 },
	{  0xf0, 0xc0, 0xff, 0xff,  0x3, 0xe5,  0x0, 0xfb, 0xf0,  0x0,  0xe },
	{   0x0,  0x2, 0xf0, 0xff,  0x0, 0x11, 0x80, 0xf0, 0xff,  0x0,  0x6 }
};


PercussionNote percussion_notes[47] = {
	{  0x0,  0xb, 0xa8, 0x38,  0x0,  0x0,  0x0, 0xd6, 0x49,  0x0,  0x0,  0x4,  0x1,   0x97,  0x4 },
	{ 0xc0, 0xc0, 0xf8, 0x3f,  0x2, 0xc0,  0x0, 0xf6, 0x8e,  0x0,  0x0,  0x4,  0x1,   0xf7,  0x4 },
	{ 0xc0, 0x80, 0xc9, 0xab,  0x0, 0xeb, 0x40, 0xb5, 0xf6,  0x0,  0x1,  0x3,  0x1,   0x6a,  0x6 },
	{  0xc,  0x0, 0xd8, 0xa6,  0x0,  0x0,  0x0, 0xd6, 0x4f,  0x0,  0x1,  0x3,  0x1,   0x6c,  0x5 },
	{  0x1,  0x0, 0xe2, 0xd2,  0x0,  0x3, 0x41, 0x8f, 0x48, 0x49,  0xc,  0x4,  0x1,   0x2f,  0x5 },
	{  0x0,  0x0, 0xc8, 0x58,  0x3,  0x0,  0x0, 0xf6, 0x4f,  0x0,  0x9,  0x3,  0x1,  0x108,  0x4 },
	{  0x1,  0x0, 0xff,  0x5,  0x0, 0xf2, 0xff, 0xe0, 0x50, 0x52, 0x5d,  0x2,  0x1,   0x9f,  0x5 },
	{  0xe,  0x9, 0xb9, 0x47,  0x0, 0xeb, 0x40, 0xf5, 0xe6,  0x0,  0x0,  0x0,  0x1,   0x82,  0x6 },
	{  0x0,  0x0, 0xd6, 0x83,  0x0, 0xd6, 0xd7, 0xe0, 0x41, 0x5e, 0x4a,  0x2,  0x1,   0xc7,  0x5 },
	{  0x1,  0x9, 0x89, 0x67,  0x0, 0xd6, 0xd7, 0xe0, 0x41, 0x5e, 0x4a,  0x0,  0x1,   0x80,  0x6 },
	{  0x1,  0x0, 0xd6, 0x96,  0x0, 0xd6, 0xd7, 0xe0, 0x41, 0x5e, 0x4a,  0x2,  0x1,   0xed,  0x5 },
	{  0x0,  0x9, 0xa9, 0x55,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x1,   0x82,  0x6 },
	{  0x2,  0x0, 0xc6, 0x96,  0x0, 0xe0,  0x0, 0xe0, 0x40,  0x0,  0x1,  0x2,  0x1,  0x123,  0x5 },
	{  0x5,  0x0, 0xf6, 0x56,  0x0, 0xf7, 0xff, 0xb3, 0x90, 0x4f,  0x1,  0x2,  0x1,  0x15b,  0x5 },
	{  0x1,  0x0, 0xf7, 0x14,  0x0, 0xf7, 0xff, 0x36, 0x90, 0x79, 0xe7,  0x1,  0x1,  0x1ac,  0x5 },
	{  0x0,  0x0, 0xf6, 0x56,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x2,  0x1,  0x18b,  0x5 },
	{  0x0, 0x83, 0xfb,  0x5,  0x0, 0xf7, 0x41, 0x39, 0x90, 0x79,  0x1,  0x1,  0x1,   0xc8,  0x5 },
	{  0x0,  0x0, 0xff,  0x5,  0x0, 0xf7, 0xff, 0x36, 0x90, 0x79, 0xe7,  0x1,  0x1,   0xf9,  0x5 },
	{  0x1,  0x0, 0xa0,  0x5,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x2,  0x1,  0x27a,  0x6 },
	{  0x0,  0x5, 0xf3,  0x6,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x2,  0x1,  0x108,  0x7 },
	{  0x1,  0x0, 0xf9, 0x34,  0x0, 0xf7, 0xff, 0x36, 0x90, 0x79, 0xe7,  0x1,  0x1,  0x147,  0x4 },
	{  0x0,  0x0, 0xf7, 0x16,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x2,  0x1,  0x120,  0x6 },
	{  0x1,  0x0, 0xff,  0x5,  0x0, 0xf7, 0xff, 0x36, 0x90, 0x79, 0xe7,  0x1,  0x1,   0x42,  0x6 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x1,  0x0, 0xff,  0x5,  0x0, 0xf7, 0xff, 0x36, 0x90, 0x79, 0xe7,  0x1,  0x1,   0x6d,  0x5 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 },
	{  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x3fc,  0x4 }
};


/**********************************
	msc-midi driver
*/

void midi_driver() {
	while (true) {

		if (!driver_installed) {
			return;
		}
		if (driver_status != kStatusPlaying) {
			return;
		}
		
		if (midi_buffer_pos < midi_buffer_size) {
			// playback
			if (driver_fading_in) {
				if (full_volume > COARSE_VOL(fadein_volume_cur)) {
					fadein_volume_cur += fadein_volume_inc;
					midi_volume = COARSE_VOL(fadein_volume_cur);
				} else {
					driver_fading_in = false;
					midi_volume = full_volume;
					break;	//return
				}
			}
			
			if (driver_fading_out) {
				if (0 < COARSE_VOL(fadeout_volume_cur)) {
					fadeout_volume_cur -= fadeout_volume_dec;
					midi_volume = COARSE_VOL(fadeout_volume_cur);
				} else {
					driver_fading_out = false;
					midi_volume = full_volume;
					midi_fade_out_flag = false;	// needed to force stopping
					midi_fadeout_and_stop();
					midi_fade_out_flag = true;	// reset the flag
					break;	//return
				}			
			}
			
			driver_timestamp++;
			
			if (midi_event_delta != 0) {
				midi_event_delta--;
				break; // return
			}

			if (midi_event_type == 255) {
				process_midi_meta_event();
				midi_event_delta = read_midi_word();
				midi_event_type = read_midi_byte();			
			} else {
				if ((midi_event_type & 0x80) == 0) {
					// repeat the last event
					midi_buffer_pos--;
					midi_event_type = last_midi_event_type;
				}
			
				process_midi_channel_event();
				last_midi_event_type = midi_event_type;
				midi_event_delta = read_midi_word();
				midi_event_type = read_midi_byte();				
			}

		} else {
			// end-of-file
			if (midi_loop) {
				// loop the file from the beginning
				midi_buffer_pos = 7;	// skip signature and a couple of fields
				midi_event_delta = read_midi_word();
				midi_event_type = read_midi_byte();
			} else {
				midi_stop();
				break;	// return
			}
		}
	}
}

void midi_fadeout_and_stop() {
	if (!driver_installed) {
		return;
	}
	if (driver_status == kStatusStopped) {
		return;
	}

	if (midi_fade_out_flag && !driver_fading_out) {
		// start a fadeout
		driver_fading_out = true;
		full_volume = midi_volume;
		fadeout_volume_dec = FINE_VOL(midi_volume) / (midi_fade_volume_change_rate * midi_division);
		fadeout_volume_cur = FINE_VOL(midi_volume);
	} else {
		midi_stop();
	}
}

void midi_stop() {
	ADLIB_mute_voices();
	reset_hw_timer();	// restore the previous timer frequency
	driver_status = kStatusStopped;
}

void midi_pause() {
	if (!driver_installed) {
		return;
	}
	ADLIB_mute_voices();
	reset_hw_timer();
	driver_status = kStatusPaused;
}

void midi_resume() {
	if (!driver_installed) {
		return;
	}
	if (driver_status != kStatusPaused) {
		ADLIB_init_voices();
		driver_assigned_voice = 0;
		midi_buffer_pos = 4;	// skip signature
		midi_tempo = read_midi_byte();
		midi_division = read_midi_word();
		if (midi_division > 255) {
			midi_division = 192;
		}
		
		midi_event_delta = read_midi_word();
		midi_event_type = read_midi_byte();
		last_midi_event_type = 0;
		driver_timestamp = 0;
		
		if (midi_fade_in_flag && !driver_fading_in) {
			// start a fade in
			full_volume = midi_volume;
			driver_fading_in = true;
			fadein_volume_inc = FINE_VOL(midi_volume) / (midi_fade_volume_change_rate * midi_division);
			fadein_volume_cur = 0;
		}
	}

	midi_set_tempo();
	driver_status = kStatusPlaying;
}

void midi_set_tempo() {
	uint16 word_13A3F = (midi_tempo * midi_division) / 60;
	set_hw_timer(word_13A3F);
}

void process_midi_meta_event() {
	uint8 type = read_midi_byte();
	uint8 length = read_midi_byte();

	if (type == 81) {	// tempo event
		process_meta_tempo_event();
	} else {
		// discard other meta events
		for (int i = 0; i < length; ++i) {
			read_midi_byte();
		}
	}
}

#define BYTE3(a,b,c) (((a) << 16) | ((b) << 8) | (c))

void process_meta_tempo_event() {
	uint8 v0 = read_midi_byte();
	uint8 v1 = read_midi_byte();
	uint8 v2 = read_midi_byte();
	
	midi_tempo = 60000000 / BYTE3(v0,v1,v2);
	midi_set_tempo();	
}

#define NOTE_KEY(note)			((note) & 0xFF)
#define NOTE_VEL(note)			(((note) >> 8) & 0xFF)
#define NOTEON_VEL(note)		((driver_lin_volume[midi_volume] * NOTE_VEL(note)) >> 8)

void process_midi_channel_event() {
	midi_event_channel = midi_event_type & 0xF;
	uint8 event_type = midi_event_type >> 4;
	
	uint16 note_info;
	uint8 controller_number, controller_value;
	
	switch (event_type) {
	case 9: // note on
		note_info = read_midi_word();
		midi_onoff_note = NOTE_KEY(note_info);
		midi_onoff_velocity = NOTEON_VEL(midi_onoff_note);
		ADLIB_turn_on_voice();
		break;	// return
		
	case 8: // note off
		note_info = read_midi_word();
		midi_onoff_note = NOTE_KEY(note_info);
		midi_onoff_velocity = NOTE_VEL(midi_onoff_note);
		ADLIB_turn_off_voice();
		break;	// return
	
	case 12:	// program change
		midi_channels[midi_event_channel].program = read_midi_byte();
		break;	// return
		
	case 13:	// channel aftertouch
		read_midi_byte();	
		break;	// return		
	
	case 10:	// note aftertouch
		read_midi_word();	
		break;	// return		

	case 14:	// pitch bend
		// this should always read 2 bytes from the stream, so using VLQ might not be correct
		midi_pitch_bend = read_midi_VLQ();	
		ADLIB_pitch_bend(midi_pitch_bend, midi_event_channel);
		break;	
	
	case 11:	// controller
		controller_number = read_midi_byte();
		controller_value = read_midi_byte();
		
		switch (controller_number) {
		case 1:	// modulation
			ADLIB_modulation(controller_value);
			break;	// return		
			
		case 7: // main volume
			midi_channels[midi_event_channel].volume = controller_value;
			break;	// return		
		
		case 4: // foot controller
			midi_channels[midi_event_channel].pedal = (controller_value >= 64);
			break;	// return					
			
		case 123: // all notes off
			ADLIB_mute_voices();
			break; // return
		}

		break;
	
	default:
		break;
	}
}

void midi_init() {
	
	// linear map [0..127] to [0..128] (user volume to driver volume?)
	const float k = 128.0f / 127.0f;
	for (int i = 0; i < 128; ++i) {
		driver_lin_volume[i] = (uint32)round(k * (float)i);
	}

	ADLIB_init();
	
	midi_buffer_pos = 4;
	midi_tempo = 120;
	midi_division = 192;
	midi_event_delta = 0;
	midi_event_type = 0;
	last_midi_event_type = 0;
	midi_fade_volume_change_rate = 0;
	midi_fade_out_flag = false;
	midi_fade_in_flag = false;
	driver_fading_out = false;
	driver_fading_in = false;
	midi_loop = false;
	driver_status = kStatusStopped;
	driver_assigned_voice = 0;
	driver_timestamp = 0;
	midi_volume = 127;
}


/**********************************
	low-level OPL manipulation
*/

#define NUM_VOICES				9		// the driver only uses rhythm mode, so there are 9 FM voices available

uint8 operator_offsets_for_percussion[] = {
	0x11, // hi-hat operator 		[channel 7, operator 1]
	0x15, // cymbal operator		[channel 8, operator 2]
	0x12, // tom tom operator		[channel 8, operator 1]
	0x14  // snare drum operator	[channel 7, operator 1]
//  0x10  // bass drum				[channel 6, operator 1]
//  0x13  // bass drum				[channel 6, operator 2]
};

uint8 operator1_offset_for_melodic[NUM_VOICES] = {
	 0x0,  0x1,  0x2,  0x8,  0x9,  0xa, 0x10, 0x11, 0x12
};

uint8 operator2_offset_for_melodic[NUM_VOICES] = {
	 0x3,  0x4,  0x5,  0xb,  0xc,  0xd, 0x13, 0x14, 0x15
};

uint16 melodic_fnumbers[36] = {
	 0x55,   0x5a,   0x60,   0x66,   0x6c,   0x72,   0x79,   0x80,   0x88,   
	 0x90,   0x99,   0xa1,   0xab,   0xb5,   0xc0,   0xcc,   0xd8,   0xe5,   
	 0xf2,  0x101,  0x110,  0x120,  0x132,  0x143,  0x156,  0x16b,  0x181,  
	0x198,  0x1b0,  0x1ca,  0x1e5,  0x202,  0x220,  0x241,  0x263,  0x286
};


/*
	bit 7 - Clear:  AM depth is 1 dB
	bit 6 - Clear:  Vibrato depth is 7 cent
	bit 5 - Set:    Rhythm enabled  (6 melodic voices)
	bit 4 - Bass drum off
	bit 3 - Snare drum off
	bit 2 - Tom tom off
	bit 1 - Cymbal off
	bit 0 - Hi Hat off
*/
#define ADLIB_DEFAULT_PERCUSSION_MASK	0x20

/*	
 *	bit  7-6: unused		
 *	bit   5 : key on (0 mutes voice)
 *	bits 4-2: octave
 *	bits 1-0: higher 2 bits of f-number
 */
#define ADLIB_B0(key_on,octave,fnumber) ( ((key_on) & 0x10) | ((octave) & 7) | ((fnumber) & 3) )

#define ADLIB_A0(fnumber) 		(fnumber)

#define ADLIB_40(scaling,total) ( ((scaling) & 0xC0) | ((total_level) & 0x3F) )

#define INC_MOD(n,max)		((n+1) % max)


// the maximum volume value in the hardware and its bitmask
#define MAXIMUM_LEVEL			63
#define LEVEL_MASK				0x3F		//	63

#define PITCH_BEND_THRESH		8192


uint8 calc_level(uint8 velocity, uint8 program_level, uint8 midi_channel) {
/* combines note, program and channel levels, then scales it down to fit the six bits available in
   the hardware. The result is subtracted from MAXIMUM_LEVEL as the hardware's logic is
   reversed. See http://www.shipbrook.com/jeff/sb.html#40-55.
 */
	uint32 note_level = ADLIB_log_volume[velocity];
	uint32 channel_level = ADLIB_log_volume[midi_channels[midi_channel].volume];
	// program_level comes from the static data and is probably already in the correct logarithmic scale

	return MAXIMUM_LEVEL - ((note_level * channel_level * program_level) >> 16);
}

/* turn off all the voices and restore base octave and (hi) frequency */
void ADLIB_mute_voices() {
	// turn off melodic voices
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		ADLIB_mute_melodic_voice(i);
	}
	
	// turn off percussions
	ADLIB_out(0xBD, ADLIB_DEFAULT_PERCUSSION_MASK);
}


void ADLIB_init_voices() {
	for (int i = 0; i < NUM_MIDI_CHANNELS; ++i) {
		midi_channels[i].program = 0;
		midi_channels[i].volume = 127;
		midi_channels[i].pedal = 0;
	}
	
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		melodic[i].key = -1;
		melodic[i].program = -1;
		melodic[i].channel = -1;
		melodic[i].timestamp = 0;
		melodic[i].fnumber = 0;
		melodic[i].octave = 0;
		melodic[i].in_use = false;
	}
	
	// clear out current percussion notes
	memset(notes_per_percussion, 5, 0xFF);
	
	driver_fading_in = false;
	driver_fading_out = false;
	driver_percussion_mask = ADLIB_DEFAULT_PERCUSSION_MASK;
	ADLIB_out(0xBD, driver_percussion_mask);
}

void ADLIB_turn_off_voice() {
	if (midi_event_channel == 9) {
		ADLIB_onoff_percussion(false);
	} else {
		uint8 voice;	// left uninitialized !
	
		if (midi_channels[midi_event_channel].pedal == 0) {
			voice = 0xFF;	// used below as a flag
		}
		
		for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
			if (melodic[i].key == midi_onoff_note && melodic[i].channel == midi_event_channel) {
				voice = i;
			}
		}
		
		if (voice != 0xFF) {
			// mute the channel
			ADLIB_mute_melodic_voice(voice);
		}
	}
}

void ADLIB_turn_on_voice() {
	if (midi_event_channel == 9) {
		ADLIB_onoff_percussion(midi_onoff_velocity != 0);
	} else {
		if (midi_onoff_velocity == 0) {
			ADLIB_turn_off_voice();
		} else {
			ADLIB_turn_on_melodic();		
		}	
	}	
}

void ADLIB_onoff_percussion(bool onoff) {
	if (midi_onoff_note < 35 || midi_onoff_note > 81) {
		return;
	}
	PercussionNote *note = &percussion_notes[midi_onoff_note - 35];

	if (onoff) {
		if (note->valid == 0) {
			return;
		}
		if (midi_onoff_note != notes_per_percussion[note->percussion]) {
			ADLIB_setup_percussion(note);
			notes_per_percussion[note->percussion] = midi_onoff_note;
		}
		
		ADLIB_play_percussion(note, midi_onoff_velocity);	
	} else {
		driver_percussion_mask &= ~(1 << note->percussion);
	}
}

void ADLIB_program_operator(uint8 operator_offset, OplOperator *data) {
	ADLIB_out(0x20 + operator_offset, data->characteristic);
	ADLIB_out(0x60 + operator_offset, data->attack_decay);
	ADLIB_out(0x80 + operator_offset, data->sustain_release);
	ADLIB_out(0x40 + operator_offset, data->levels);
	ADLIB_out(0xE0 + operator_offset, data->waveform);
}

void ADLIB_program_operator_s(uint8 operator_offset, OplOperator *data) {
	ADLIB_out(0x40 + operator_offset, data->levels & LEVEL_MASK);
	ADLIB_out(0x60 + operator_offset, data->attack_decay);
	ADLIB_out(0x80 + operator_offset, data->sustain_release);		
}

void ADLIB_set_operator_level(uint8 operator_offset, OplOperator *data, uint8 velocity, uint8 midi_channel, bool full_volume) {
	uint8 scaling_level = data->levels;
	uint8 program_level = MAXIMUM_LEVEL - (full_volume ? 0 : (data->levels & LEVEL_MASK));
	uint8 total_level = calc_level(velocity, program_level, midi_channel);
	ADLIB_out(0x40 + operator_offset, ADLIB_40(scaling_level, total_level));
}

void ADLIB_setup_percussion(PercussionNote *note) {
	if (note->percussion < 4) {
		// simple percussions (1 operator)
		driver_percussion_mask &= ~(1 << note->percussion);
		ADLIB_out(0xBD, driver_percussion_mask);
		
		uint8 offset = operator_offsets_for_percussion[note->percussion];		
		ADLIB_program_operator_s(offset, &note->op[0]);
	} else {
		// bass drum (2 operators)
		driver_percussion_mask &= ~(0x10);
		ADLIB_out(0xBD, driver_percussion_mask);
		
		ADLIB_program_operator(0x10, &note->op[0]);
		ADLIB_program_operator(0x13, &note->op[1]);
				
		// feedback / algorithm
		uint8 voice = 6;
		ADLIB_out(0xC0 + voice, note->feedback_algo);
	}
}


void ADLIB_play_percussion(PercussionNote *note, uint8 velocity) {
	if (note->percussion < 4) {
		// simple percussion (1 operator)
		driver_percussion_mask &= ~(1 << note->percussion);
		ADLIB_out(0xBD, driver_percussion_mask);
		
		ADLIB_set_operator_level(operator_offsets_for_percussion[note->percussion], &note->op[0], velocity, midi_event_channel, true);
				
		if (note->percussion == 2) {
			// tom tom operator		[channel 8, operator 1]
			ADLIB_play_note(8, note->octave, note->fnumber);
		} else
		if (note->percussion == 3) {
			// snare drum operator	[channel 7, operator 1]
			ADLIB_play_note(7, note->octave, note->fnumber);
		}
		
		driver_percussion_mask |= (1 << note->percussion);
		ADLIB_out(0xBD, driver_percussion_mask);		
	} else {
		// bass drum (2 operators)
		driver_percussion_mask &= ~(0x10);
		ADLIB_out(0xBD, driver_percussion_mask);
	
		if (note->feedback_algo & 1) {
			// operators 1 and 2 in additive synthesis
			ADLIB_set_operator_level(0x10, &note->op[0], velocity, midi_event_channel, true);
			ADLIB_set_operator_level(0x13, &note->op[1], velocity, midi_event_channel, true);
		} else {
			// operator 2 is modulating operator 1	
			ADLIB_set_operator_level(0x13, &note->op[1], velocity, midi_event_channel, true);
		}

		ADLIB_play_note(6, note->octave, note->fnumber);		

		driver_percussion_mask |= 0x10;
		ADLIB_out(0xBD, driver_percussion_mask);				
	}
}

void ADLIB_turn_on_melodic() {
	// ideal: look for a melodic voice playing the same note with the same program
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		if (melodic[i].channel == midi_event_channel && 
			melodic[i].program == midi_channels[midi_event_channel].program &&
			melodic[i].key == midi_onoff_note) {
			ADLIB_mute_melodic_voice(i);
			ADLIB_play_melodic_note(i);
			return;
		}
	}
	
	// fallback 1: look for a free melodic voice with the same program
	uint8 voice = driver_assigned_voice;
	do {
		driver_assigned_voice = INC_MOD(driver_assigned_voice, NUM_MELODIC_VOICES);
		
		if (!melodic[driver_assigned_voice].in_use) {	
			continue;
		}
		
		if (midi_channels[midi_event_channel].program == melodic[driver_assigned_voice].program) {
			ADLIB_play_melodic_note(driver_assigned_voice);
			return;
		}
	} while (driver_assigned_voice != voice);

	// fallback 2: look for a free melodic voice
	do {
		driver_assigned_voice = INC_MOD(driver_assigned_voice, NUM_MELODIC_VOICES);
	
		if (!melodic[driver_assigned_voice].in_use) {	
			ADLIB_program_melodic_voice(driver_assigned_voice, midi_channels[midi_event_channel].program);
			ADLIB_play_melodic_note(driver_assigned_voice);
			return;		
		}
	} while (voice != driver_assigned_voice);

	// last attempt: look for any voice with the same program
	driver_assigned_voice = voice;
	do {
		driver_assigned_voice = INC_MOD(driver_assigned_voice, NUM_MELODIC_VOICES);

		if (midi_channels[midi_event_channel].program == melodic[driver_assigned_voice].program) {
			ADLIB_mute_melodic_voice(driver_assigned_voice);
			ADLIB_play_melodic_note(driver_assigned_voice);
			return;
		}
	} while (voice != driver_assigned_voice);	

	// forget the good manners and take possession of the voice with the oldest timestamp
	int32 min_timestamp = 0x7FFFFFFF;
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		if (melodic[i].timestamp < min_timestamp) {
			min_timestamp = melodic[i].timestamp;
			driver_assigned_voice = i;
		}
	}
	ADLIB_program_melodic_voice(driver_assigned_voice, midi_channels[midi_event_channel].program);
	ADLIB_play_melodic_note(driver_assigned_voice);
}

void ADLIB_program_melodic_voice(uint8 voice, uint8 program) {
	// the original decreases channel by one, but we are already counting from 0
	MelodicProgram *prg = &melodic_programs[program];
	
	uint8 offset1 = operator1_offset_for_melodic[voice];
	uint8 offset2 = operator2_offset_for_melodic[voice];
	ADLIB_out(0x40 + offset1, MAXIMUM_LEVEL);
	ADLIB_out(0x40 + offset2, MAXIMUM_LEVEL);

	ADLIB_mute_melodic_voice(voice);

	ADLIB_program_operator(offset1, &prg->op[0]);
	ADLIB_program_operator(offset2, &prg->op[1]);

	// feedback / algorithm
	ADLIB_out(0xC0 + voice, prg->feedback_algo);
}

void ADLIB_mute_melodic_voice(uint8 voice) {
	ADLIB_out(0xB0 + voice, ADLIB_B0(0, melodic[voice].octave << 2, melodic[voice].fnumber >> 8));
}

void ADLIB_play_melodic_note(uint8 voice) {
	uint8 octave = midi_onoff_note / 12;
	uint8 f = 12 + (midi_onoff_note % 12);
	if (octave > 7) {
		octave = 7;
	}
	
	uint8 program = midi_channels[midi_event_channel].program;
	MelodicProgram *prg = &melodic_programs[program];
	
	if (1 & melodic_programs[program].feedback_algo) {
		ADLIB_set_operator_level(operator1_offset_for_melodic[voice], &prg->op[0], midi_onoff_velocity, midi_event_channel, false);
		ADLIB_set_operator_level(operator2_offset_for_melodic[voice], &prg->op[1], midi_onoff_velocity, midi_event_channel, false);
	} else {
		ADLIB_set_operator_level(operator2_offset_for_melodic[voice], &prg->op[1], midi_onoff_velocity, midi_event_channel, true);
	}
	
	ADLIB_play_note(voice, octave, melodic_fnumbers[f]);

	melodic[voice].program = program;
	melodic[voice].key = midi_onoff_note;
	melodic[voice].channel = midi_event_channel;
	melodic[voice].timestamp = driver_timestamp;
	melodic[voice].fnumber = melodic_fnumbers[f];
	melodic[voice].octave = octave;
	melodic[voice].in_use = true;
}

void ADLIB_play_note(uint8 voice, uint8 octave, uint16 fnumber) {
	/* Percussions are always fed keyOn = 0 even to set the note, as they are activated using the
	   BD register instead. I wonder if they can just be fed the same value as melodic voice and
	   be done with it. */
	uint8 keyOn = (voice < NUM_MELODIC_VOICES) ? 0x20 : 0;

	ADLIB_out(0xB0 + voice, ADLIB_B0(keyOn, octave << 2, fnumber >> 8));
	ADLIB_out(0xA0 + voice, fnumber & 0xFF);
}

void ADLIB_pitch_bend(int amount, uint8 midi_channel) {
	amount -= PITCH_BEND_THRESH;
	int16 bend_amount;

	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		if (melodic[i].channel == midi_channel && melodic[i].in_use) {
			uint8 f = 12 + melodic[i].key % 12;	// index to fnumber
			if (amount > 0) {
				// bend up two semitones
				bend_amount = (amount * (melodic_fnumbers[f+2] - melodic_fnumbers[f])) / PITCH_BEND_THRESH;
			} else {
				// bend down two semitones
				bend_amount = (amount * (melodic_fnumbers[f] - melodic_fnumbers[f-2])) / PITCH_BEND_THRESH;					
			}
			bend_amount += melodic_fnumbers[f];	// add the base frequency
			ADLIB_play_note(i, melodic[i].octave, bend_amount);
			melodic[i].timestamp = driver_timestamp;
		}
	}
}

void ADLIB_init() {
	ADLIB_out(0x1, 0x80);	// ???
	ADLIB_out(0x1, 0x20);	// enable all waveforms

	// logarithmic map [0 -> 0, 1..128 -> 1..256] (driver volume to hw volume?)
	for (int i = 0; i < 129; ++i) {
		ADLIB_log_volume[i] = (uint32)round(256.0f * (log((float)i+1.0f) / log(128.0f)));
	}
	
	for (int i = 0; i < NUM_VOICES; ++i) {
		ADLIB_out(0xA0 + i, 0);
		ADLIB_out(0xB0 + i, 0);
		ADLIB_out(0xC0 + i, 0);
	}

	ADLIB_out(0xBD, driver_percussion_mask);
}

void ADLIB_modulation(int value) {
	if (value >= 64) {
		driver_percussion_mask |= 0x80;
	} else {
		driver_percussion_mask &= 0x7F;	
	}
	ADLIB_out(0xBD, driver_percussion_mask);
}

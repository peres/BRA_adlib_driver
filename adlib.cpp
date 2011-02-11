#define NUM_VOICES				15
#define NUM_MELODIC_VOICES		6		// channels 0-5	(1 voice each)
#define NUM_PERCUSSIONS			5		// channels 6 (1 voice), 7-8 (2 voices each)

struct Voice {
	uint8 program;		// why program again?
	uint8 volume;
	uint8 field_2;
} voices[NUM_VOICES];

struct MelodicVoice {
	int8 key;			// the note being played
	int8 program;		// the midi instrument? (see voice)
	int8 channel;		// the hardware channel
	int32 timestamp;
	int16 fnumber;		// frequency id (see lookup table)
	int8 octave;
	bool in_use;
} melodic[NUM_MELODIC_VOICES];

// notes being currently played for each percussion (0xFF if none)
uint8 notes_per_percussion[NUM_PERCUSSIONS];

uint8 operator_offsets_for_percussion[] = {
	0x11, // hi-hat operator 		[channel 7, operator 1]
	0x15, // cymbal operator		[channel 8, operator 2]
	0x12, // tom tom operator		[channel 8, operator 1]
	0x14  // snare drum operator	[channel 7, operator 1]
//  0x10  // bass drum				[channel 6, operator 1]
//  0x13  // bass drum				[channel 6, operator 2]
};


// (almost) static info about notes played by percussions
// fields with _2 are used only by the bass drum (2 operators)
struct PercussionNotes {
	uint8 am_vib_env;	// amplitude modulation, vibrato, envelope, keyboard scaling, modulator frequency
	uint8 am_vib_env_2;
	uint8 total_level;
	uint8 total_level_2;
	uint8 attack_decay;
	uint8 attack_decay_2;
	uint8 sustain_release;
	uint8 sustain_release_2;
	uint8 waveform;
	uint8 waveform_2;
	uint8 feedback_algo;	// only used by the bass drum
	uint8 field_B;
	uint8 field_C;
	uint8 field_D;
	uint8 field_E;
	uint8 field_F;
	uint8 percussion;
	uint8 field_11;
	uint16 fnumber;
	uint8 octave;
	uint8 field_15;
	uint8 field_16;
	uint8 field_17;
	uint8 field_18;
	uint8 field_19;
	uint8 field_1A;
	uint8 field_1B;
	uint8 field_1C;
	uint8 field_1D;
	uint8 field_1E;
	uint8 field_1F;
} percussion_notes[82];

struct MelodicProgram {
	uint8 am_vib_env;	// amplitude modulation, vibrato, envelope, keyboard scaling, modulator frequency
	uint8 am_vib_env_2;
	uint8 total_level;
	uint8 total_level_2;
	uint8 attack_decay;
	uint8 attack_decay_2;
	uint8 sustain_release;
	uint8 sustain_release_2;
	uint8 waveform;
	uint8 waveform_2;
	uint8 both_operators;
	uint8 field_B;
	uint8 field_C;
	uint8 field_D;
} melodic_programs[58];

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
#define ADLIB_DEFAULT_BD	0x20

uint8 driver_ADLIB_DB_status;

/*	
 *	bit  7-6: unused		
 *	bit   5 : key on (0 mutes voice)
 *	bits 4-2: octave
 *	bits 1-0: higher 2 bits of f-number
 */
#define ADLIB_B0(key_on,octave,fnumber) ( ((key_on) & 1) | ((octave) & 7) | ((fnumber)) & 3))


#define ADLIB_A0(fnumber) 		(fnumber)

#define ADLIB_40(scaling,total) ( ((scaling) & 0xC0) | ((total_level) & 0x3F) )


/* turn off all the voices and restore base octave and (hi) frequency */
void ADLIB_mute_voices() {
	// turn off melodic voices
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		ADLIB_out(0xB0 + i, ADLIB_B0(0, melodic[i].octave << 2, melodic[i].fnumber >> 8));
	}
	
	// turn off percussions
	ADLIB_out(0xBD, ADLIB_DEFAULT_BD);
}


void ADLIB_init_voices() {
	for (int i = 0; i < NUM_VOICES; ++i) {
		voices[i].program = 0;
		voices[i].volume = 127;
		voices[i].field_2 = 0;
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
	driver_ADLIB_DB_status = ADLIB_DEFAULT_BD;
}

void ADLIB_turn_off_voice() {
	if (midi_event_channel == 9) {
		ADLIB_turn_off_percussion();
	} else {
		uint8 voice;	// left uninitialized !
	
		if (voices[midi_channel_event].field_2 == 0) {
			voice = 0;
		}
		
		for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
			if (melodic[i].key == midi_onoff_note && melodic[i].channel == midi_event_channel) {
				voice = i;
			}
		}
		
		if (voice) {
			// mute the channel
			ADLIB_out(0xB0 + voice, ADLIB_B0(0, melodic[voice].octave << 2, melodic[voice].fnumber >> 8));
		}
	}
}

void ADLIB_turn_off_percussion() {
	driver_ADLIB_DB_status &= ~(1 << percussion_notes[midi_onoff_note].percussion);
}

void ADLIB_turn_on_voice() {
	if (midi_event_channel == 9) {
		if (midi_onoff_speed == 0) {
			ADLIB_turn_off_percussion();
		} else {
			ADLIB_turn_on_percussion();		
		}
	} else {
		if (midi_onoff_speed == 0) {
			ADLIB_turn_off_voice();
		} else {
			ADLIB_turn_on_melodic();		
		}	
}

void ADLIB_turn_on_percussion() {
	if (midi_onoff_note < 35 || midi_onoff_note > 81) {
		return;
	}
	if (percussion_notes[midi_onoff_note].field_11 == 0) {
		return;
	}
	
	if (midi_onoff_note != notes_per_percussion[percussion_notes[midi_onoff_note].percussion]) {
		ADLIB_setup_percussion(percussion_notes[midi_onoff_note].percussion, midi_onoff_note);
		notes_per_percussion[percussion_notes[midi_onoff_note].percussion] = midi_onoff_note;
	}
	
	ADLIB_play_percussion();
}

void ADLIB_setup_percussion(uint8 percussion_number, uint8 note) {
	if (percussion_number < 4) {
		// simple percussions (1 operator)
		driver_ADLIB_DB_status &= ~(1 << percussion_number);
		ADLIB_out(0xBD, driver_ADLIB_DB_status);
		
		uint8 offset = operator_offsets_for_percussion[percussion_number];		
		ADLIB_out(0x40 + offset, percussion_notes[note].total_level & 0x3F);
		ADLIB_out(0x60 + offset, percussion_notes[note].attack_decay);
		ADLIB_out(0x80 + offset, percussion_notes[note].sustain_release);		
	} else {
		// bass drum (2 operators)
		driver_ADLIB_DB_status &= ~(0x10);
		ADLIB_out(0xBD, driver_ADLIB_DB_status);
		
		// first operator
		ADLIB_out(0x30, percussion_notes[note].am_vib_env);
		ADLIB_out(0x50, percussion_notes[note].total_level);
		ADLIB_out(0x70, percussion_notes[note].attack_decay);
		ADLIB_out(0x90, percussion_notes[note].sustain_release);
		ADLIB_out(0xF0, percussion_notes[note].waveform);

		// second operator
		ADLIB_out(0x33, percussion_notes[note].am_vib_env_2);
		ADLIB_out(0x53, percussion_notes[note].total_level_2);
		ADLIB_out(0x73, percussion_notes[note].attack_decay_2);
		ADLIB_out(0x93, percussion_notes[note].sustain_release_2);
		ADLIB_out(0xF3, percussion_notes[note].waveform_2);		
		
		// feedback / algorithm
		ADLIB_out(0xC6, percussion_notes[note].feedback_algo);
	}
}

#define TOTAL_LEVEL(speed)	(63 - ((dword_13B17[speed] * 63 * dword_13B17[voices[midi_event_channel].volume]) >> 16))


void ADLIB_play_percussion() {
	uint8 percussion_number = percussion_notes[midi_onoff_note].percussion;
	if (percussion_number < 4) {
		// simple percussion (1 operator)
		driver_ADLIB_DB_status &= ~(1 << percussion_number);
		ADLIB_out(0xBD, driver_ADLIB_DB_status);
		
		uint8 offset = operator_offsets_for_percussion[percussion_number];		
		
		uint8 scaling_level = percussion_notes[midi_onoff_note].total_level;
		uint8 total_level = TOTAL_LEVEL(midi_onoff_speed);
		ADLIB_out(0x40 + offset, ADLIB_40(scaling_level, percussion_notes[midi_onoff_note].total_level));
		
		if (percussion_number == 2) {
			// tom tom operator		[channel 8, operator 1]
			octave = percussion_notes[midi_onoff_note].octave << 2;
			fnumber = percussion_notes[midi_onoff_note].fnumber >> 8;
			ADLIB_out(0xB8, ADLIB_B0(0,octave,fnumber));
			ADLIB_out(0xA8, ADLIB_A0(percussion_notes[midi_onoff_note].fnumber & 0xFF));
		} else
		if (percussion_number == 3) {
			// snare drum operator	[channel 7, operator 1]
			octave = percussion_notes[midi_onoff_note].octave << 2;
			fnumber = percussion_notes[midi_onoff_note].fnumber >> 8;
			ADLIB_out(0xB7, ADLIB_B0(0,octave,fnumber));
			ADLIB_out(0xA7, ADLIB_A0(percussion_notes[midi_onoff_note].fnumber & 0xFF));
		}
		
		driver_ADLIB_DB_status |= (1 << percussion_number);
		ADLIB_out(0xBD, driver_ADLIB_DB_status);		
	} else {
		// bass drum (2 operators)
		driver_ADLIB_DB_status &= ~(0x10);
	
		if (percussions.feedback_algo[midi_onoff_note]) {
			// operator 2 is modulation operation 1	
			uint8 scaling_level = percussion_notes[midi_onoff_note].total_level_2;
			uint8 total_level = TOTAL_LEVEL(midi_onoff_speed);
			ADLIB_out(0x53, ADLIB_40(scaling_level, total_level));
		} else {
			// operators 1 and 2 are independent
			uint8 scaling_level = percussion_notes[midi_onoff_note].total_level;
			uint8 total_level = TOTAL_LEVEL(midi_onoff_speed);
			ADLIB_out(0x40 + offset, ADLIB_40(scaling_level, total_level);

			uint8 scaling_level = percussion_notes[midi_onoff_note].total_level_2;
			uint8 total_level = TOTAL_LEVEL(midi_onoff_speed);
			ADLIB_out(0x53, ADLIB_40(scaling_level, total_level));
		}
		
		octave = percussion_notes[midi_onoff_note].octave << 2;
		fnumber = percussion_notes[midi_onoff_note].fnumber >> 8;
		ADLIB_out(0xB6, ADLIB_B0(0,octave,fnumber));
		ADLIB_out(0xA6, ADLIB_A0(percussion_notes[midi_onoff_note].fnumber & 0xFF));

		driver_ADLIB_DB_status |= 0x10;
		ADLIB_out(0xBD, driver_ADLIB_DB_status);				
	}
}

#define INC_MOD(n,max)		((n+1) % max)

void ADLIB_turn_on_melodic() {
	// ideal: look for a melodic voice playing the same note with the same program
	for (int i = 0; i < NUM_MELODIC_VOICES; ++i) {
		if (melodic[i].channel == midi_event_channel && 
			melodic[i].program == voices[midi_event_channel].program &&
			melodic[i].note == midi_onoff_note) {
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
		
		if (voices[midi_event_channel].program == melodic[driver_assigned_voice].program) {
			ADLIB_play_melodic_note(driver_assigned_voice);
			return;
		}
	} while (driver_assigned_voice != voice);

	// fallback 2: look for a free melodic voice
	do {
		driver_assigned_voice = INC_MOD(driver_assigned_voice, NUM_MELODIC_VOICES);
	
		if (!melodic[driver_assigned_voice].in_use) {	
			ADLIB_program_melodic_voice(driver_assigned_voice, voices[midi_event_channel].program);
			ADLIB_play_melodic_note(driver_assigned_voice);
			return;		
		}
	} while (voice != driver_assigned_voice);

	// last attempt: look for any voice with the same program
	driver_assigned_voice = voice;
	do {
		driver_assigned_voice = INC_MOD(driver_assigned_voice, NUM_MELODIC_VOICES);

		if (voices[midi_event_channel].program == melodic[driver_assigned_voice].program) {
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
	ADLIB_program_melodic_voice(driver_assigned_voice, voices[midi_event_channel].program);
	ADLIB_play_melodic_note(driver_assigned_voice);
}

void ADLIB_program_melodic_voice(uint8 voice, uint8 program) {
	// the original decreases channel by one, but we are already counting from 0
	
	uint8 offset1 = operator1_offset_for_melodic[voice];
	uint8 offset2 = operator2_offset_for_melodic[voice];
	ADLIB_out(0x40 + offset1, 0x3F);
	ADLIB_out(0x40 + offset2, 0x3F);

	ADLIB_out(0xB0 + voice, ADLIB_B0(0, melodic[voice].octave << 2, melodic[voice].fnumber >> 8));

	ADLIB_out(0x20 + offset1, melodic_programs[program].am_vib_env);
	ADLIB_out(0x60 + offset1, melodic_programs[program].attack_decay);
	ADLIB_out(0x80 + offset1, melodic_programs[program].sustain_release);
	ADLIB_out(0xE0 + offset1, melodic_programs[program].waveform);
	ADLIB_out(0x40 + offset1, melodic_programs[program].total_level);
	
	ADLIB_out(0x20 + offset2, melodic_programs[program].am_vib_env_2);
	ADLIB_out(0x60 + offset2, melodic_programs[program].attack_decay_2);
	ADLIB_out(0x80 + offset2, melodic_programs[program].sustain_release_2);
	ADLIB_out(0xE0 + offset2, melodic_programs[program].waveform_2);
	ADLIB_out(0x40 + offset2, melodic_programs[program].total_level_2);
}

void ADLIB_mute_melodic_voice(uint8 voice) {
	ADLIB_out(0xB0 + voice, ADLIB_B0(0, melodic[voice].octave << 2, melodic[voice].fnumber >> 8));
}

void ADLIB_play_melodic_note(uint8 voice) {
	uint8 octave = midi_onoff_note / 12;
	uint8 fnumber = 12 + (midi_onoff_note % 12);
	if (octave > 7) {
		octave = 7;
	}
	
	uint8 program = voices[midi_event_channel].program;
	
	if (1 & melodic_programs[program].both_operators) {
		uint8 offset1 = operator1_offset_for_melodic[voice];
		uint8 scaling_level = melodic_programs[program].total_level;
		uint8 total_level = 63 - ((dword_13B17[voices[midi_event_channel].volume] * dword_13B17[midi_onoff_speed] * (63 - (melodic_programs[program].total_level & 0x3F))) >> 16);		
		ADLIB_out(0x40 + offset1, ADLIB_40(scaling_level, total_level));

		uint8 offset2 = operator2_offset_for_melodic[voice];
		scaling_level = melodic_programs[program].total_level_2;
		total_level = 63 - ((dword_13B17[voices[midi_event_channel].volume] * dword_13B17[midi_onoff_speed] * (63 - (melodic_programs[program].total_level_2 & 0x3F))) >> 16);		
		ADLIB_out(0x40 + offset2, ADLIB_40(scaling_level, total_level));
	} else {
		uint8 offset2 = operator2_offset_for_melodic[voice];
		uint8 scaling_level = melodic_programs[program].total_level_2;
		uint8 total_level = TOTAL_LEVEL(midi_onoff_speed);
		ADLIB_out(0x40 + offset2, ADLIB_40(scaling_level, total_level));
	}
	
	ADLIB_out(0xB0 + voice, ADLIB_B0(1,octave << 2,melodic_fnumbers[fnumber] >> 8);
	ADLIB_out(0xA0 + voice, melodic_fnumbers[fnumber] & 0xFF);

	melodic[voice].program = program;
	melodic[voice].note = midi_onoff_note;
	melodic[voice].channel = midi_event_channel;
	melodic[voice].timestamp = driver_timestamp;
	melodic[voice].fnumber = fnumber;
	melodic[voice].octave = octave;
	melodic[voice].in_use = true;
}
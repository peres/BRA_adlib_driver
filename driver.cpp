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
uint8  midi_onoff_speed;

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

// internal fine volume
uint16 full_volume;

#define COARSE_VOL(x)	((x)>>8)
#define FINE_VOL(x)		((x)<<8)

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
	ADLIB_mute();
	reset_hw_timer();	// restore the previous timer frequency
	interrupt_cycle_ratio = 1;
	driver_status = kStatusStopped;
}

void midi_pause() {
	if (!driver_installed) {
		return;
	}
	ADLIB_mute();
	reset_hw_timer();
	interrupt_cycle_ratio = 1;
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
		interrupt_cycles = 0;
		
		ADLIB_out(0xFFDB, driver_ADLIB_DB_status);
		if (fade_in_flag && !driver_fading_in) {
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
	interrupt_cycle_ratio = (word_13A3F << 4) / 291;
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
#define NOTE_SPEED(note)		(((note) >> 8) & 0xFF)
#define NOTEON_SPEED(note)		((dword_13D18[midi_volume] * NOTE_SPEED(note)) >> 8)

void process_midi_channel_event() {
	midi_event_channel = midi_event_type & 0xF;
	uint8 event_type = midi_event_type >> 4;
	
	uint16 note_info;
	uint8 controller_number, controller_value;
	
	switch (event_type) {
	case 9: // note on
		note_info = read_midi_word();
		midi_onoff_note = NOTE_KEY(note_info);
		midi_onoff_speed = NOTEON_SPEED(midi_onoff_note);
		ADLIB_turn_on_voice();
		break;	// return
		
	case 8: // note off
		note_info = read_midi_word();
		midi_onoff_note = NOTE_KEY(note_info);
		midi_onoff_speed = NOTE_SPEED(midi_onoff_note);
		ADLIB_turn_off_voice();
		break;	// return
	
	case 12:	// program change
		voices[midi_event_channel].program = read_midi_byte();
		break;	// return
		
	case 13:	// channel aftertouch
		read_midi_byte();	
		break;	// return		
	
	case 10:	// note aftertouch
		read_midi_word();	
		break;	// return		

	case 14:	// pitch bend
		break;	
	
	case 11:	// controller
		controller_number = read_midi_byte();
		controller_value = read_midi_byte();
		
		switch (controller_number) {
		case 1:	// modulation
			if (controller_value >= 64) {
				driver_ADLIB_DB_status |= 0x80;
			} else {
				driver_ADLIB_DB_status &= 0x7F;	
			}
			ADLIB_out(0xBD, driver_ADLIB_DB_status);
			break;	// return		
			
		case 7: // main volume
			voices[midi_event_channel].volume = controller_value;
			break;	// return		
		
		case 4: // foot controller
			voices[midi_event_channel].field_2 = (controller_value >= 64);
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
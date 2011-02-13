// these are shared with the main executable
uint8 command;
uint16 parameter;

// int 8 (timer)
void interrupt_handler() {
	switch (command) {
	case 1:
		midi_stop();
		midi_buffer_hi = parameter;
		break;
	case 2:
		midi_stop();
		midi_buffer_lo = parameter;
		break;
	case 3:
		midi_buffer_size = parameter;
		break;
	case 4:
		midi_resume();
		break;
	case 5:
		midi_fadeout_and_stop();
		break;
	case 6:
		midi_pause();
		eak;
	case 7:
		voices[parameter & 0xFF].volume = parameter >> 8;
		break;
	case 8:
		midi_fade_in_flag = parameter != 0;
		break;
	case 9:
		midi_fade_out_flag = parameter != 0;
		break;
	case 10:
		midi_volume = parameter;
		break;
	case 11:
		reset_hw_timer();
		ADLIB_mute_voices();
		set_interrupt_handler(8, old_interrupt_handler);
		break;
	case 12:
		parameter = driver_status;
		break;
	case 13:
		midi_fade_volume_change_rate = parameter & 0xFF;
		fadeout_volume_cur = 0;
		fadein_volume_cur = 0;
		break;
	case 14:
		parameter = midi_volume;
		break;
	case 15:
		parameter = midi_fade_in_flag;
		break;
	case 16:
		parameter = midi_fade_out_flag;
		break;
	case 17:
		midi_tempo = parameter & 0xFF;
		midi_set_tempo();
		break;
	case 18:
		parameter = midi_tempo;
		break;
	case 19:
		parameter = midi_fade_volume_change_rate;
		break;
	case 20:
		midi_loop = parameter != 0;
		break;
	case 21:
		parameter = midi_loop;
		break;
	case 22:
		parameter = 0xF0;	// version??
		break;
	case 23:
		parameter = 1;		// version??
		break;
	case 24:
		voices[parameter & 0xFF].program = parameter >> 8;
		break;
	case 25:
		parameter = voices[parameter & 0xFF].program;
		break;
	}
	
	command = 0;
	midi_driver();
	interrupt_cycles++;
	if (interrupt_cycles >= interrupt_ratio) {
		old_interrupt_handler();
		interrupt_cycles = 0;
	}
}
/*
  Previous - sdlaudio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the SDL interface for sound input and sound output.
*/
const char SDLaudio_fileid[] = "Previous sdlaudio.c";

#include "main.h"
#include "audio.h"
#include "sdlaudio.h"
#include "log.h"
#include "statusbar.h"


struct audio_t {
	uint8_t data[4096];
	int read;
	int size;
	int freq;
	int chan;
	SDL_AudioStream* stream;
	bool enabled;
};

static struct audio_t audio_playback;
static struct audio_t audio_recording;
static struct audio_t audio_dsp;

/*-----------------------------------------------------------------------*/
/**
 * Sound playback functions.
 */
static int Audio_Buffer_Size;

void Audio_FormatChanged(bool recording) {
	if (!recording && audio_playback.stream) {
		SDL_AudioSpec spec = { 0 };
		int frames = 0;
		if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio_playback.stream), &spec, &frames)) {
			Audio_Buffer_Size = (frames > 0 ? frames : 1024) * SDL_AUDIO_FRAMESIZE(spec);
		} else {
			Audio_Buffer_Size = 4096;
		}
		Log_Printf(LOG_WARN, "[Audio] Output buffer size: %d byte", Audio_Buffer_Size);
	}
}

void Audio_Output_Queue_Put(uint8_t* data, int len) {
	if (audio_playback.stream && len > 0) {
		SDL_PutAudioStreamData(audio_playback.stream, data, len);
	}
}

int Audio_Output_Queue_Size(void) {
	if (audio_playback.stream) {
		if (SDL_GetAudioStreamAvailable(audio_playback.stream) > Audio_Buffer_Size) {
			return SDL_GetAudioStreamQueued(audio_playback.stream);
		}
	}
	return 0;
}

void Audio_Output_Queue_Flush(void) {
	if (audio_playback.stream) {
		SDL_FlushAudioStream(audio_playback.stream);
	}
}

void Audio_Output_Queue_Clear(void) {
	if (audio_playback.stream) {
		SDL_ClearAudioStream(audio_playback.stream);
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Sound recording functions.
 */
static int Audio_Data_Get(struct audio_t* audio, int16_t* sample) {
	if (audio->stream) {
		if (audio->read >= audio->size) { /* Try to re-fill buffer in case it is empty. */
			audio->read = 0;
			audio->size = SDL_GetAudioStreamData(audio->stream, audio->data, sizeof(audio->data));
			if (audio->size & 1) {
				Log_Printf(LOG_WARN, "[Audio] Recorded data has invalid size (%d).", audio->size);
				audio->size--;
			}
		}
		if (audio->read < audio->size) {
			*sample = (((uint16_t)audio->data[audio->read] << 8) | audio->data[audio->read + 1]);
			audio->read += 2;
		} else {
			return -1;
		}
	} else {
		*sample = 0; /* silence */
	}
	return 0;
}

int Audio_Input_Buffer_Size(void) {
	if (audio_recording.stream) {
		return SDL_GetAudioStreamAvailable(audio_recording.stream);
	}
	return 0;
}

int Audio_Input_Buffer_Get(int16_t* sample) {
	return Audio_Data_Get(&audio_recording, sample);
}

int Audio_DSP_Buffer_Get(int16_t* sample) {
	return Audio_Data_Get(&audio_dsp, sample);
}

/*-----------------------------------------------------------------------*/
/**
 * Start/Stop playback and recording.
 */
static void Audio_Init_Data(struct audio_t* audio, int init) {
	Log_Printf(LOG_WARN, "[Audio] Initialising buffer with %d samples of silence.", init / (2 * audio->chan));
	/* Initialise buffer with silence to compensate for time gap between
	 * Audio_Input_Enable() and first availability of recorded data. */
	audio->read = 0;
	audio->size = init;
	memset(audio->data, 0, audio->size);
}

static void Audio_Enable(struct audio_t* audio, bool bEnable) {
	if (audio->stream) {
		if (bEnable && SDL_AudioStreamDevicePaused(audio->stream)) {
			/* Start */
			Audio_Init_Data(audio, 32);
			SDL_ResumeAudioStreamDevice(audio->stream);
		} else if (!bEnable && !SDL_AudioStreamDevicePaused(audio->stream)) {
			/* Stop */
			SDL_PauseAudioStreamDevice(audio->stream);
		}
	}
	audio->enabled = bEnable;
}

void Audio_Output_Enable(bool bEnable) {
	Audio_Enable(&audio_playback, bEnable);
}

/*-----------------------------------------------------------------------*/
/**
 * Initialise the audio subsystem.
 */
static void Audio_Open(struct audio_t* audio, SDL_AudioDeviceID dev, int channels, int freq) {
	if (audio->stream == NULL) {
		SDL_AudioSpec request = {SDL_AUDIO_S16BE, channels, freq};
		
		/* Init the SDL's audio subsystem: */
		if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
			if (SDL_InitSubSystem(SDL_INIT_AUDIO) == false) {
				Log_Printf(LOG_WARN, "[Audio] Could not init audio subsystem: %s", SDL_GetError());
				Statusbar_AddMessage("Error: Can't open SDL audio subsystem.", 5000);
				return;
			}
		}
		/* Open streaming device */
		audio->stream = SDL_OpenAudioDeviceStream(dev, &request, NULL, NULL);
		if (audio->stream == NULL) {
			Log_Printf(LOG_WARN, "[Audio] Could not open audio device: %s", SDL_GetError());
			Statusbar_AddMessage("Error: Can't open audio output device. No sound.", 5000);
		}
	}
	audio->chan = channels;
	audio->freq = freq;
}

void Audio_Output_Init(int channels, int freq) {
	Audio_Open(&audio_playback, SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, channels, freq);
	Audio_FormatChanged(false);
}

void Audio_Input_InitAndEnable(int channels, int freq) {
	Audio_Open(&audio_recording, SDL_AUDIO_DEVICE_DEFAULT_RECORDING, channels, freq);
	Audio_Enable(&audio_recording, true);
}

void Audio_DSP_InitAndEnable(int channels, int freq) {
	Audio_Open(&audio_dsp, SDL_AUDIO_DEVICE_DEFAULT_RECORDING, channels, freq);
	Audio_Enable(&audio_dsp, true);
}

/*-----------------------------------------------------------------------*/
/**
 * Free the audio subsystem.
 */
static void Audio_Close(struct audio_t* audio) {
	if (audio->stream) {
		/* Stop and close audio stream */
		SDL_DestroyAudioStream(audio->stream);
	}
	memset(audio, 0, sizeof(struct audio_t));
}

void Audio_Output_UnInit(void) {
	Audio_Close(&audio_playback);
}

void Audio_Input_UnInit(void) {
	Audio_Close(&audio_recording);
}

void Audio_DSP_UnInit(void) {
	Audio_Close(&audio_dsp);
}

/*-----------------------------------------------------------------------*/
/**
 * Handle audio device connect and disconnect.
 */
static void Audio_Handle_Connect(struct audio_t* audio, SDL_AudioDeviceID dev) {
	if (audio->freq > 0 && audio->stream == NULL) {
		Audio_Open(audio, dev, audio->chan, audio->freq);
		Audio_Enable(audio, audio->enabled);
	}
}

static void Audio_Handle_Disconnect(struct audio_t* audio) {
	if (audio->freq > 0 && SDL_GetAudioStreamDevice(audio->stream) == 0) {
		SDL_DestroyAudioStream(audio->stream);
		audio->stream = NULL;
	}
}

void Audio_DeviceConnected(bool recording) {
	if (recording) {
		Audio_Handle_Connect(&audio_recording, SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
		Audio_Handle_Connect(&audio_dsp, SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
	} else {
		Audio_Handle_Connect(&audio_playback, SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
		Audio_FormatChanged(recording);
	}
}

void Audio_DeviceDisconnected(bool recording) {
	if (recording) {
		Audio_Handle_Disconnect(&audio_recording);
		Audio_Handle_Disconnect(&audio_dsp);
	} else {
		Audio_Handle_Disconnect(&audio_playback);
	}
}

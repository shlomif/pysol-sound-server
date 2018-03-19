/*
    MIXERLIB:  An audio mixer library based on the SDL library
    Copyright (C) 1997-1999  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    5635-34 Springhouse Dr.
    Pleasanton, CA 94588 (USA)
    slouken@devolution.com
*/

/* $Id: mixer.c,v 1.17 2000/02/01 21:16:28 hercules Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_mutex.h"
#include "SDL_endian.h"
#include "SDL_timer.h"

#include "SDL_mixer.h"

#undef channel
#define channel	mix_channel

static int audio_opened = 0;

static SDL_AudioSpec mixer;
static SDL_mutex *mixer_lock;
static struct _Mix_Channel {
	Mix_Chunk *chunk;
	int playing;
	int paused;
	Uint8 *samples;
	int volume;
	int looping;
	int tag;
	Uint32 expire;
	Uint32 start_time;
	Mix_Fading fading;
	int fade_volume;
	Uint32 fade_length;
	Uint32 ticks_fade;
} *channel = NULL;
static int num_channels;
static int reserved_channels = 0;


/* Support for hooking into the mixer callback system */
static void (*mix_postmix)(void *udata, Uint8 *stream, int len) = NULL;
static void *mix_postmix_data = NULL;

/* Music function declarations */
extern int open_music(SDL_AudioSpec *mixer);
extern void close_music(void);

/* Support for user defined music functions, plus the default one */
extern int volatile music_active;
extern void music_mixer(void *udata, Uint8 *stream, int len);
static void (*mix_music)(void *udata, Uint8 *stream, int len) = music_mixer;
static void *music_data = NULL;

/* Mixing function */
static void mix_channels(void *udata, Uint8 *stream, int len)
{
    int remaining_len, step;
    int i, mixable, volume;
    Uint32 sdl_ticks;

    /* Break mixing into blocks so that we are more responsive
     * to Mix_PlayChannel() calls and decrease the average
     * response time. */
    step = 4096;
    for (remaining_len = len; remaining_len > 0; remaining_len -= len)
    {
	len = remaining_len > step ? step : remaining_len;

	/* Mix the music (must be done before the channels are added) */
	if ( music_active || (mix_music != music_mixer) )
	    mix_music(music_data, stream, len);

	/* Grab the channels we need to mix */
	SDL_mutexP(mixer_lock);
	sdl_ticks = SDL_GetTicks();
	for ( i = 0; i < num_channels; ++i )
	{
	    if( channel[i].paused )
		continue;

	    if ( channel[i].expire > 0 && channel[i].expire < sdl_ticks )
	    {
		/* Expiration delay for that channel is reached */
		channel[i].playing = 0;
		channel[i].fading = MIX_NO_FADING;
		channel[i].expire = 0;
	    }
	    else if ( channel[i].fading != MIX_NO_FADING )
	    {
		Uint32 ticks = sdl_ticks - channel[i].ticks_fade;
		if( ticks > channel[i].fade_length )
		{
		    if( channel[i].fading == MIX_FADING_OUT )
		    {
			channel[i].playing = 0;
			channel[i].expire = 0;
			Mix_Volume(i, channel[i].fading); /* Restore the volume */
		    }
		    channel[i].fading = MIX_NO_FADING;
		}
		else
		{
		    if( channel[i].fading == MIX_FADING_OUT )
			ticks = channel[i].fade_length - ticks;
		    volume = (channel[i].fade_volume * ticks) / channel[i].fade_length;
		    Mix_Volume(i, volume);
		}
	    }

	    if ( channel[i].playing == 0 )
		continue;

	    volume = (channel[i].volume*channel[i].chunk->volume) / MIX_MAX_VOLUME;
	    mixable = channel[i].playing;
	    if ( mixable > len )
		mixable = len;
	    SDL_MixAudio(stream,channel[i].samples,mixable,volume);
	    channel[i].samples += mixable;
	    channel[i].playing -= mixable;

	    /* If looping the sample and we are at its end, make sure
	     * we will still return a full buffer */
	    while (mixable < len && channel[i].looping)
	    {
		int remaining = len - mixable;
		int alen = channel[i].chunk->alen;
		if (remaining > alen)
		    remaining = alen;
		--channel[i].looping;
		SDL_MixAudio(stream+mixable, channel[i].chunk->abuf, remaining, volume);
		channel[i].samples = channel[i].chunk->abuf + remaining;
		channel[i].playing = channel[i].chunk->alen - remaining;
		mixable += remaining;
	    }

	    /* Restart any looping samples for the next run */
	    if ( ! channel[i].playing && channel[i].looping )
	    {
		if ( --channel[i].looping )
		{
		    channel[i].samples = channel[i].chunk->abuf;
		    channel[i].playing = channel[i].chunk->alen;
		}
	    }
	}
	SDL_mutexV(mixer_lock);

	if ( mix_postmix )
	    mix_postmix(mix_postmix_data, stream, len);

	stream += len;
    }
}


#if 0
static void PrintFormat(const char *title, SDL_AudioSpec *fmt)
{
	printf("%s: %d bit %s audio (%s) at %u Hz\n", title, (fmt->format&0xFF),
			(fmt->format&0x8000) ? "signed" : "unsigned",
			(fmt->channels > 1) ? "stereo" : "mono", fmt->freq);
}
#endif

int Mix_GetMixerInfo(SDL_AudioSpec *m, char *namebuf, int maxlen)
{
    if (!audio_opened)
	return -1;
    if (m)
        *m = mixer;    		/* struct copy */
    if (namebuf && maxlen > 0)
    {
	namebuf[0] = 0;
	if (SDL_AudioDriverName(namebuf, maxlen) == NULL)
	    namebuf[0] = 0;
    }
    return 0;
}


/* Open the mixer with a certain desired audio format */
int Mix_OpenAudio(int frequency, Uint16 format, int nchannels, int chunksize)
{
	int i;
	SDL_AudioSpec desired;

	/* If the mixer is already opened, increment open count */
	if ( audio_opened ) {
	    ++audio_opened;
	    return(0);
	}

	/* Set the desired format and frequency */
	desired.freq = frequency;
	desired.format = format;
	desired.channels = nchannels;
	desired.samples = chunksize;
	desired.callback = mix_channels;
	desired.userdata = NULL;

	/* Accept nearly any audio format */
	if ( SDL_OpenAudio(&desired, &mixer) < 0 ) {
		return(-1);
	}
#if 0
	PrintFormat("Audio device", &mixer);
#endif

	/* Create the channel lock mutex */
	mixer_lock = SDL_CreateMutex();
	if ( mixer_lock == NULL ) {
		SDL_CloseAudio();
		SDL_SetError("Unable to create mixer lock");
		return(-1);
	}

	/* Initialize the music players */
	if ( open_music(&mixer) < 0 ) {
		SDL_CloseAudio();
		SDL_DestroyMutex(mixer_lock);
		return(-1);
	}

	num_channels = MIX_CHANNELS;
	channel = (struct _Mix_Channel *) malloc(num_channels * sizeof(struct _Mix_Channel));
	if ( channel == NULL ) {
		SDL_SetError("Out of memory");
		return(-1);
	}
	memset(channel, 0, num_channels * sizeof(struct _Mix_Channel));

	/* Clear out the audio channels */
	for ( i=0; i<num_channels; ++i ) {
		channel[i].chunk = NULL;
		channel[i].playing = 0;
		channel[i].paused = 0;
		channel[i].looping = 0;
		channel[i].volume = SDL_MIX_MAXVOLUME;
		channel[i].tag = -1;
		channel[i].expire = 0;
		channel[i].fading = MIX_NO_FADING;
	}
	Mix_VolumeMusic(SDL_MIX_MAXVOLUME);
	audio_opened = 1;
	SDL_PauseAudio(0);
	return(0);
}

/* Dynamically change the number of channels managed by the mixer.
   If decreasing the number of channels, the upper channels are
   stopped.
 */
int Mix_AllocateChannels(int numchans)
{
	if ( numchans<0 || numchans==num_channels )
		return(num_channels);

	if ( numchans < num_channels ) {
		/* Stop the affected channels */
		int i;
		for(i=numchans; i < num_channels; i++) {
			Mix_HaltChannel(i);
		}
	}
	SDL_mutexP(mixer_lock);
	channel = (struct _Mix_Channel *) realloc(channel, numchans * sizeof(struct _Mix_Channel));
	if ( numchans > num_channels ) {
		/* Initialize the new channels */
		int i;
                memset(&channel[num_channels], 0, (numchans - num_channels) * sizeof(struct _Mix_Channel));
		for(i=num_channels; i < numchans; i++) {
			channel[i].chunk = NULL;
			channel[i].playing = 0;
			channel[i].paused = 0;
			channel[i].looping = 0;
			channel[i].volume = SDL_MIX_MAXVOLUME;
			channel[i].tag = -1;
			channel[i].expire = 0;
			channel[i].fading = MIX_NO_FADING;
		}
	}
	num_channels = numchans;
	SDL_mutexV(mixer_lock);
	return(num_channels);
}

/* Return the actual mixer parameters */
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
	if ( audio_opened ) {
		if ( frequency ) {
			*frequency = mixer.freq;
		}
		if ( format ) {
			*format = mixer.format;
		}
		if ( channels ) {
			*channels = mixer.channels;
		}
	}
	return(audio_opened);
}

/* Load a wave file */
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc)
{
	Mix_Chunk *chunk;
	SDL_AudioSpec wavespec;
	SDL_AudioCVT wavecvt;
	int samplesize;

	/* Make sure audio has been opened */
	if ( ! audio_opened ) {
		SDL_SetError("Audio device hasn't been opened");
		if ( freesrc ) {
			SDL_RWclose(src);
		}
		return(NULL);
	}

	/* Allocate the chunk memory */
	chunk = (Mix_Chunk *)malloc(sizeof(Mix_Chunk));
	if ( chunk == NULL ) {
		SDL_SetError("Out of memory");
		if ( freesrc ) {
			SDL_RWclose(src);
		}
		return(NULL);
	}

	/* Load the WAV file into the chunk */
	if ( SDL_LoadWAV_RW(src, freesrc,
		&wavespec, (Uint8 **)&chunk->abuf, &chunk->alen) == NULL ) {
		free(chunk);
		return(NULL);
	}
#if 0
	PrintFormat("Audio device", &mixer);
	PrintFormat("-- Wave file", &wavespec);
#endif

	/* Build the audio converter and create conversion buffers */
	if ( SDL_BuildAudioCVT(&wavecvt,
			wavespec.format, wavespec.channels, wavespec.freq,
			mixer.format, mixer.channels, mixer.freq) < 0 ) {
		SDL_FreeWAV(chunk->abuf);
		free(chunk);
		return(NULL);
	}
	samplesize = ((wavespec.format & 0xFF)/8)*wavespec.channels;
	wavecvt.len = chunk->alen & ~(samplesize-1);
	wavecvt.buf = (Uint8 *)malloc(wavecvt.len*wavecvt.len_mult);
	if ( wavecvt.buf == NULL ) {
		SDL_SetError("Out of memory");
		SDL_FreeWAV(chunk->abuf);
		free(chunk);
		return(NULL);
	}
	memcpy(wavecvt.buf, chunk->abuf, chunk->alen);
	SDL_FreeWAV(chunk->abuf);

	/* Run the audio converter */
	if ( SDL_ConvertAudio(&wavecvt) < 0 ) {
		free(wavecvt.buf);
		free(chunk);
		return(NULL);
	}
	chunk->allocated = 1;
	chunk->abuf = wavecvt.buf;
	chunk->alen = wavecvt.len_cvt;
	chunk->volume = MIX_MAX_VOLUME;
	return(chunk);
}

/* Load a wave file of the mixer format from a memory buffer */
Mix_Chunk *Mix_QuickLoad_WAV(Uint8 *mem)
{
	Mix_Chunk *chunk;
	Uint8 magic[4];

	/* Make sure audio has been opened */
	if ( ! audio_opened ) {
		SDL_SetError("Audio device hasn't been opened");
		return(NULL);
	}

	/* Allocate the chunk memory */
	chunk = (Mix_Chunk *)malloc(sizeof(Mix_Chunk));
	if ( chunk == NULL ) {
		SDL_SetError("Out of memory");
		return(NULL);
	}

	/* Essentially just skip to the audio data (no error checking - fast) */
	chunk->allocated = 0;
	mem += 12; /* WAV header */
	do {
		memcpy(magic, mem, 4);
		mem += 4;
		chunk->alen = ((mem[3]<<24)|(mem[2]<<16)|(mem[1]<<8)|(mem[0]));
		mem += 4;
		chunk->abuf = mem;
		mem += chunk->alen;
	} while ( memcmp(magic, "data", 4) != 0 );
	chunk->volume = MIX_MAX_VOLUME;

	return(chunk);
}

/* Free an audio chunk previously loaded */
void Mix_FreeChunk(Mix_Chunk *chunk)
{
	int i;

	/* Caution -- if the chunk is playing, the mixer will crash */
	if ( chunk ) {
		/* Guarantee that this chunk isn't playing */
		SDL_mutexP(mixer_lock);
		for ( i=0; i<num_channels; ++i ) {
			if ( chunk == channel[i].chunk ) {
				channel[i].playing = 0;
				channel[i].looping = 0;
			}
		}
		SDL_mutexV(mixer_lock);

		/* Actually free the chunk */
		if ( chunk->allocated ) {
		    	free(chunk->abuf);
		}
		free(chunk);
	}
}

/* Set a function that is called after all mixing is performed.
   This can be used to provide real-time visual display of the audio stream
   or add a custom mixer filter for the stream data.
*/
void Mix_SetPostMix(void (*mix_func)
                    (void *udata, Uint8 *stream, int len), void *arg)
{
	SDL_LockAudio();
	mix_postmix_data = arg;
	mix_postmix = mix_func;
	SDL_UnlockAudio();
}

/* Add your own music player or mixer function.
   If 'mix_func' is NULL, the default music player is re-enabled.
 */
void Mix_HookMusic(void (*mix_func)(void *udata, Uint8 *stream, int len),
                                                                void *arg)
{
	SDL_LockAudio();
	if ( mix_func != NULL ) {
		music_data = arg;
		mix_music = mix_func;
	} else {
		music_data = NULL;
		mix_music = music_mixer;
	}
	SDL_UnlockAudio();
}

void *Mix_GetMusicHookData(void)
{
	return(music_data);
}

/* Reserve the first channels (0 -> n-1) for the application, i.e. don't allocate
   them dynamically to the next sample if requested with a -1 value below.
   Returns the number of reserved channels.
 */
int Mix_ReserveChannels(int num)
{
	if (num > num_channels)
		num = num_channels;
	reserved_channels = num;
	return num;
}

/* Play an audio chunk on a specific channel.
   If the specified channel is -1, play on the first free channel.
   'ticks' is the number of milliseconds at most to play the sample, or -1
   if there is no limit.
   Returns which channel was used to play the sound.
*/
int Mix_PlayChannelTimed(int which, Mix_Chunk *chunk, int loops, int ticks)
{
	int i;

	/* Don't play null pointers :-) */
	if ( chunk == NULL ) {
		return(-1);
	}

	/* Lock the mixer while modifying the playing channels */
	SDL_mutexP(mixer_lock);
	{
		/* If which is -1, play on the first free channel */
		if ( which == -1 ) {
			for ( i=reserved_channels; i<num_channels; ++i ) {
				if ( channel[i].playing <= 0 )
					break;
			}
			if ( i == num_channels ) {
				which = -1;
			} else {
				which = i;
			}
		}

		/* Queue up the audio data for this channel */
		if ( which >= 0 ) {
			Uint32 sdl_ticks = SDL_GetTicks();
			channel[which].samples = chunk->abuf;
			channel[which].playing = chunk->alen;
			channel[which].looping = loops;
			channel[which].chunk = chunk;
			channel[which].paused = 0;
			channel[which].fading = MIX_NO_FADING;
			channel[which].start_time = sdl_ticks;
			channel[which].expire = (ticks>0) ? (sdl_ticks + ticks) : 0;
		}
	}
	SDL_mutexV(mixer_lock);

	/* Return the channel on which the sound is being played */
	return(which);
}

/* Change the expiration delay for a channel */
int Mix_ExpireChannel(int which, int ticks)
{
	int status = 0;

	if ( which == -1 ) {
		int i;
		for ( i=0; i < num_channels; ++ i ) {
			status += Mix_ExpireChannel(i, ticks);
		}
	} else if ( which < num_channels ) {
		SDL_mutexP(mixer_lock);
		channel[which].expire = (ticks>0) ? (SDL_GetTicks() + ticks) : 0;
		SDL_mutexV(mixer_lock);
		++ status;
	}
	return(status);
}

/* Fade in a sound on a channel, over ms milliseconds */
int Mix_FadeInChannelTimed(int which, Mix_Chunk *chunk, int loops, int ms, int ticks)
{
	int i;

	/* Don't play null pointers :-) */
	if ( chunk == NULL ) {
		return(-1);
	}

	/* Lock the mixer while modifying the playing channels */
	SDL_mutexP(mixer_lock);
	{
		/* If which is -1, play on the first free channel */
		if ( which == -1 ) {
			for ( i=reserved_channels; i<num_channels; ++i ) {
				if ( channel[i].playing <= 0 )
					break;
			}
			if ( i == num_channels ) {
				which = -1;
			} else {
				which = i;
			}
		}

		/* Queue up the audio data for this channel */
		if ( which >= 0 ) {
			Uint32 sdl_ticks = SDL_GetTicks();
			channel[which].samples = chunk->abuf;
			channel[which].playing = chunk->alen;
			channel[which].looping = loops;
			channel[which].chunk = chunk;
			channel[which].paused = 0;
			channel[which].fading = MIX_FADING_IN;
			channel[which].fade_volume = channel[which].volume;
			channel[which].volume = 0;
			channel[which].fade_length = (Uint32)ms;
			channel[which].start_time = channel[which].ticks_fade = sdl_ticks;
			channel[which].expire = (ticks > 0) ? (sdl_ticks+ticks) : 0;
		}
	}
	SDL_mutexV(mixer_lock);

	/* Return the channel on which the sound is being played */
	return(which);
}

/* Set volume of a particular channel */
int Mix_Volume(int which, int volume)
{
	int i;
	int prev_volume;

	if ( which == -1 ) {
		prev_volume = 0;
		for ( i=0; i<num_channels; ++i ) {
			prev_volume += Mix_Volume(i, volume);
		}
		prev_volume /= num_channels;
	} else {
		prev_volume = channel[which].volume;
		if ( volume < 0 ) {
			volume = 0;
		}
		if ( volume > SDL_MIX_MAXVOLUME ) {
			volume = SDL_MIX_MAXVOLUME;
		}
		channel[which].volume = volume;
	}
	return(prev_volume);
}
/* Set volume of a particular chunk */
int Mix_VolumeChunk(Mix_Chunk *chunk, int volume)
{
	int prev_volume;

	prev_volume = chunk->volume;
	if ( volume >= 0 ) {
		if ( volume > MIX_MAX_VOLUME ) {
			volume = MIX_MAX_VOLUME;
		}
		chunk->volume = volume;
	}
	return(prev_volume);
}

/* Halt playing of a particular channel */
int Mix_HaltChannel(int which)
{
	int i;

	if ( which == -1 ) {
		for ( i=0; i<num_channels; ++i ) {
			Mix_HaltChannel(i);
		}
	} else {
		SDL_mutexP(mixer_lock);
		channel[which].playing = 0;
		channel[which].looping = 0;
		channel[which].expire = 0;
		if(channel[which].fading != MIX_NO_FADING) /* Restore volume */
			channel[which].volume = channel[which].fade_volume;
		channel[which].fading = MIX_NO_FADING;
		SDL_mutexV(mixer_lock);
	}
	return(0);
}

/* Halt playing of a particular group of channels */
int Mix_HaltGroup(int tag)
{
	int i;

	for ( i=0; i<num_channels; ++i ) {
		if( channel[i].tag == tag ) {
			Mix_HaltChannel(i);
		}
	}
	return(0);
}

/* Fade out a channel and then stop it automatically */
int Mix_FadeOutChannel(int which, int ms)
{
	int status;

	status = 0;
	if ( which == -1 ) {
		int i;

		for ( i=0; i<num_channels; ++i ) {
			status += Mix_FadeOutChannel(i,ms);
		}
	} else {
		SDL_mutexP(mixer_lock);
		if ( channel[which].playing && channel[which].volume>0 &&
			 channel[which].fading==MIX_NO_FADING ) {

			channel[which].fading = MIX_FADING_OUT;
			channel[which].fade_volume = channel[which].volume;
			channel[which].fade_length = ms;
			channel[which].ticks_fade = SDL_GetTicks();
			++ status;
		}
		SDL_mutexV(mixer_lock);
	}
	return(status);
}

/* Halt playing of a particular group of channels */
int Mix_FadeOutGroup(int tag, int ms)
{
	int i;
	int status = 0;
	for ( i=0; i<num_channels; ++i ) {
		if( channel[i].tag == tag ) {
			status += Mix_FadeOutChannel(i,ms);
		}
	}
	return(status);
}

Mix_Fading Mix_FadingChannel(int which)
{
	return channel[which].fading;
}

/* Check the status of a specific channel.
   If the specified channel is -1, check all channels.
*/
int Mix_Playing(int which)
{
	int status;

	status = 0;
	if ( which == -1 ) {
		int i;

		for ( i=0; i<num_channels; ++i ) {
			if ( channel[i].playing > 0 ) {
				++status;
			}
		}
	} else {
		if ( channel[which].playing > 0 ) {
			++status;
		}
	}
	return(status);
}

/* Close the mixer, halting all playing audio */
void Mix_CloseAudio(void)
{
	if ( audio_opened ) {
		if ( audio_opened == 1 ) {
			close_music();
			Mix_HaltChannel(-1);
			SDL_CloseAudio();
			SDL_DestroyMutex(mixer_lock);
			free(channel);
			channel = NULL;
		}
		--audio_opened;
	}
}

/* Pause a particular channel (or all) */
void Mix_Pause(int which)
{
	Uint32 sdl_ticks = SDL_GetTicks();
	if ( which == -1 ) {
		int i;

		for ( i=0; i<num_channels; ++i ) {
			if ( channel[i].playing > 0 ) {
				channel[i].paused = sdl_ticks;
			}
		}
	} else {
		if ( channel[which].playing > 0 ) {
			channel[which].paused = sdl_ticks;
		}
	}
}

/* Resume a paused channel */
void Mix_Resume(int which)
{
	Uint32 sdl_ticks = SDL_GetTicks();
	if ( which == -1 ) {
		int i;

		SDL_mutexP(mixer_lock);
		for ( i=0; i<num_channels; ++i ) {
			if ( channel[i].playing > 0 ) {
				if(channel[i].expire > 0)
					channel[i].expire += sdl_ticks - channel[i].paused;
				channel[i].paused = 0;
			}
		}
		SDL_mutexV(mixer_lock);
	} else {
		SDL_mutexP(mixer_lock);
		if ( channel[which].playing > 0 ) {
			if(channel[which].expire > 0)
				channel[which].expire += sdl_ticks - channel[which].paused;
			channel[which].paused = 0;
		}
		SDL_mutexV(mixer_lock);
	}
}

int Mix_Paused(int which)
{
	if ( which > num_channels )
		return(0);
	if ( which < 0 ) {
		int status = 0;
		int i;
		for( i=0; i < num_channels; ++i ) {
			if ( channel[i].paused ) {
				++ status;
			}
		}
		return(status);
	} else {
		return(channel[which].paused != 0);
	}
}

/* Change the group of a channel */
int Mix_GroupChannel(int which, int tag)
{
	if ( which < 0 || which > num_channels )
		return(0);

	SDL_mutexP(mixer_lock);
	channel[which].tag = tag;
	SDL_mutexV(mixer_lock);
	return(1);
}

/* Assign several consecutive channels to a group */
int Mix_GroupChannels(int from, int to, int tag)
{
	int status = 0;
	for( ; from <= to; ++ from ) {
		status += Mix_GroupChannel(from, tag);
	}
	return(status);
}

/* Finds the first available channel in a group of channels */
int Mix_GroupAvailable(int tag)
{
	int i;
	for( i=0; i < num_channels; i ++ ) {
		if ( (channel[i].tag==tag || tag==-1) && channel[i].playing<=0 )
			return i;
	}
	return(-1);
}

int Mix_GroupCount(int tag)
{
	int count = 0;
	int i;
	for( i=0; i < num_channels; i ++ ) {
		if ( channel[i].tag==tag || tag==-1 )
			++ count;
	}
	return(count);
}

/* Finds the "oldest" sample playing in a group of channels */
int Mix_GroupOldest(int tag)
{
	int chan = -1;
	Uint32 mintime = SDL_GetTicks();
	int i;
	for( i=0; i < num_channels; i ++ ) {
		if ( (channel[i].tag==tag || tag==-1) && channel[i].playing > 0
			 && channel[i].start_time <= mintime ) {
			mintime = channel[i].start_time;
			chan = i;
		}
	}
	return(chan);
}

/* Finds the "most recent" (i.e. last) sample playing in a group of channels */
int Mix_GroupNewer(int tag)
{
	int chan = -1;
	Uint32 maxtime = 0;
	int i;
	for( i=0; i < num_channels; i ++ ) {
		if ( (channel[i].tag==tag || tag==-1) && channel[i].playing > 0
			 && channel[i].start_time >= maxtime ) {
			maxtime = channel[i].start_time;
			chan = i;
		}
	}
	return(chan);
}

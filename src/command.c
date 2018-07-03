/*
    pysol-sound-server
    Copyright (C) 1999-2004  Markus F.X.J. Oberhumer

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Markus F.X.J. Oberhumer
    <markus@oberhumer.com>
    http://www.oberhumer.com/pysol

    Sam Lantinga
    5635-34 Springhouse Dr.
    Pleasanton, CA 94588 (USA)
    slouken@devolution.com
*/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <mmsystem.h>
#else
#include <unistd.h>
#endif

#include "SDL.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "SDL_mixer.h"
#include "server.h"


/***********************************************************************
// this file is a quick hack - don't look ;-)
************************************************************************/

FILE *err = NULL;
int debug = 0;
int protocol = -1;
int audio_open = 0;

SDL_mutex *queue_lock = NULL;


/***********************************************************************
//
************************************************************************/

#define WAV_CHANNEL     0

#define STRSTART(a,b,c) \
    (strncmp(a,c,sizeof(c)-1) == 0 ? (*(b) = (a)+sizeof(c)-1, 1) : 0)


/* current sample */
static struct sample_t {
    Mix_Chunk *data;
    int id;
    int priority;
    int loop;
    char *filename;
} sample;

/* current music */
static struct music_t {
    Mix_Music *data;
    int id;
    int priority;
    int loop;
    char *filename;
} music;

/* current settings */
static struct settings_t {
    int sample_volume;
    int music_volume;
} settings = {
    MIX_MAX_VOLUME,
    MIX_MAX_VOLUME/2
};


/***********************************************************************
// music queue for background music
************************************************************************/

#define Q_SIZE          1024
static char *q_cmd[Q_SIZE];
static int q_head = 0;
static int q_tail= 0;


static void music_add_queue(const char *c)
{
    char *cmd = NULL;

    if (!audio_open || !c)
        return;
    cmd = strdup(c);
    if (!cmd)
        return;

    SDL_mutexP(queue_lock);
    q_cmd[q_head] = cmd;
    cmd = NULL;
    if (++q_head == Q_SIZE)
        q_head = 0;
    if (q_tail == q_head)               /* queue overflow - remove tail */
    {
        cmd = q_cmd[q_tail];
        q_cmd[q_tail] = NULL;
        if (++q_tail == Q_SIZE)
            q_tail = 0;
    }
    SDL_mutexV(queue_lock);
    if (cmd)
    {
        free(cmd);
    }
}


/* called from music.c when a music has finished */
void music_handle_queue(void)
{
    char *cmd = NULL;

    if (!audio_open)
        return;
    if (debug >= 3 && err)
        fprintf(err,"music_handle_queue %d %d: %s\n", q_head, q_tail, q_cmd[q_tail]);
    SDL_mutexP(queue_lock);
    if (q_tail != q_head)
    {
        cmd = q_cmd[q_tail];
        q_cmd[q_tail] = NULL;
        if (++q_tail == Q_SIZE)
            q_tail = 0;
    }
    SDL_mutexV(queue_lock);
    if (cmd)
    {
        handle_command(cmd);
        free(cmd);
    }
    else if (music.data)
        handle_command("stopmus");
}


static void music_clear_queue(void)
{
    SDL_mutexP(queue_lock);
    while (q_tail != q_head)
    {
        char *cmd = q_cmd[q_tail];
        free(cmd);
        q_cmd[q_tail] = NULL;
        if (++q_tail == Q_SIZE)
            q_tail = 0;
    }
    SDL_mutexV(queue_lock);
}


/***********************************************************************
//
************************************************************************/

void CleanUp(void)
{
    static int cleanup_done = 0;
                if (debug >= 9 && err) fprintf(err, "CleanUp 1\n");
    if (cleanup_done)
        return;
    cleanup_done = 1;

    audio_open = 0;

                if (debug >= 9 && err) fprintf(err, "CleanUp 2\n");
    Mix_HookMusicFinished(NULL);
                if (debug >= 9 && err) fprintf(err, "CleanUp 3\n");
    music_clear_queue();
                if (debug >= 9 && err) fprintf(err, "CleanUp 4\n");
    Mix_ResumeMusic();
    Mix_HaltMusic();
    Mix_FreeMusic(music.data);
    music.data = NULL;
    music.id = -1;
    if (music.filename)
        free(music.filename);
    music.filename = NULL;
                if (debug >= 9 && err) fprintf(err, "CleanUp 5\n");
    Mix_FreeChunk(sample.data);
    sample.data = NULL;
    sample.id = -1;
    if (sample.filename)
        free(sample.filename);
    sample.filename = NULL;
                if (debug >= 9 && err) fprintf(err, "CleanUp 6\n");
    Mix_CloseAudio();
                if (debug >= 9 && err) fprintf(err, "CleanUp 7\n");
    SDL_DestroyMutex(queue_lock);
    queue_lock = NULL;
                if (debug >= 9 && err) fprintf(err, "CleanUp 8\n");
    SDL_Quit();
                if (debug >= 9 && err) fprintf(err, "CleanUp 9\n");
}


/***********************************************************************
// util
************************************************************************/

static int parse_song(const char *b,
                      char *filename, int *id, int *priority, int *loop, int *volume)
{
    const char *f;
    size_t len;
    char delim;

    filename[0] = '\0';
    *id = -1;

    if (!b)
        return 0;
    while (*b == ' ')
        b++;
    if (!*b)
        return 0;

    /* note: this doesn't handle spaces in filename */
    if (protocol == 0)
        return 1 + sscanf(b, "%s %d %d %d", filename, priority, loop, volume);

    /* get 'filename' */
    delim = *b++;
    if (delim != '\'' && delim != '"')
        return 0;
    for (f = b; *b && *b != delim; )
        b++;
    if (*b != delim)
        return 0;
    len = b - f;
    if (len == 0 || len >= 200)
        return 0;
    memcpy(filename, f, len);
    filename[len] = 0;
    /* parse the rest */
    if (b[1] != ' ')
        return 1;
    for (b += 2; *b == ' '; )
        b++;
    if (protocol <= 3)
        return 2 + sscanf(b, "%d %d %d", priority, loop, volume);
    else
        return 1 + sscanf(b, "%d %d %d %d", id, priority, loop, volume);
}


/***********************************************************************
// protocol versions 0 - 6
************************************************************************/

static int handle_command_0_6(const char *b)
{
    char cmd[512+1];
    int i = 0;
    char name[256];
    int id = -1;
    int priority = 0;
    int loop = 0;
    int volume = 0;
    Mix_Chunk *new_sample = NULL;
    Mix_Chunk *prev_sample = NULL;
    Mix_Music *new_music = NULL;
    const char *bb = NULL;
    int sn = 0;
    int in_handle_queue = 0;

    if (STRSTART(b, &bb, "playwav "))
    {
        sn = parse_song(bb, name, &id, &priority, &loop, &volume) - 5;
        if (sn)
            goto error;
        if (volume < 0)
            volume = settings.sample_volume;

        new_sample = Mix_LoadWAV(name);
        if (!new_sample)
        {
            if (err)
                fprintf(err, "WAV load error %s: %s\n", name, SDL_GetError());
            return -1;
        }
        if (priority <= sample.priority && Mix_Playing(WAV_CHANNEL))
        {
            if (new_sample->allocated)
                free(new_sample->abuf);
            free(new_sample);
            return 0;
        }
        prev_sample = sample.data;
        sample.data = new_sample;
        sample.id = id;
        sample.priority = priority;
        sample.loop = loop;
        if (sample.filename)
            free(sample.filename);
        sample.filename = strdup(name);
        Mix_Volume(WAV_CHANNEL, volume);
        if (protocol >= 6)
            Mix_PlayChannel(WAV_CHANNEL, sample.data, loop);
        else
            Mix_PlayChannel(WAV_CHANNEL, sample.data, loop ? -1 : 0);
        if (prev_sample)
        {
            if (prev_sample->allocated)
                free(prev_sample->abuf);
            free(prev_sample);
        }
        /* if (err) fprintf(err, "%s %d %d\n", name, p, loop); */
    }
    else if (STRSTART(b, &bb, "setwavvol "))
    {
        if (sscanf(bb, "%d", &volume) != 1)
            goto error;
        if (volume >= 0 && volume <= MIX_MAX_VOLUME)
        {
            settings.sample_volume = volume;
            Mix_Volume(WAV_CHANNEL, volume);
        }
    }
    else if (STRSTART(b, &bb, "stopwav"))
    {
stopwav:
        Mix_FreeChunk(sample.data);
        sample.data = NULL;
        sample.id = -1;
        sample.priority = -1;
        sample.loop = 0;
        if (sample.filename)
            free(sample.filename);
        sample.filename = NULL;
    }
    else if (STRSTART(b, &bb, "stopwavloop"))
    {
        if (sample.loop)
            goto stopwav;
    }
    else if (STRSTART(b, &bb, "playmus "))
    {
        sn = parse_song(bb, name, &id, &priority, &loop, &volume) - 5;
        if (sn)
            goto error;
playmus:
        if (volume < 0)
            volume = settings.music_volume;
        new_music = Mix_LoadMUS(name);
        if (!new_music)
        {
            if (err)
                fprintf(err, "Music load error %s: %s\n", name, SDL_GetError());
            if (in_handle_queue)
                music_handle_queue();       /* get next music */
            return -1;
        }
        if (in_handle_queue)
        {
            if (loop)
                music_add_queue(b);     /* re-add the __playqueuemus command */
            loop = 0;
        }
        if (priority <= music.priority && Mix_PlayingMusic())
        {
            Mix_FreeMusic(new_music);
            return 0;
        }
        Mix_ResumeMusic();
        Mix_HaltMusic();
        Mix_FreeMusic(music.data);
        if (music.filename)
            free(music.filename);
        music.filename = strdup(name);
        music.data = new_music;
        music.id = id;
        music.priority = priority;
        music.loop = loop;
        if (volume >= 0 && volume <= MIX_MAX_VOLUME)
        {
            if (debug >= 1 && err)
                fprintf(err, "Playing music '%s'\n", name);
            Mix_PlayMusic(music.data, loop ? -1 : 0);
            if (volume == 0)
                Mix_PauseMusic();
            Mix_VolumeMusic(volume);
        }
        /* if (err) fprintf(err, "playmus: %s %d %d %d\n", name, priority, loop, volume); */
    }
    else if (STRSTART(b, &bb, "setmusvol "))
    {
        if (sscanf(bb, "%d", &volume) != 1)
            goto error;
        if (volume >= 0 && volume <= MIX_MAX_VOLUME)
        {
            settings.music_volume = volume;
            if (volume == 0)            /* pause */
            {
                Mix_PauseMusic();
                Mix_VolumeMusic(volume);
            }
            else                        /* resume */
            {
                Mix_VolumeMusic(volume);
                Mix_ResumeMusic();
                if (!Mix_PlayingMusic())
                    music_handle_queue();
            }
        }
    }
    else if (STRSTART(b, &bb, "stopmus"))
    {
stopmus:
        Mix_FreeMusic(music.data);
        music.data = NULL;
        music.id = -1;
        music.priority = -1;
        music.loop = 0;
        if (music.filename)
            free(music.filename);
        music.filename = NULL;
    }
    else if (STRSTART(b, &bb, "stopqueue"))
    {
        music_clear_queue();
        goto stopmus;
    }
    else if (STRSTART(b, &bb, "startqueue"))
    {
        if (!Mix_PlayingMusic())
            music_handle_queue();
    }
    else if (STRSTART(b, &bb, "queuemus "))
    {
        sn = parse_song(bb, name, &id, &priority, &loop, &volume) - 5;
        if (sn)
            goto error;
        sprintf(cmd, "__playqueuemus %s", bb);
        music_add_queue(cmd);
    }
    else if (STRSTART(b, &bb, "__playqueuemus "))
    {
        /* internal - called from music_handle_queue when music has finished */
        sn = parse_song(bb, name, &id, &priority, &loop, &volume) - 5;
        if (sn)
            goto error;
        if (protocol < 5)
            loop = 1;
        if (!Mix_PlayingMusic())
        {
            in_handle_queue = 1;
            goto playmus;                       /* and play the music */
        }
    }
    else if (protocol >= 3 && STRSTART(b, &bb, "nextmus"))
    {
        Mix_FreeMusic(music.data);
        music.data = NULL;
        music.id = -1;
        music.priority = -1;
        music.loop = 0;
        if (music.filename)
            free(music.filename);
        music.filename = NULL;
        music_handle_queue();
    }
    else if (protocol >= 2 && STRSTART(b, &bb, "debug "))
    {
        if (sscanf(bb, "%d", &i) != 1)
            goto error;
        if (i >= 0 && i <= 9 && i != debug)
        {
            debug = i;
            if (i || debug)
            {
                err = stderr;
                fprintf(err, "info: debug level set to %d\n", debug);
            }
            err = debug ? stderr : (FILE *) NULL;
        }
    }
    else
    {
        if (err)
            fprintf(err, "unknown command '%s'\n", b);
        return -1;
    }
    return 0;

error:
    if (err)
        fprintf(err, "parse error %d: '%s'\n", sn, b);
    return -1;
}


/***********************************************************************
// handle a command
************************************************************************/

int handle_command(const char *b)
{
    const char *bb = NULL;

    if (!b || !b[0])
        return 0;
    if (strlen(b) > 255)
        return -2;

    if (STRSTART(b, &bb, "exit"))
    {
        CleanUp();
        return 0;
    }

    if (STRSTART(b, &bb, "protocol "))
    {
        int p = -1;
        if (sscanf(bb, "%d", &p) != 1 || p < 0)
        {
            if (err)
                fprintf(err, "syntax error: %s\n", b);
            return -1;
        }
        if (p > PROTOCOL_VERSION)
        {
            if (err)
                fprintf(err, "Unsupported protocol version %d.\n", p);
            return -1;
        }
        if (protocol < 0)
            protocol = p;
        else if (p != protocol)
        {
            if (err)
                fprintf(err, "Invalid protocol redefinition %d.\n", p);
            return -1;
        }
        return 0;
    }

    if (protocol >= 0 && protocol <= 6)
        return handle_command_0_6(b);

    if (protocol < 0)
    {
        if (err)
            fprintf(err, "No protocol version yet -- command ignored.\n");
    }
    else
    {
        if (err)
            fprintf(err, "Unknown protocol version %d.\n", protocol);
    }
    return -1;
}


/***********************************************************************
//
************************************************************************/

int get_music_info(void)
{
    if (!music.data)
        return -2;
#if 0
    if (!Mix_PlayingMusic())
        return -3;
#endif
    return music.id;
}


/*
vi:ts=4:et
*/

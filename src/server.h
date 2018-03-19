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
*/


#define PACKAGE                 "pysolsoundserver"
#define PROTOCOL_VERSION        6
#include "version.h"

#if !defined(WAV_MUSIC)
#define WAV_MUSIC
#endif
#if !defined(MOD_MUSIC)
#define MOD_MUSIC
#endif
#if !defined(MP3_MUSIC)
#if !defined(_WIN32) && !defined(__CHECKER__)
#define MP3_MUSIC
#endif
#endif

#define err server_err
extern FILE *err;
extern int debug;
extern int protocol;
extern int audio_open;

extern SDL_mutex *queue_lock;

void CleanUp(void);
int handle_command(const char *cmd);
void music_handle_queue(void);
int get_music_info(void);

int open_music(SDL_AudioSpec *mixer);
void close_music(void);

#include "SDL_version.h"
#if (SDL_VERSIONNUM(SDL_MAJOR_VERSION,SDL_MINOR_VERSION,SDL_PATCHLEVEL) < 1004)
#  error "need SDL 1.0.4 or better"
#endif

#include "conf.h"

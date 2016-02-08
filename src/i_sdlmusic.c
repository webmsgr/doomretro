/*
========================================================================

                           D O O M  R e t r o
         The classic, refined DOOM source port. For Windows PC.

========================================================================

  Copyright © 1993-2012 id Software LLC, a ZeniMax Media company.
  Copyright © 2013-2016 Brad Harding.

  DOOM Retro is a fork of Chocolate DOOM.
  For a list of credits, see the accompanying AUTHORS file.

  This file is part of DOOM Retro.

  DOOM Retro is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  DOOM Retro is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with DOOM Retro. If not, see <http://www.gnu.org/licenses/>.

  DOOM is a registered trademark of id Software LLC, a ZeniMax Media
  company, in the US and/or other countries and is used without
  permission. All other trademarks are the property of their respective
  holders. DOOM Retro is in no way affiliated with nor endorsed by
  id Software.

========================================================================
*/

#include "c_console.h"
#include "i_system.h"
#include "m_config.h"
#include "m_misc.h"
#include "mus2mid.h"
#include "SDL.h"
#include "SDL_mixer.h"
#include "s_sound.h"
#include "version.h"
#include "z_zone.h"

#define CHANNELS        2
#define SAMPLECOUNT     512

static dboolean         music_initialized;

// If this is true, this module initialized SDL sound and has the
// responsibility to shut it down
static dboolean         sdl_was_initialized;

static dboolean         musicpaused;
static int              current_music_volume;

static char             *tempmusicfilename;

char                    *s_timiditycfgpath = s_timiditycfgpath_default;

static char             *temp_timidity_cfg;

// If the temp_timidity_cfg config variable is set, generate a "wrapper"
// config file for Timidity to point to the actual config file. This
// is needed to inject a "dir" command so that the patches are read
// relative to the actual config file.
static dboolean WriteWrapperTimidityConfig(char *write_path)
{
    char        *p;
    FILE        *fstream;

    if (!strcmp(s_timiditycfgpath, ""))
        return false;

    fstream = fopen(write_path, "w");

    if (!fstream)
        return false;

    p = strrchr(s_timiditycfgpath, DIR_SEPARATOR);
    if (p)
    {
        char    *path = strdup(s_timiditycfgpath);

        path[p - s_timiditycfgpath] = '\0';
        fprintf(fstream, "dir %s\n", path);
        free(path);
    }

    fprintf(fstream, "source %s\n", s_timiditycfgpath);
    fclose(fstream);

    return true;
}

void I_InitTimidityConfig(void)
{
    dboolean    success;

    temp_timidity_cfg = M_TempFile("timidity.cfg");

    success = WriteWrapperTimidityConfig(temp_timidity_cfg);

    // Set the TIMIDITY_CFG environment variable to point to the temporary
    // config file.
    if (success)
    {
        char    *env_string = M_StringJoin("TIMIDITY_CFG=", temp_timidity_cfg, NULL);

        putenv(env_string);
    }
    else
    {
        free(temp_timidity_cfg);
        temp_timidity_cfg = NULL;
    }
}

void CheckTimidityConfig(void)
{
    if (*s_timiditycfgpath)
        if (M_FileExists(s_timiditycfgpath))
            C_Output("Using TiMidity configuration file %s.", uppercase(s_timiditycfgpath));
        else
            C_Warning("Can't find TiMidity configuration file %s.", uppercase(s_timiditycfgpath));
}

// Remove the temporary config file generated by I_InitTimidityConfig().
static void RemoveTimidityConfig(void)
{
    if (temp_timidity_cfg)
    {
        remove(temp_timidity_cfg);
        free(temp_timidity_cfg);
    }
}

// Shutdown music
void I_SDL_ShutdownMusic(void)
{
    if (music_initialized)
    {
        Mix_HaltMusic();
        music_initialized = false;

        free(tempmusicfilename);

        if (sdl_was_initialized)
        {
            Mix_CloseAudio();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdl_was_initialized = false;
        }
    }
}

static dboolean SDLIsInitialized(void)
{
    int         freq, channels;
    Uint16      format;

    return ((dboolean)Mix_QuerySpec(&freq, &format, &channels));
}

// Initialize music subsystem
dboolean I_SDL_InitMusic(void)
{
    // If SDL_mixer is not initialized, we have to initialize it
    // and have the responsibility to shut it down later on.
    if (!SDLIsInitialized())
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
            I_Error("Unable to set up sound: %s", SDL_GetError());
        else if (Mix_OpenAudio(snd_samplerate, MIX_DEFAULT_FORMAT, CHANNELS,
            SAMPLECOUNT * snd_samplerate / 11025) < 0)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            I_Error("Error initializing SDL_mixer: %s", Mix_GetError());
        }

    SDL_PauseAudio(0);

    tempmusicfilename = M_TempFile(PACKAGE".mid");

    sdl_was_initialized = true;
    music_initialized = true;

    // Once initialization is complete, the temporary Timidity config
    // file can be removed.
    RemoveTimidityConfig();

    return music_initialized;
}

//
// SDL_mixer's native MIDI music playing does not pause properly.
// As a workaround, set the volume to 0 when paused.
//
static void UpdateMusicVolume(void)
{
    Mix_VolumeMusic((current_music_volume * MIX_MAX_VOLUME) / 127 * !musicpaused);
}

// Set music volume (0 - 127)
void I_SDL_SetMusicVolume(int volume)
{
    // Internal state variable.
    current_music_volume = volume;

    UpdateMusicVolume();
}

// Start playing a mid
void I_SDL_PlaySong(void *handle, int looping)
{
    if (!music_initialized || !handle)
        return;

    Mix_PlayMusic((Mix_Music *)handle, looping ? -1 : 1);
}

void I_SDL_PauseSong(void)
{
    if (!music_initialized)
        return;

    musicpaused = true;

    UpdateMusicVolume();
}

void I_SDL_ResumeSong(void)
{
    if (!music_initialized)
        return;

    musicpaused = false;

    UpdateMusicVolume();
}

void I_SDL_StopSong(void)
{
    if (!music_initialized)
        return;

    Mix_HaltMusic();
}

void I_SDL_UnRegisterSong(void *handle)
{
    if (!music_initialized || !handle)
        return;

    Mix_FreeMusic(handle);
}

static dboolean ConvertMus(byte *musdata, int len, char *filename)
{
    MEMFILE     *instream = mem_fopen_read(musdata, len);
    MEMFILE     *outstream = mem_fopen_write();
    void        *outbuf;
    int         result = mus2mid(instream, outstream);

    if (!result)
    {
        size_t  outbuf_len;

        mem_get_buf(outstream, &outbuf, &outbuf_len);
        M_WriteFile(filename, outbuf, outbuf_len);
    }

    mem_fclose(instream);
    mem_fclose(outstream);

    return result;
}

void *I_SDL_RegisterSong(void *data, int len)
{
    Mix_Music   *music = NULL;

    if (music_initialized)
        if (!memcmp(data, "MUS", 3))
        {
            ConvertMus(data, len, tempmusicfilename);
            music = Mix_LoadMUS(tempmusicfilename);
            remove(tempmusicfilename);
        }
        else
        {
            SDL_RWops   *rwops = SDL_RWFromMem(data, len);

            if (rwops)
                music = Mix_LoadMUS_RW(rwops, SDL_TRUE);
        }

    return music;
}

// Is the song playing?
dboolean I_SDL_MusicIsPlaying(void)
{
    if (!music_initialized)
        return false;

    return Mix_PlayingMusic();
}

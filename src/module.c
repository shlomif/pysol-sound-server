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


#include <Python.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#if PY_MAJOR_VERSION >= 3
#define PyString_FromString(s) PyBytes_FromString(s)
#define PyInt_FromLong(l) PyLong_FromLong(l)
#endif

#include "SDL.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"

#include "SDL_mixer.h"
#include "server.h"


/***********************************************************************
// dummy SDL video stubs for SMPEG when using `--disable-video'
************************************************************************/

#if defined(STATIC_SDL) && defined(DISABLE_VIDEO)
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
}
int SDL_LockSurface(SDL_Surface *surface) { return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) {}
#  if defined(SDL_YV12_OVERLAY)
SDL_Overlay *SDL_CreateYUVOverlay(int width, int height, Uint32 format, SDL_Surface *display)
{
    return NULL;
}
int SDL_LockYUVOverlay(SDL_Overlay *overlay) { return 0; }
void SDL_UnlockYUVOverlay(SDL_Overlay *overlay) {}
int SDL_DisplayYUVOverlay(SDL_Overlay *overlay, SDL_Rect *dstrect) { return 0; }
void SDL_FreeYUVOverlay(SDL_Overlay *overlay) {}
#  endif /* SDL_YV12_OVERLAY */
#endif


/***********************************************************************
//
************************************************************************/

static PyObject *error;


static PyObject *
do_init(PyObject *self, PyObject *args)
{
    Uint32 audio_rate = 22050;
    Uint16 audio_format = AUDIO_S16;
    int audio_channels = 2;
    int audio_buffers = audio_rate > 22050 ? 2048 : 1024;
    SDL_AudioSpec audio_mixer;
    char audio_name[256];
    PyObject *r = NULL;

    /* Initialize the SDL library */
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE) < 0)
    {
        PyErr_Format(error, "unable to initialize SDL: %s", SDL_GetError());
        return NULL;
    }
    /* Create the mutex */
    queue_lock = SDL_CreateMutex();
    if (queue_lock == NULL)
    {
        PyErr_Format(error, "unable to create queue mutex: %s", SDL_GetError());
        return NULL;
    }
    /* Open the audio device */
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) < 0)
    {
        PyErr_Format(error, "unable to open audio: %s", SDL_GetError());
        return NULL;
    }
    /* Success */
    audio_open = 1;
    Mix_HookMusicFinished(music_handle_queue);

    /* return some info */
    r = PyTuple_New(5);
    if (r != NULL)
    {
        PyTuple_SET_ITEM(r, 0, PyString_FromString("name"));
        PyTuple_SET_ITEM(r, 1, PyInt_FromLong(0));
        PyTuple_SET_ITEM(r, 2, PyString_FromString((0) ? "signed" : "unsigned"));
        PyTuple_SET_ITEM(r, 3, PyInt_FromLong(2));
        PyTuple_SET_ITEM(r, 4, PyInt_FromLong(44100));
    }
    else
    {
        r = Py_None;
        Py_INCREF(r);
    }
    return r;
}


static PyObject *
do_exit(PyObject *self, PyObject *args)
{
    CleanUp();
    return PyInt_FromLong(0);
}


static PyObject *
do_cmd(PyObject *self, PyObject *args)
{
    char buf[255+1];
    char *cmd;
    int r;
    int len;

    self = self;
    if (!PyArg_ParseTuple(args, "s#", &cmd, &len))
        return NULL;
    if (len < 0 || len > 255)
    {
        PyErr_Format(error, "command too long");
        return NULL;
    }
    memcpy(buf, cmd, len);
    buf[len] = 0;
    if (debug >= 2 && err)
        fprintf(err, "received command '%s'\n", buf);
    r = handle_command(buf);
    if (debug >= 2 && err)
        fprintf(err, "handled %d '%s'\n", r, buf);
    return PyInt_FromLong(r);
}


static PyObject *
do_getMusicInfo(PyObject *self)
{
    int id;
    id = get_music_info();
    return PyInt_FromLong(id);
}


static /* const */ PyMethodDef methods[] =
{
    {"init",            (PyCFunction)do_init,           1, NULL},
    {"exit",            (PyCFunction)do_exit,           1, NULL},
    {"cmd",             (PyCFunction)do_cmd,            1, NULL},
    {"getMusicInfo",    (PyCFunction)do_getMusicInfo,   1, NULL},
    {NULL, NULL, 0, NULL}
};


/***********************************************************************
//
************************************************************************/

#if defined(PyMODINIT_FUNC)
PyMODINIT_FUNC
#else
#if defined(__cplusplus)
extern "C"
#endif
DL_EXPORT(void)
#endif
#if PY_MAJOR_VERSION >= 3
PyInit_pysolsoundserver(void)
#else
initpysolsoundserver(void)
#endif
{
    PyObject *m, *d, *v;

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef soundservermod = {
    PyModuleDef_HEAD_INIT,
    "pysolsoundserver",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    methods
};
    m =PyModule_Create(&soundservermod);
#else
    m = Py_InitModule4("pysolsoundserver", methods, NULL,
                       NULL, PYTHON_API_VERSION);
#endif

    d = PyModule_GetDict(m);

    error = PyErr_NewException("pysolsoundserver.error", NULL, NULL);
    PyDict_SetItemString(d, "error", error);

    v = PyString_FromString("Markus F.X.J. Oberhumer <markus@oberhumer.com>");
    PyDict_SetItemString(d, "__author__", v);
    Py_DECREF(v);
    v = PyString_FromString(VERSION);
    PyDict_SetItemString(d, "__version__", v);
    Py_DECREF(v);
    v = PyString_FromString(VERSION_DATE);
    PyDict_SetItemString(d, "__version_date__", v);
    Py_DECREF(v);
    v = PyString_FromString(__DATE__);
    PyDict_SetItemString(d, "__date__", v);
    Py_DECREF(v);
    v = PyString_FromString(__TIME__);
    PyDict_SetItemString(d, "__time__", v);
    Py_DECREF(v);

    err = debug ? stderr : (FILE *) 0;
#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}


/*
vi:ts=4:et
*/

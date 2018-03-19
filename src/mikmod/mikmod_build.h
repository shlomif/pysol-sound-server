#if defined(WIN32) && !defined(_WIN32)
#  define _WIN32 1
#endif
#if defined(_WIN32) && !defined(WIN32)
#  define WIN32 1
#endif

#if defined(WIN32) && !defined(__STDC__)
#  define __STDC__ 1
#endif


#include "mikmod.h"
#include "conf.h"


#if defined(WIN32) && defined(_MSC_VER)
#  pragma warning(disable: 4018 4244 4761)
#endif

/* SDL_mixer: we don't want any mikmod thread handling */
#undef HAVE_PTHREAD

/* SDL_mixer additions: don't pollute the global namespace */
#define npertab		SDL_mixer_mikmod_npertab
#define pf		SDL_mixer_mikmod_pf
#define of		SDL_mixer_mikmod_of

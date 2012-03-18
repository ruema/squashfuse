#define CHANGE_XOPEN_SOURCE 1
#define CHANGE_DARWIN_C_SOURCE 2
#define CHANGE_BSD_SOURCE 3
#define CHANGE_GNU_SOURCE 4
#define CHANGE_POSIX_C_SOURCE 5

#if SQFEATURE == CHANGE_XOPEN_SOURCE
	#define _XOPEN_SOURCE 500
#elif SQFEATURE == CHANGE_DARWIN_C_SOURCE
	#define _DARWIN_C_SOURCE
#elif SQFEATURE == CHANGE_BSD_SOURCE
	#define _BSD_SOURCE
#elif SQFEATURE == CHANGE_GNU_SOURCE
	#define _GNU_SOURCE
#elif SQFEATURE == CHANGE_POSIX_C_SOURCE
	#undef _POSIX_C_SOURCE
#endif

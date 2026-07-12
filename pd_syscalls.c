/* Newlib syscall stubs for the device build. The core uses printf-family
 * formatting (goes nowhere on device) and time() for the emulated RTC,
 * which is mapped to the Playdate clock here. Simulator builds use the
 * host libc and skip all of this. */

#ifdef TARGET_PLAYDATE

#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>

#include "pd_api.h"

/* Set by playdate_main.c at init; NULL-safe fallbacks before that. */
PlaydateAPI *pd_syscalls_pd;

/* Seconds between the Unix epoch (1970) and the Playdate epoch (2000). */
#define PD_EPOCH_OFFSET 946684800u

int _write(int fd, const void *buf, unsigned int count)
{
  (void)fd; (void)buf;
  return (int)count; /* swallow stdout/stderr */
}

int _read(int fd, void *buf, unsigned int count)
{
  (void)fd; (void)buf; (void)count;
  return 0;
}

int _close(int fd)
{
  (void)fd;
  return -1;
}

long _lseek(int fd, long offset, int whence)
{
  (void)fd; (void)offset; (void)whence;
  return -1;
}

int _fstat(int fd, struct stat *st)
{
  (void)fd;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd)
{
  (void)fd;
  return 1;
}

int _gettimeofday(struct timeval *tv, void *tz)
{
  (void)tz;
  if (!tv)
    return -1;

  if (pd_syscalls_pd)
  {
    unsigned int ms = 0;
    unsigned int secs = pd_syscalls_pd->system->getSecondsSinceEpoch(&ms);
    tv->tv_sec = (time_t)(secs + PD_EPOCH_OFFSET);
    tv->tv_usec = (suseconds_t)ms * 1000;
  }
  else
  {
    tv->tv_sec = PD_EPOCH_OFFSET;
    tv->tv_usec = 0;
  }
  return 0;
}

#endif /* TARGET_PLAYDATE */

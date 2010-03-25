/* sigh, a signal handling library
 * (C) 2004 Nick Welch
 *
 * Usage of the works is permitted provided that this
 * instrument is retained with the works, so that any entity
 * that uses the works is notified of this instrument.
 *
 * DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.
 *
 * [2004, Fair License; rhid.com/fair]
 */

#include "sigh.h"

/* gettimeofday / struct timeval */
#include <time.h>
#include <sys/time.h>

#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INTERNAL */

#ifndef DOXYGEN /* hide internals from doxygen */

static int __sigh_sigtimedwait
(const sigset_t * sigs, long usec)
{
    int ret;
    struct timespec timeout;
    struct timeval begin, current, end; /* unused by poll */

    timeout.tv_sec = usec / 1000000;
    timeout.tv_nsec = usec%1000000 * 1000;

    if(!usec) /* this is a poll */
    {
        /* unlikely that a poll will be interrupted but you never know */
        do ret = sigtimedwait(sigs, 0, &timeout);
        while(ret == -1 && errno == EINTR);
        return ret > 0 ? ret : 0;
    }

    /* else this is a wait */

    /* i am still uncomfortable with the fact that so much legwork is needed to
     * resume a sigtimedwait, while not possibly sleeping forever.  this is the
     * only way i'm aware that it can be done, so that's what we do.  i just
     * wish i didn't have to.
     */

    gettimeofday(&begin, 0);
    end.tv_sec = begin.tv_sec + begin.tv_usec/1000000 + usec/1000000;
    end.tv_usec = begin.tv_usec%1000000 + usec%1000000;

    for(;;)
    {
        ret = sigtimedwait(sigs, 0, &timeout);

        /* if sigtimedwait finished without interruption, we're done here */
        if(ret != -1 || errno != EINTR)
            break;

        /* find out how far along we are */
        gettimeofday(&current, 0);

        /* if we've been interrupted but gone past the wait period anyways,
         * then we're done */
        if(current.tv_sec > end.tv_sec ||
                (current.tv_sec == end.tv_sec &&
                 current.tv_usec > end.tv_usec))
            break;

        /* ok, so we've been interrupted early: resume sigtimedwait() for
         * the remainder of time we're supposed to be here */

        timeout.tv_sec = end.tv_sec - current.tv_sec;
        timeout.tv_nsec = (end.tv_usec - current.tv_usec) * 1000;

        while(timeout.tv_nsec < 0)
        {
            --timeout.tv_sec;
            timeout.tv_nsec += 1000000000;
        }
    }

    /* possible errors for sigtimedwait() (from linux man page):
     *
     *   EINVAL (invalid pointer) - won't happen.
     *   EINTR - taken care of by the do/while loop.
     *
     * so the only error we need to worry about is:
     *
     *   EAGAIN - signal(s) wasn't/weren't received.
     *
     * therefore, a return of -1 will only be EAGAIN, which means that the
     * signal(s) wasn't/weren't caught, and for that, we return "false" (0).
     */

    return ret > 0 ? ret : 0;
}

static int __sigh_ensure_sigprocmask
(int how, const sigset_t * sigs)
{
    int ret;

    do ret = sigprocmask(how, sigs, 0);
    while(ret == -1 && errno == EINTR);

    return ret == 0;
}

#endif /* ifndef DOXYGEN */

/* PUBLIC */

char sigh_watch(const sigset_t * sigs)
{
    return __sigh_ensure_sigprocmask(SIG_BLOCK, sigs);
}

char sigh_ignore(const sigset_t * sigs)
{
    return __sigh_ensure_sigprocmask(SIG_UNBLOCK, sigs);
}

int sigh_wait(const sigset_t * sigs, long usec)
{
    return __sigh_sigtimedwait(sigs, usec);
}

int sigh_poll(const sigset_t * sigs)
{
    return __sigh_sigtimedwait(sigs, 0);
}

sigset_t sigh_make_sigset(int sig, ...)
{
    va_list sigs;
    int current_sig = sig;
    sigset_t set;

    sigemptyset(&set);

    va_start(sigs, sig);
    while(current_sig)
    {
        sigaddset(&set, current_sig);
        current_sig = va_arg(sigs, int);
    }
    va_end(sigs);

    return set;
}

#ifdef __cplusplus
}
#endif


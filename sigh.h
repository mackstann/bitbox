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

#ifndef __SIGH_SIGH_H
#define __SIGH_SIGH_H

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/**

@mainpage
@author Nick Welch <mack@incise.org>
@date 2004

@section synopsis Synopsis

sigh is a library for handling UNIX signals in a simple, sane, and safe manner.

@section rationale Rationale

Wha?  Sane, safe?  Isn't that already easy to do?  Well, not really.  Handling
signals is pretty simple, but many implementations are prone to subtle bugs and
crashes due to race conditions and the laboriousness of constantly checking for
interrupted syscalls.

First you have the issue of complexity of the signal handler.  Depending on how
correct and portable you want to be, you have to limit the complexity of what
happens inside of your signal handler.  This might mean avoiding certain
syscalls inside of the handler, or it might mean doing nothing but setting a
flag, and then polling that flag in the program's main loop.  If you're not
very careful, you'll probably end up doing something in your signal handler
that you shouldn't, and eventually, sometime, somewhere, your program is
probably going to flake out or die because of it.  It probably won't be easily
repeatable, and it might be so rare that you just dismiss it, but it'll be
there.

Alright, so let's be slick and avoid complexity inside of our signal handlers.
We'll do nothing but set a flag of type @c sig_atomic_t to @c 1, and return.
Our main loop will periodically check that flag, and when it's found, it'll run
whatever handling code it wants, and reset the flag to @c 0.  Cool!

Now we have another problem: @c EINTR.  When a system call is interrupted --
depending on which it is, and depending on specifics of your system and how you
compiled your program -- it will set @c errno to @c EINTR and return @c -1.
This means you potentially have to check @c errno for @c EINTR after @em every
system call, and then probably @em re-run that syscall, potentially @em
multiple times, until it completes without interruption.  Yuck.

The issue is even more complex than this.  For more detail, check out the
documentation for a project called <a
href="http://www.slamb.org/projects/sigsafe/">sigsafe</a> (which is itself a
solution to the problem).

All of this is a bit of a nightmare.  I just want to see if a person hit ^C, or
if I received SIGCHLD, or something like that.  Shouldn't that be simple?
Shouldn't that be easy to do without race conditions and bugs and complexity?
For that matter, it'd be nice to avoid the asynchronous nature of signals, and
just handle them like any other event, within the normal flow of our program.

Ok, well, we can do that!  And it's not all that painful, although it did take
me some time to work out all of the little details.  The result is sigh.  We
tell sigh which signals we want to listen for, and those signals no longer are
asynchronously delivered to our app.  Then, we frequently check to see whether
these signals have been received, and we handle them appropriately.  It's
something like:

@code
sigh_please_handle_SIGINT();

while(1)
{
    do_everything_else();
    if(sigh_has_received_SIGINT())
        handle_sigint();
}
@endcode

@section procon Pros / Cons

Pros:

- No re-entrant signal handlers; more logical code flow
- Syscalls are never interrupted
- No race conditions
- Potentially faster than asynchronous signal handlers

Cons:

- Sometimes you want a syscall to be interrupted.
- Not appropriate for use with anything that blocks; signals will almost always
  need to be polled many times per second.
- Considering the above point, and the fact that it's completely up to you as
  to how frequently you poll for signals, it is very possible to handle signals
  @em slower than they would be via normal handlers.
- If you don't toss the source code into your own source tree, then it's yet
  another dependency for your users.

@section implementation Implementation

The manner in which sigh goes about doing all of this is fairly simple, but has
some details which complicate the implementation a little.  Essentially, sigh
uses @c sigprocmask() with @c SIG_BLOCK to block asynchronous delivery of
signals.  It then uses @c sigtimedwait() to check whether or not signals have
been delivered.

@section getit Get it

Source is in arch <a
href="http://incise.org:82/archzoom.cgi/mack@incise.org--2004/sigh">here</a>,
no releases yet.

@sa
- @link PublicInterface Public Interface @endlink

*/

/**
 * @example frequency-sigh.c
 */
/**
 * @example frequency-standard.c
 */
/**
 * @example latency-sigh-receive.c
 */
/**
 * @example latency-standard-receive.c
 */

/**
 * @defgroup PublicInterface Public Interface
 *
 * @note sigh uses @c char to symbolize a boolean. anything that takes or
 * returns a @c char only cares about it being zero and non-zero.  a return
 * value of zero (i.e. false) indicates failure and non-zero (i.e. true)
 * indicates success.
 *
 * @note in all sigh functions, special care is taken to ensure the the
 * underlying system call is restarted if interrupted.  you do not need to
 * worry about checking @c errno for @c EINTR and whatnot after running sigh
 * functions.
 *
 * To use sigh, you first construct a @c sigset_t, either with @c
 * sigh_make_sigset(); or with @c sigemptyset(), @c sigaddset(), et. al.; or
 * whichever method works for you.
 *
 * You then tell sigh that this @c sigset_t contains the signals you want
 * to watch for, by calling @c sigh_watch().  If you ever want to reverse
 * this, use @c sigh_ignore().
 *
 * Now that you have sigh watching for your signals -- and blocking their
 * asynchronous delivery -- you check every so often to see if any have been
 * received.  You can do a poll with @c sigh_poll(), or you can wait around for
 * them with @c sigh_wait().  These will either return zero, meaning no signals
 * were received, or they will return a positive integer: the signal number.
 * You then handle the signal however you want.  Say you received @c SIGHUP --
 * you now call your restart function by hand, instead of registering it as a
 * signal handler with @c sigaction() or however you might have done it before.
 * Nothing is called asynchronously, the state of the program is known, and you
 * can easily and simply handle your signals like you'd handle any other event
 * or condition.
 *
 * @{
 */

/**
 * @brief block signals in @c sigs from being handled via "normal" means, so
 * that they can be checked for on our terms, with @c sigh_poll() and @c
 * sigh_wait().
 *
 * @return @c non-zero for success, @c zero for failure.  there are only two
 * possibilities for failure: 1. if you pass in a bad pointer, in which case @c
 * errno will be set to @c EFAULT.  2. if you pass a bad signal number, in
 * which case @c errno will be set to @c EINVAL.  both are programmer errors
 * and generally will either just not happen, or they will be caught during
 * coding/debugging.
 */
char sigh_watch(const sigset_t * sigs);

/** 
 * @brief performs the opposite of @c sigh_watch(): unblocks all signals in @c
 * sigs.
 *
 * all of the notes for @c sigh_watch() also apply to this function; they are
 * largely the same underlying code.  @c _watch() blocks, @c _ignore()
 * unblocks.
 */
char sigh_ignore(const sigset_t * sigs);

/**
 * @brief wait for @a usec microseconds for a signal in @a sigs to be received.
 * @return signal number if a signal in @c sigs has been caught, otherwise zero,
 * indicating no signals were caught.
 */
int sigh_wait(const sigset_t * sigs, long usec);

/**
 * @brief check if any signal in @c sigs has been received, and return
 * immediately in either case.
 * @return same as @c sigh_wait().
 */
int sigh_poll(const sigset_t * sigs);

/**
 * @brief a convenient way to construct a @c sigset_t.
 * @return a @c sigset_t containing the signals passed in argument list.
 * 
 * pass a list of signal numbers, and terminate this list with 0.  it is valid
 * to call with a single zero argument; this will return an empty @c sigset_t.
 * the following are all valid:
 * @code
 * sigset_t mysigset = sigh_make_sigset(SIGTERM, 0);
 * @endcode
 * @code
 * sigset_t mysigset = sigh_make_sigset(SIGHUP, SIGUSR1, SIGUSR2, 0);
 * @endcode
 * @code
 * sigset_t mysigset = sigh_make_sigset(0);
 * @endcode
 */
sigset_t sigh_make_sigset(int sig, ...);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __SIGH_SIGH_H */


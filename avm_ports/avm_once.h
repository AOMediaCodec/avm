/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AVM_PORTS_AVM_ONCE_H_
#define AVM_AVM_PORTS_AVM_ONCE_H_

#include "config/avm_config.h"

/* Implement a function wrapper to guarantee initialization
 * thread-safety for library singletons.
 *
 * NOTE: This function uses static locks, and can only be
 * used with one common argument per compilation unit. So
 *
 * file1.c:
 *   avm_once(foo);
 *   ...
 *   avm_once(foo);
 *
 * file2.c:
 *   avm_once(bar);
 *
 * will ensure foo() and bar() are each called only once, but in
 *
 * file1.c:
 *   avm_once(foo);
 *   avm_once(bar):
 *
 * bar() will never be called because the lock is used up
 * by the call to foo().
 */

#if CONFIG_MULTITHREAD && defined(_WIN32)
#include <windows.h>
/* Declare a per-compilation-unit state variable to track the progress
 * of calling func() only once. This must be at global scope because
 * local initializers are not thread-safe in MSVC prior to Visual
 * Studio 2015.
 */
static INIT_ONCE avm_init_once = INIT_ONCE_STATIC_INIT;

static void avm_once(void (*func)(void)) {
  BOOL pending;
  InitOnceBeginInitialize(&avm_init_once, 0, &pending, NULL);
  if (!pending) {
    // Initialization has already completed.
    return;
  }
  func();
  InitOnceComplete(&avm_init_once, 0, NULL);
}

#elif CONFIG_MULTITHREAD && defined(__OS2__)
#define INCL_DOS
#include <os2.h>
static void avm_once(void (*func)(void)) {
  static int done;

  /* If the initialization is complete, return early. */
  if (done) return;

  /* Causes all other threads in the process to block themselves
   * and give up their time slice.
   */
  DosEnterCritSec();

  if (!done) {
    func();
    done = 1;
  }

  /* Restores normal thread dispatching for the current process. */
  DosExitCritSec();
}

#elif CONFIG_MULTITHREAD && HAVE_PTHREAD_H
#include <pthread.h>
static void avm_once(void (*func)(void)) {
  static pthread_once_t lock = PTHREAD_ONCE_INIT;
  pthread_once(&lock, func);
}

#else
/* Default version that performs no synchronization. */

static void avm_once(void (*func)(void)) {
  static int done;

  if (!done) {
    func();
    done = 1;
  }
}
#endif

#endif  // AVM_AVM_PORTS_AVM_ONCE_H_

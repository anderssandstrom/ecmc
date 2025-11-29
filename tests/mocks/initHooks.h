/*************************************************************************\
* Minimal initHooks definitions for unit tests.
\*************************************************************************/

#ifndef INIT_HOOKS_STUB_H_
#define INIT_HOOKS_STUB_H_

typedef enum {
  initHookAtIocBuild = 0,
  initHookAtBeginning,
  initHookAfterCallbackInit,
  initHookAfterCaLinkInit,
  initHookAfterInitDrvSup,
  initHookAfterInitRecSup,
  initHookAfterInitDevSup,
  initHookAfterInitDatabase,
  initHookAfterFinishDevSup,
  initHookAfterScanInit,
  initHookAfterInitialProcess,
  initHookAfterIocBuilt,
  initHookAtIocRun,
  initHookAfterDatabaseRunning,
  initHookAfterCaServerRunning,
  initHookAfterIocRunning,
  initHookAtIocPause,
  initHookAfterCaServerPaused,
  initHookAfterDatabasePaused,
  initHookAfterIocPaused,
  initHookAfterInterruptAccept
} initHookState;

#endif  /* INIT_HOOKS_STUB_H_ */

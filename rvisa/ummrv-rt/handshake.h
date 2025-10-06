#include "syslib.h"

/*
Handshakes ease synchronization between two tasklets, usually to start one of
them when the other has finished a specific task.
When a tasklet calls handshake_wait_for(notifier), its execution is suspended
until the notifier tasklet calls handshake_notify(). If the notifier calls
handshake_notify() before the other tasklet starts to wait, the notifier
execution is suspended until a tasklet calls handshake_wait_for(notifier). If a
tasklet calls handshake_wait_for(notifier) with an already waiting tasklet on
this notifier, the second tasklet does not wait and an error is returned.
*/

// allows a tasklet to wait on another one and returns 0 in case of success
int handshake_wait_for(sysname_t notifier);
//  allows a tasklet to notify a waiting tasklet
int handshake_notify_for(sysname_t notifier);
// Like handshake_notify_for(me()) but do NOT check `a < 0` because if all
// threads exclusively notify slots[me()], only one thread writes -me() every
// slot. Passing me() to notify_for exclusively is very common so having this
// special path is worthwhile.
// Either use notify_for or notify exclusively; mixing two in a same segment is
// undefined behavior.
void handshake_notify(void);

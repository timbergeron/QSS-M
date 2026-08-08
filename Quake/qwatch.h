/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * qwatch.h -- QSS-M Aura bridge.
 *
 * The bridge is deliberately small: the engine publishes only the local
 * player's two relevant inventory bits and never exposes entity state.
 */

#ifndef QWATCH_H
#define QWATCH_H

#include "q_stdinc.h"

struct cvar_s;
extern struct cvar_s qwatch_aura;
extern struct cvar_s qwatch_port;

void QWatch_InitLocal(void);
qboolean QWatch_LocalPlayerSpectating(void);
void QWatch_Frame(qboolean valid, unsigned int items);
void QWatch_Shutdown(void);

#endif /* QWATCH_H */

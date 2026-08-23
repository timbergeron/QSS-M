/* Cross-platform discovery and import of unmounted Quake data. */
#ifndef QSSM_QUAKE_DATA_IMPORT_H
#define QSSM_QUAKE_DATA_IMPORT_H

#include <stddef.h>

qboolean QuakeDataImport_RunAtStartup(const char *basedir, const char *userdir,
	qboolean dedicated, char *missing_hint, size_t missing_hint_size);

qboolean QuakeDataImport_IsSelfTestArg(int argc, char **argv);
int QuakeDataImport_RunSelfTests(void);

/* Shared by mounted and raw pak1.pak validation. */
qboolean QuakeDataImport_ValidatePopData(const void *data, size_t size);

#endif

//
// iplog.h
//
// JPG 1.05
//
// This entire file is new in proquake 1.05.  It is used for player IP logging.
//

typedef struct tagIPLog
{
	int addr;
	char name[16];
	struct tagIPLog *parent;
	struct tagIPLog *children[2];
} iplog_t;

extern int iplog_size;

void IPLog_Init (void);
void IPLog_WriteLog (void);
void IPLog_Add (int addr, char *name);
void IPLog_Delete (iplog_t *node);
iplog_t *IPLog_Merge (iplog_t *left, iplog_t *right);
void IPLog_Identify (int addr);
void IPLog_Dump (void);
void IPLog_Import (void);

void IPLog_PrintNames (void);



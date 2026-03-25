//
// iplog.c
//
// JPG 1.05
//
// This entire file is from proquake 1.05.  It is used for player IP logging.
//

#include "quakedef.h"



extern int con_linewidth;
iplog_t	*iplogs;
iplog_t *iplog_head;

int iplog_size;
int iplog_next;
int iplog_full;

#define DEFAULT_IPLOGSIZE	0x10000
#define MAX_REPETITION	128

static qboolean migrated_old_files = false; // Track if migration has occurred

/*
====================
IPLog_CopyPrintableName
====================
*/
static void IPLog_CopyPrintableName(char *dst, size_t dstsize, const char *src)
{
	unsigned char *ch;

	q_strlcpy(dst, src, dstsize);
	for (ch = (unsigned char *)dst; *ch; ch++)
	{
		*ch = dequake[*ch];
		if (*ch == 10 || *ch == 13)
			*ch = ' ';
	}
}

/*
====================
IPLog_CopyRawName
====================
*/
static void IPLog_CopyRawName(char *dst, size_t dstsize, const char *src)
{
	q_strlcpy(dst, src, dstsize);
}

/*
====================
IPLog_CountTree
====================
*/
static int IPLog_CountTree(iplog_t *root)
{
	if (!root)
		return 0;

	return 1 + IPLog_CountTree(root->children[0]) + IPLog_CountTree(root->children[1]);
}

/*
====================
IPLog_CollectNames
====================
*/
static void IPLog_CollectNames(iplog_t *root, char (*names)[16], int *count)
{
	if (!root)
		return;

	IPLog_CollectNames(root->children[0], names, count);
	IPLog_CopyRawName(names[*count], sizeof(names[*count]), root->name);
	(*count)++;
	IPLog_CollectNames(root->children[1], names, count);
}

/*
====================
IPLog_CompareNames
====================
*/
static int IPLog_CompareNames(const void *lhs, const void *rhs)
{
	const char *left = (const char *)lhs;
	const char *right = (const char *)rhs;
	const int diff = q_strcasecmp(left, right);

	if (diff)
		return diff;

	return strcmp(left, right);
}

/*
====================
IPLog_MigrateFiles
====================
*/
static void IPLog_MigrateFiles(void)
{
	FILE *f_old, *f_new;
	char old_dat_path[MAX_OSPATH];
	char new_dat_path[MAX_OSPATH];
	char old_txt_path[MAX_OSPATH];
	char new_txt_path[MAX_OSPATH];
	char backup_dir[MAX_OSPATH];
	qboolean migrated_any = false;
	
	// Create paths
	q_snprintf(old_dat_path, sizeof(old_dat_path), "%s/id1/iplog.dat", com_basedir);
	q_snprintf(old_txt_path, sizeof(old_txt_path), "%s/id1/iplog.txt", com_basedir);
	q_snprintf(backup_dir, sizeof(backup_dir), "%s/id1/backups", com_basedir);
	q_snprintf(new_dat_path, sizeof(new_dat_path), "%s/id1/backups/iplog.dat", com_basedir);
	q_snprintf(new_txt_path, sizeof(new_txt_path), "%s/id1/backups/iplog.txt", com_basedir);
	
	// Ensure backups directory exists
	Sys_mkdir(backup_dir);
	
	// Migrate iplog.dat if it exists in old location and doesn't exist in new location
	f_old = fopen(old_dat_path, "rb");
	if (f_old)
	{
		fclose(f_old);
		f_new = fopen(new_dat_path, "rb");
		if (!f_new) // New location doesn't exist, safe to migrate
		{
			// Try atomic rename first
			if (rename(old_dat_path, new_dat_path) == 0)
			{
				Con_Printf("Migrated iplog.dat to backups folder\n");
				migrated_any = true;
			}
			else
			{
				// Fallback to copy if rename fails
				f_old = fopen(old_dat_path, "rb");
				f_new = fopen(new_dat_path, "wb");
				
				if (f_old && f_new)
				{
					// Copy file contents
					char buffer[4096];
					size_t bytes_read, bytes_written;
					qboolean copy_failed = false;
					
					while ((bytes_read = fread(buffer, 1, sizeof(buffer), f_old)) > 0)
					{
						bytes_written = fwrite(buffer, 1, bytes_read, f_new);
						if (bytes_written != bytes_read)
						{
							copy_failed = true;
							break;
						}
					}
					
					if (ferror(f_old) || ferror(f_new))
						copy_failed = true;
					
					fclose(f_old);
					fclose(f_new);
					
					if (!copy_failed)
					{
						// Remove old file after successful copy
						remove(old_dat_path);
						Con_Printf("Migrated iplog.dat to backups folder\n");
						migrated_any = true;
					}
					else
					{
						Con_Printf("WARNING: Failed to migrate iplog.dat\n");
						remove(new_dat_path); // Clean up partial copy
					}
				}
				else
				{
					if (f_old) fclose(f_old);
					if (f_new) fclose(f_new);
				}
			}
		}
		else
		{
			fclose(f_new);
		}
	}
	
	// Migrate iplog.txt if it exists in old location and doesn't exist in new location
	f_old = fopen(old_txt_path, "rb");
	if (f_old)
	{
		fclose(f_old);
		f_new = fopen(new_txt_path, "rb");
		if (!f_new) // New location doesn't exist, safe to migrate
		{
			// Try atomic rename first
			if (rename(old_txt_path, new_txt_path) == 0)
			{
				Con_Printf("Migrated iplog.txt to backups folder\n");
				migrated_any = true;
			}
			else
			{
				// Fallback to copy if rename fails
				f_old = fopen(old_txt_path, "rb");
				f_new = fopen(new_txt_path, "wb");
				
				if (f_old && f_new)
				{
					// Copy file contents
					char buffer[4096];
					size_t bytes_read, bytes_written;
					qboolean copy_failed = false;
					
					while ((bytes_read = fread(buffer, 1, sizeof(buffer), f_old)) > 0)
					{
						bytes_written = fwrite(buffer, 1, bytes_read, f_new);
						if (bytes_written != bytes_read)
						{
							copy_failed = true;
							break;
						}
					}
					
					if (ferror(f_old) || ferror(f_new))
						copy_failed = true;
					
					fclose(f_old);
					fclose(f_new);
					
					if (!copy_failed)
					{
						// Remove old file after successful copy
						remove(old_txt_path);
						Con_Printf("Migrated iplog.txt to backups folder\n");
						migrated_any = true;
					}
					else
					{
						Con_Printf("WARNING: Failed to migrate iplog.txt\n");
						remove(new_txt_path); // Clean up partial copy
					}
				}
				else
				{
					if (f_old) fclose(f_old);
					if (f_new) fclose(f_new);
				}
			}
		}
		else
		{
			fclose(f_new);
		}
	}
	
	// Set flag to indicate migration check has been completed
	migrated_old_files = true;
	
	if (!migrated_any)
		Con_DPrintf("IPLog migration check complete - no files to migrate\n");
}

/*
====================
IPLog_GetDataPath
====================
*/
static const char* IPLog_GetDataPath(void)
{
	static char path[MAX_OSPATH];
	q_snprintf(path, sizeof(path), "%s/id1/backups/iplog.dat", com_basedir);
	return path;
}

/*
====================
IPLog_GetTxtPath
====================
*/
static const char* IPLog_GetTxtPath(void)
{
	static char path[MAX_OSPATH];
	q_snprintf(path, sizeof(path), "%s/id1/backups/iplog.txt", com_basedir);
	return path;
}

/*
====================
IPLog_Init
====================
*/
void IPLog_Init (void)
{
	//int p;
	FILE *f;
	iplog_t temp;
	char backup_dir[MAX_OSPATH];

	// Allocate space for the IP logs
	iplog_size = 0;
//	p = COM_CheckParm ("-iplog");
//	if (!p)
//		return;

//	if (p < com_argc - 1)
//		iplog_size = Q_atoi(com_argv[p+1]) * 1024 / sizeof(iplog_t);

//	if (!iplog_size)
		iplog_size = DEFAULT_IPLOGSIZE;

	iplogs = (iplog_t *) Q_malloc (iplog_size * sizeof(iplog_t));
	// Zero-fill the allocated memory to avoid random data in unused slots
	memset(iplogs, 0, iplog_size * sizeof(*iplogs));
	
	iplog_next = 0;
	iplog_head = NULL;
	iplog_full = 0;

	// Ensure backups directory exists
	q_snprintf(backup_dir, sizeof(backup_dir), "%s/id1/backups", com_basedir);
	Sys_mkdir(backup_dir);
	
	// Migrate old files if they exist
	IPLog_MigrateFiles();

	// Attempt to load log data from iplog.dat (new location first, then old location for compatibility)
//	Sys_GetLock();
	f = fopen(IPLog_GetDataPath(), "rb");
	if (!f && !migrated_old_files)
	{
		// Fallback to old location if new location doesn't exist and migration hasn't been attempted
		f = fopen(va("%s/id1/iplog.dat", com_basedir), "rb");
	}
	
	if (f)
	{
		while(fread(&temp, 20, 1, f))
			IPLog_Add(temp.addr, temp.name);
		fclose(f);
	}
//	Sys_ReleaseLock();
}

/*
====================
IPLog_Import
====================
*/
void IPLog_Import (void)
{
	FILE *f;
	iplog_t temp;

	if (!iplog_size) // woods
	{
		Con_Printf("IP logging not available\n");
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Con_Printf("Usage: ipmerge <filename>\n");
		return;
	}
	f = fopen(va("%s", Cmd_Argv(1)), "rb");
	if (f)
	{
		while(fread(&temp, 20, 1, f))
			IPLog_Add(temp.addr, temp.name);
		fclose(f);
		Con_Printf("Merged %s\n", Cmd_Argv(1));
	}
	else
		Con_Printf("Could not open %s\n", Cmd_Argv(1));
}

/*
====================
IPLog_WriteLog
====================
*/
void IPLog_WriteLog (void)
{
	FILE *f;
	int i;
	iplog_t temp;
	char backup_dir[MAX_OSPATH];

	if (!iplog_size)
		return;

	// Ensure backups directory exists
	q_snprintf(backup_dir, sizeof(backup_dir), "%s/id1/backups", com_basedir);
	Sys_mkdir(backup_dir);

//	Sys_GetLock();

	// first merge - try new location first, then old location only if migration hasn't occurred
	f = fopen(IPLog_GetDataPath(), "rb");
	if (!f && !migrated_old_files)
	{
		// Fallback to old location for compatibility only if we haven't migrated yet
		f = fopen(va("%s/id1/iplog.dat", com_basedir), "rb");
	}
	
	if (f)
	{
		while(fread(&temp, 20, 1, f))
			IPLog_Add(temp.addr, temp.name);
		fclose(f);
	}

	// then write to new location
	f = fopen(IPLog_GetDataPath(), "wb");
	if (f)
	{
		if (iplog_full)
		{
			for (i = iplog_next + 1 ; i < iplog_size ; i++)
				fwrite(&iplogs[i], 20, 1, f);
		}
		for (i = 0 ; i < iplog_next ; i++)
			fwrite(&iplogs[i], 20, 1, f);

		fclose(f);
	}	
	else
		Con_Printf("Could not write iplog.dat\n");

//	Sys_ReleaseLock();
}

/*
====================
IPLog_Add
====================
*/
void IPLog_Add (int addr, char *name)
{
	iplog_t	*iplog_new;
	iplog_t **ppnew;
	iplog_t *parent;
	char name2[16];
	char *ch;
	int cmatch;		// limit 128 entries per IP
	iplog_t *match[MAX_REPETITION];
	int i;

	if (!iplog_size)
		return;

	// delete trailing spaces
	strncpy(name2, name, 15);
	ch = &name2[15];
	*ch = 0;
	while (ch >= name2 && (*ch == 0 || *ch == ' '))
		*ch-- = 0;
	if (ch < name2)
		return;

	iplog_new = &iplogs[iplog_next];

	cmatch = 0;
	parent = NULL;
	ppnew = &iplog_head;
	while (*ppnew)
	{
		if ((*ppnew)->addr == addr)
		{
			if (!strcmp(name2, (*ppnew)->name))
			{
				return;
			}
			match[cmatch] = *ppnew;
			if (++cmatch == MAX_REPETITION)
			{
				// shift up the names and replace the last one
				for (i = 0 ; i < MAX_REPETITION - 1 ; i++)
					strcpy(match[i]->name, match[i+1]->name);
				strcpy(match[i]->name, name2);
				return;
			}
		}
		parent = *ppnew;
		ppnew = &(*ppnew)->children[addr > (*ppnew)->addr];
	}
	*ppnew = iplog_new;
	strcpy(iplog_new->name, name2);
	iplog_new->addr = addr;
	iplog_new->parent = parent;
	iplog_new->children[0] = NULL;
	iplog_new->children[1] = NULL;

	if (++iplog_next == iplog_size)
	{
		iplog_next = 0;
		iplog_full = 1;
	}
	if (iplog_full)
		IPLog_Delete(&iplogs[iplog_next]);
}

/*
====================
IPLog_Delete
====================
*/
void IPLog_Delete (iplog_t *node)
{
	iplog_t *newlog;

	newlog = IPLog_Merge(node->children[0], node->children[1]);
	if (newlog)
		newlog->parent = node->parent;
	if (node->parent)
		node->parent->children[node->addr > node->parent->addr] = newlog;
	else
		iplog_head = newlog;
}

/*
====================
IPLog_Merge
====================
*/
iplog_t *IPLog_Merge (iplog_t *left, iplog_t *right)
{
	if (!left)
		return right;
	if (!right)
		return left;

	if (rand() & 1)
	{
		left->children[1] = IPLog_Merge(left->children[1], right);
		left->children[1]->parent = left;
		return left;
	}
	right->children[0] = IPLog_Merge(left, right->children[0]);
	right->children[0]->parent = right;
	return right;
}

/*
====================
IPLog_Identify
====================
*/
void IPLog_Identify (int addr)
{
	iplog_t *node;
	int count = 0;

	node = iplog_head;
	while (node)
	{
		if (node->addr == addr)
		{
			Con_Printf("%s\n", node->name);
			count++;
		}
		node = node->children[addr > node->addr];
	}
	Con_Printf("%d %s found\n", count, (count == 1) ? "entry" : "entries");
}

/*
====================
IPLog_DumpTree
====================
*/
void IPLog_DumpTree (iplog_t *root, FILE *f)
{
	char address[20];
	char name[16];

	if (!root)
		return;
	IPLog_DumpTree(root->children[0], f);

	sprintf(address, "%d.%d.%d.xxx", root->addr >> 16, (root->addr >> 8) & 0xff, root->addr & 0xff);
	IPLog_CopyPrintableName(name, sizeof(name), root->name);

	fprintf(f, "%-16s  %s\n", address, name); // woods 

	IPLog_DumpTree(root->children[1], f);
}

/*
====================
IPLog_PrintNames
====================
*/
void IPLog_PrintNames (void)
{
	char (*names)[16];
	const char *col1;
	int colwidth;
	int cols;
	int count;
	int i;
	int j;
	int maxlen;
	int unique_count;

	if (!iplog_size)
	{
		Con_Printf("IP logging not available\n");
		return;
	}

	count = IPLog_CountTree(iplog_head);
	if (!count)
	{
		Con_Printf("No IP log entries found\n");
		return;
	}

	names = (char (*)[16])Q_malloc(count * sizeof(*names));
	i = 0;
	IPLog_CollectNames(iplog_head, names, &i);
	qsort(names, count, sizeof(*names), IPLog_CompareNames);

	unique_count = 0;
	for (i = 0; i < count; i++)
	{
		if (unique_count > 0 && !strcmp(names[unique_count - 1], names[i]))
			continue;

		if (unique_count != i)
			memcpy(names[unique_count], names[i], sizeof(*names));
		unique_count++;
	}
	count = unique_count;
	maxlen = 0;
	for (i = 0; i < count; i++)
		maxlen = q_max(maxlen, (int)strlen(names[i]));

	colwidth = q_max(maxlen + 2, 8);
	cols = q_max(con_linewidth / colwidth, 1);
	cols = q_min(cols, count);

	Con_Printf("IP log names:\n");
	for (i = 0; i < count; i += cols)
	{
		for (j = 0; j < cols && i + j < count; j++)
		{
			col1 = names[i + j];
			Con_Printf("%-*.*s", colwidth, colwidth, col1);
		}
		Con_Printf("\n");
	}
	Con_Printf("%d %s found\n", count, (count == 1) ? "entry" : "entries");

	free(names);
}

/*
====================
IPLog_Dump
====================
*/
void IPLog_Dump (void)
{
	FILE *f;
	char backup_dir[MAX_OSPATH];

	if (!iplog_size)
	{
		Con_Printf("IP logging not available\n");
		return;
	}

	// Ensure backups directory exists
	q_snprintf(backup_dir, sizeof(backup_dir), "%s/id1/backups", com_basedir);
	Sys_mkdir(backup_dir);

	f = fopen(IPLog_GetTxtPath(), "w");
	if (!f)
	{
		Con_Printf ("Couldn't write iplog.txt.\n");
		return;
	}

	IPLog_DumpTree(iplog_head, f);
	fclose(f);
	Con_Printf("Wrote iplog.txt\n");
}


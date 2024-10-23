//------------
// locations.c
//------------
// Original format by JP Grossman for Proquake -- woods / rook #pqteam

#include "quakedef.h"

#define VectorSet(v, x, y, z) ((v)[0] = (x), (v)[1] = (y), (v)[2] = (z))
#define VectorClear(a) ((a)[0] = (a)[1] = (a)[2] = 0)

location_t	*locations, *temploc;

extern	cvar_t	r_drawlocs;

void TP_LocFiles_Init(void); // woods #locext

void LOC_PQ_Init (void) // woods added PQ to name #pqteam
{
	temploc = Z_Malloc(sizeof(location_t));
	locations = Z_Malloc(sizeof(location_t));
    TP_LocFiles_Init(); // woods #locext
}

void LOC_Delete(location_t* loc)
{
	location_t** ptr, ** next;
	for (ptr = &locations; *ptr; ptr = next)
	{
		next = &(*ptr)->next_loc;
		if (*ptr == loc)
		{
			*ptr = loc->next_loc;
			Z_Free(loc);
		}
	}
}

void LOC_Clear_f (void)
{
	location_t *l;
	for (l = locations;l;l = l->next_loc)
	{
		LOC_Delete(l);
	}
}

void LOC_SetLoc (vec3_t mins, vec3_t maxs, char *name)
{
	location_t *l, **ptr;
	int namelen;
	float	temp;
	int		n;
	vec_t	sum;

	if (!(name))
		return;

	namelen = strlen(name);
	l = Z_Malloc(sizeof(location_t));

	sum = 0;
	for (n = 0 ; n < 3 ; n++)
	{
		if (mins[n] > maxs[n])
		{
			temp = mins[n];
			mins[n] = maxs[n];
			maxs[n] = temp;
		}
		sum += maxs[n] - mins[n];
	}
	
	l->sum = sum;

	VectorSet(l->mins, mins[0], mins[1], mins[2]-32);	//ProQuake Locs are extended +/- 32 units on the z-plane...
	VectorSet(l->maxs, maxs[0], maxs[1], maxs[2]+32);
	q_snprintf (l->name, namelen + 1, "%s", name);
	Con_DPrintf("Location %s assigned.\n", name);
	for (ptr = &locations;*ptr;ptr = &(*ptr)->next_loc);
	*ptr = l; // woods remove indent
}

qboolean nqloc; // woods nq/qw 3/6-point format flag #qwlocs
qboolean hasPrintedFormat = false; // woods #qwlocs

qboolean LOC_LoadLocations (void) // woods #qwlocs
{
    char	filename[MAX_OSPATH];
    vec3_t	mins, maxs;
    char	name[64]; // incrrased from 32 to 64 for qw locs

    int		i, linenumber, limit, len;
    char	*filedata, *text, *textend, *linestart, *linetext, *lineend;
    int		filesize;

    hasPrintedFormat = false;

    if (cls.state != ca_connected || !cl.worldmodel)
    {
        Con_Printf("!LOC_LoadLocations ERROR: No map loaded!\n");
        return false;
    }

    //LOC_Init();

    LOC_Clear_f();

    q_snprintf(filename, sizeof(filename), "locs/%s.loc", cl.mapname);

    if (COM_FileExists(filename, NULL) == false)
    {
        //Con_Printf("%s could not be found.\n", filename);
        return false;
    }

    filedata = (char*)COM_LoadTempFile(filename, NULL);

    if (!filedata)
    {
        Con_Printf("%s contains empty or corrupt data.\n", filename);
        return false;
    }

    filesize = strlen(filedata);

    text = filedata;
    textend = filedata + filesize;
    for (linenumber = 1;text < textend;linenumber++)
    {
        linestart = text;
        for (;text < textend && *text != '\r' && *text != '\n';text++)
            ;
        lineend = text;
        if (text + 1 < textend && *text == '\r' && text[1] == '\n')
            text++;
        if (text < textend)
            text++;
        // trim trailing whitespace
        while (lineend > linestart && lineend[-1] <= ' ')
            lineend--;
        // trim leading whitespace
        while (linestart < lineend && *linestart <= ' ')
            linestart++;
        // check if this is a comment
        if (linestart + 2 <= lineend && !strncmp(linestart, "//", 2))
            continue;
        linetext = linestart;
        limit = 3; // Assume a 3-point format initially
       
        for (i = 0;i < limit && linetext < lineend;i++) // detect 3 or 6 point format using commas, qw 3 point system has no commas
        {
            char* nextCommaOrSpace = linetext;
            while (nextCommaOrSpace < lineend && *nextCommaOrSpace != ',' && *nextCommaOrSpace > ' ')
                nextCommaOrSpace++;

            if (*nextCommaOrSpace == ',')
            {
                nqloc = true;
                if (!hasPrintedFormat)
                {
                    Con_DPrintf("NetQuake LOC Detected\n");
                    hasPrintedFormat = true;
                }
            }
            else
            {
				nqloc = false;
                if (!hasPrintedFormat)
                {
                    Con_DPrintf("QuakeWorld LOC Detected\n");
                    hasPrintedFormat = true;
                }
			}

            if (i < 3) mins[i] = atof(linetext); // Use mins for initial coordinates

            linetext = nextCommaOrSpace;
            if (*nextCommaOrSpace == ',') linetext++;

            while (linetext < lineend && *linetext <= ' ') linetext++;
        }

        if (nqloc) // Detected a 6-point format
        {
            limit = 6; // Adjust limit for parsing 6-point data
            for (; i < limit && linetext < lineend; i++)
            {
                char* nextCommaOrSpace = linetext;
                while (nextCommaOrSpace < lineend && *nextCommaOrSpace != ',' && *nextCommaOrSpace > ' ')
                    nextCommaOrSpace++;

                if (i >= 3) maxs[i - 3] = atof(linetext); // Use maxs for additional coordinates

                linetext = nextCommaOrSpace;
                if (*nextCommaOrSpace == ',') linetext++;

                while (linetext < lineend && *linetext <= ' ') linetext++;
            }
        }

        // Now, extract the name for both formats here
        while (linetext < lineend && *linetext <= ' ') linetext++;
        len = q_min(lineend - linetext, sizeof(name) - 1);
        memcpy(name, linetext, len);
        name[len] = '\0';

        if (nqloc && i == 6) // Successfully parsed a 6-point format
        {

            size_t nameLen = strlen(name);
            if (name[0] == '"' && name[nameLen - 1] == '"') {
                name[nameLen - 1] = '\0';
                memmove(name, name + 1, nameLen - 1);
            }

            LOC_SetLoc(mins, maxs, name);
        }
        else if (!nqloc && i == 3) // Successfully parsed a 3-point format
        {
            char* separator;
            while ((separator = strstr(name, "$loc_name_separator")) != NULL)
            {
                *separator = '-'; // Replace the separator with "-"

                char* restOfString = separator + strlen("$loc_name_separator");
                memmove(separator + 1, restOfString, strlen(restOfString) + 1);
            }

            char* locNameInstance;
            while ((locNameInstance = strstr(name, "$loc_name_")) != NULL) 
            {
                size_t prefixLength = strlen("$loc_name_"); // remove the prefix
                memmove(locNameInstance, locNameInstance + prefixLength, strlen(locNameInstance + prefixLength) + 1);
            }

            char* dollarDot;
            while ((dollarDot = strstr(name, "$.")) != NULL) 
            {
                *dollarDot = '-'; // Replace "$." with "-"
                memmove(dollarDot + 1, dollarDot + 2, strlen(dollarDot + 2) + 1); // +1 for null terminator
            }
            
            LOC_SetLoc(mins, mins, name);
        }
        else
            Con_DPrintf("Error parsing location data on line %d.\n", linenumber);
    }
    return true;
}

char *LOC_GetLocation (vec3_t p) // woods #qwlocs
{
    location_t *loc;
    location_t *bestloc;
    float dist, bestdist;

    bestloc = NULL;

    bestdist = 999999;

    vec3_t diff;

    for (loc = locations;loc;loc = loc->next_loc) 
    {
        if (!nqloc) // qw nearest point location
        {
            vec3_t scaled_mins;
            VectorCopy(loc->mins, scaled_mins);
            for (int i = 0; i < 3; i++)
            {
                scaled_mins[i] /= 8.0; // Adjusting the scale of the input coordinates to match the stored locations
            }

            VectorSubtract(p, scaled_mins, diff); // squared distance from the adjusted point to the location point
            dist = diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2];
        }
        else // nq axial box location: calculate the shortest distance from point to the axial box (rectangular prism)
        {
            dist = fabs(loc->mins[0] - p[0]) + fabs(loc->maxs[0] - p[0]) +
                fabs(loc->mins[1] - p[1]) + fabs(loc->maxs[1] - p[1]) +
                fabs(loc->mins[2] - p[2]) + fabs(loc->maxs[2] - p[2]) - loc->sum;

            if (dist < .01)
            {
                return loc->name;
            }
        }

        if (!bestloc || dist < bestdist) 
        {
            bestdist = dist;
            bestloc = loc;
            ;
        }
    }

    if (bestloc) 
    {
        return bestloc->name;
    }
    return "no .loc";
}

void R_EmitWireBox(vec3_t mins, vec3_t maxs, uint32_t color); // woods #locext
void R_EmitWirePoint(vec3_t origin, uint32_t color); // woods #locext
void R_EmitPin(vec3_t origin, float radius, uint32_t color, int segments); // woods #locext
extern cvar_t r_showlocs; // woods #locext
extern cvar_t r_showlocs_y; // woods #locext
char loc_str[64]; // woods #locext


/*
================
LOC_ShowLocs -- woods #locext -- draws all bounding boxes for NetQuake locations and all points for QuakeWorld locations.
After the loop, it highlights the nearest location (bestloc) in red.
================
*/
void LOC_ShowLocs (void)
{
    if (r_showlocs.value < 2)
        return;

    location_t* loc;
    vec3_t player_origin;
    location_t* bestloc = NULL;
    float dist, bestdist = 999999.0f; // Initialize best distance with a large value
    vec3_t diff;

    uint32_t nqDefaultBoxColor = 0xFFFFFFFF; // White color for default NetQuake boxes
    uint32_t nqPlayerBoxColor = 0xFF0000FF;  // Red color for nearest NetQuake location
    uint32_t qwDefaultPointColor = 0xFFFFFFFF; // White color for default QuakeWorld points
    uint32_t qwPlayerPointColor = 0xFF0000FF;  // Red color for nearest QuakeWorld location

    float alpha = 0.0f;  // Range between 0.0 (fully transparent) and 1.0 (fully opaque)

    nqDefaultBoxColor = (nqDefaultBoxColor & 0xFFFFFF00) | ((uint8_t)(alpha * 255.0f));

    VectorCopy(cl.entities[cl.viewentity].origin, player_origin);

    if (r_showlocs.value >= 3)
    {
        // Locate the nearest location and only print text
        for (loc = locations; loc; loc = loc->next_loc)
        {
            if (!nqloc) // QuakeWorld location (point)
            {
                vec3_t scaled_mins;
                VectorCopy(loc->mins, scaled_mins);

                // Adjust the scale of the input coordinates to match stored locations
                for (int i = 0; i < 3; i++)
                {
                    scaled_mins[i] /= 8.0f;
                }

                // Calculate squared distance from player position to location point
                VectorSubtract(player_origin, scaled_mins, diff);
                dist = diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2];
            }
            else // NetQuake location (axial box)
            {
                // Calculate Manhattan distance from player to nearest face of the box
                dist = fabs(loc->mins[0] - player_origin[0]) + fabs(loc->maxs[0] - player_origin[0]) +
                    fabs(loc->mins[1] - player_origin[1]) + fabs(loc->maxs[1] - player_origin[1]) +
                    fabs(loc->mins[2] - player_origin[2]) + fabs(loc->maxs[2] - player_origin[2]) - loc->sum;
            }

            // Track the nearest location
            if (!bestloc || dist < bestdist)
            {
                bestdist = dist;
                bestloc = loc;
            }
        }

        // Print the name of the nearest location
        if (bestloc)
        {
            strncpy(loc_str, bestloc->name, sizeof(loc_str));
        }
        else
        {
            strncpy(loc_str, "no locs", sizeof(loc_str));
        }

        return; // Skip drawing
    }

    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    GL_PolygonOffset(OFFSET_SHOWTRIS);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); // johnfitz -- for alpha
    glDisable(GL_ALPHA_TEST); // johnfitz -- for alpha

    for (loc = locations; loc; loc = loc->next_loc)
    {
        if (!nqloc) // QuakeWorld: Nearest point location
        {
            vec3_t scaled_mins;
            VectorCopy(loc->mins, scaled_mins);

            // Adjust the scale of the input coordinates to match stored locations
            for (int i = 0; i < 3; i++)
            {
                scaled_mins[i] /= 8.0f;
            }

            // Calculate squared distance from player position to location point
            VectorSubtract(player_origin, scaled_mins, diff);
            dist = diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2];

            // Draw the point (in white for now)
            R_EmitPin(scaled_mins, 4, qwDefaultPointColor, 16);
        }
        else // NetQuake: Axial box location (rectangular prism)
        {
            // Calculate Manhattan distance from the player to the nearest face of the box
            dist = fabs(loc->mins[0] - player_origin[0]) + fabs(loc->maxs[0] - player_origin[0]) +
                fabs(loc->mins[1] - player_origin[1]) + fabs(loc->maxs[1] - player_origin[1]) +
                fabs(loc->mins[2] - player_origin[2]) + fabs(loc->maxs[2] - player_origin[2]) - loc->sum;

            // Draw the box (in white for now)
            R_EmitWireBox(loc->mins, loc->maxs, nqDefaultBoxColor);
        }

        // Track the best (nearest) location
        if (!bestloc || dist < bestdist)
        {
            bestdist = dist;
            bestloc = loc;
        }
    }

    // After the loop, highlight the nearest location (bestloc)
    if (bestloc)
    {
        if (!nqloc) // QuakeWorld: Highlight the nearest point in red
        {
            vec3_t scaled_mins;
            VectorCopy(bestloc->mins, scaled_mins);

            // Apply scaling to match the stored locations
            for (int i = 0; i < 3; i++)
            {
                scaled_mins[i] /= 8.0f; // Scale down the coordinates
            }
            
            R_EmitPin(scaled_mins, 4, qwPlayerPointColor, 16); // Red color for closest point
        }
        else // NetQuake: Highlight the nearest box in red
        {
            if (bestdist < 0.01f) // If the player is inside the box
            {
                R_EmitWireBox(bestloc->mins, bestloc->maxs, nqPlayerBoxColor); // Red color for the closest box
            }
            else
            {
                R_EmitWireBox(bestloc->mins, bestloc->maxs, nqPlayerBoxColor); // Red color for the closest box
            }
        }

        // Copy the name of the best location to loc_str and print it
        strncpy(loc_str, bestloc->name, sizeof(loc_str));
    }
    else
        strncpy(loc_str, "no locs", sizeof(loc_str));

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    GL_PolygonOffset(OFFSET_NONE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
}

/*
==============
QW LOC Creation Functions (ezquake) -- woods #locext
==============
*/

#define MAX_LOC_NAME		64
#define MAX_MACRO_STRING 	2048
#define MAX_ENTITIES 1024 // Maximum number of entities to store
#define MAX_NAME_LENGTH 64 // Maximum length for the entity name
#define SKIPBLANKS(ptr) while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r') ptr++
#define SKIPTOEOL(ptr) {while (*ptr != '\n' && *ptr != 0) ptr++; if (*ptr == '\n') ptr++;}
char closest_loc_str[64];

typedef struct locdata_s {
    vec3_t coord;
    char* name;
    struct locdata_s* next;
} locdata_t;

static locdata_t* locdata = NULL;
static int loc_count = 0;

typedef struct {
    char name[MAX_NAME_LENGTH]; // Store the name of the entity
    vec3_t position;            // Store the 3D position (x, y, z)
} EntityInfo;

EntityInfo entityArray[MAX_ENTITIES];
int entityCount = 0; // Track the number of entities added

typedef struct {
    char name[MAX_NAME_LENGTH];
    int count; // Track how many times the name has been found
    edict_t* edict; // Store the original edict for later use
} NameTracker;

NameTracker nameTracker[MAX_ENTITIES];
int nameTrackerCount = 0;

/*
================
StoreEntityNameAndLocation -- woods #locext
Stores unique entities' names and positions, omits duplicates, and replaces names with shorter versions afterward
================
*/
void StoreEntityNameAndLocation (void)
{
    extern edict_t* sv_player;
    edict_t* ed;
    int i;
    qcvm_t* oldvm;	//in case we ever draw a scene from within csqc.
    int replacedEntitiesCount = 0; // To track how many entities were replaced and stored

    entityCount = 0;
    nameTrackerCount = 0;

    oldvm = qcvm;
    PR_SwitchQCVM(NULL);
    PR_SwitchQCVM(&sv.qcvm);

    for (i = 1, ed = NEXT_EDICT(qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT(ed))  // First pass: Track name occurrences
    {
        if (ed == sv_player || ed->free)
            continue; // Don't process player's own bbox or freed edicts

        const char* originalName = PR_GetString(ed->v.classname);

        // Track occurrences of each name and store original edict reference
        int found = 0;
        for (int j = 0; j < nameTrackerCount; j++) {
            if (strcmp(nameTracker[j].name, originalName) == 0) {
                nameTracker[j].count++;
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(nameTracker[nameTrackerCount].name, originalName, MAX_NAME_LENGTH - 1);
            nameTracker[nameTrackerCount].name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
            nameTracker[nameTrackerCount].count = 1;
            nameTracker[nameTrackerCount].edict = ed; // Store the edict for later use
            nameTrackerCount++;
        }
    }

    for (int j = 0; j < nameTrackerCount; j++) // Second pass: Store only unique entities with name replacements
    {
        if (nameTracker[j].count > 1)
            continue; // Omit duplicates

        edict_t* storedEdict = nameTracker[j].edict;
        const char* originalName = nameTracker[j].name;
        const char* classname = NULL;

        // Apply name replacements
        if (strcmp(originalName, "item_artifact_super_damage") == 0)
            classname = "quad";
        else if (strcmp(originalName, "item_artifact_envirosuit") == 0)
            classname = "suit";
        else if (strcmp(originalName, "item_artifact_invisibility") == 0)
            classname = "ring";
        else if (strcmp(originalName, "item_artifact_invulnerability") == 0)
            classname = "pent";
        else if (strcmp(originalName, "weapon_lightning") == 0)
            classname = "lg";
        else if (strcmp(originalName, "weapon_grenadelauncher") == 0)
            classname = "gl";
        else if (strcmp(originalName, "weapon_rocketlauncher") == 0)
            classname = "rl";
        else if (strcmp(originalName, "weapon_supernailgun") == 0)
            classname = "sng";
        else if (strcmp(originalName, "weapon_supershotgun") == 0)
            classname = "ssg";
        else if (strcmp(originalName, "weapon_nailgun") == 0)
            classname = "ng";
        else if (strcmp(originalName, "item_flag_team2") == 0)
            classname = "blue-flag";
        else if (strcmp(originalName, "item_flag_team1") == 0)
            classname = "red-flag";
        else if (strcmp(originalName, "info_teleport_destination") == 0)
            classname = "tele-exit";
        else if (strcmp(originalName, "trigger_teleport") == 0)
            classname = "tele";
        else if (strcmp(originalName, "item_health") == 0 && ((int)storedEdict->v.spawnflags & 2))
            classname = "mh";

        if (classname && entityCount < MAX_ENTITIES) // Store the entity's name and position only if a name replacement was applied
        {
            strncpy(entityArray[entityCount].name, classname, MAX_NAME_LENGTH - 1);
            entityArray[entityCount].name[MAX_NAME_LENGTH - 1] = '\0'; // Ensure null termination
            VectorCopy(storedEdict->v.origin, entityArray[entityCount].position);
            entityCount++;
            replacedEntitiesCount++; // Count this replaced entity
        }
    }

    if (replacedEntitiesCount == 0)
        Con_Printf("no auto-locations were added; duplicates get omitted\n");

    PR_SwitchQCVM(NULL);
    PR_SwitchQCVM(oldvm);
}

qboolean LOC_DetectFormat (const char* linetext, const char* lineend, qboolean* hasPrintedFormat) // woods #locext
{
    for (const char* ptr = linetext; ptr < lineend; ptr++)
    {
        if (*ptr == ',') // If we find a comma, it is a NetQuake format.
        {
            if (!(*hasPrintedFormat))
            {
                Con_DPrintf("NetQuake LOC Detected\n");
                *hasPrintedFormat = true;
            }
            return true; // NetQuake format detected
        }
    }

    if (!(*hasPrintedFormat))
    {
        Con_DPrintf("QuakeWorld LOC Detected\n");
        *hasPrintedFormat = true;
    }

    return false; // QuakeWorld format
}

static void TP_ClearLocs (void) // woods #locext
{
    locdata_t* node, * temp;

    for (node = locdata; node; node = temp) {
        free(node->name);
        temp = node->next;
        free(node);
    }

    locdata = NULL;
    loc_count = 0;
}

static void TP_ClearLocs_f (void) // woods #locext
{
    int num_locs = 0;

    if (Cmd_Argc() > 1) {
        Con_Printf("clears all temporary locs in memory from addloc command\n");
        return;
    }

    num_locs = loc_count;

    if (num_locs == 0) {
        Con_Printf("no locs to clear; use ^maddloc^m and ^msaveloc^m to create locs\n");
        return;
    }

    TP_ClearLocs();
    Con_Printf("cleared ^m%d^m locs\n", num_locs);
}

qboolean TP_SaveLocFile (const char* path, qboolean quiet) // woods #locext
{
    locdata_t* node;
    char* buf;
    char locname[MAX_OSPATH];

    // Prevents saving of junk data
    if (!loc_count) {
        Con_Printf("there are no locations to save.\n");
        return false;
    }

    // Make sure we have a path to work with.
    if (!*path) {
        return false;
    }

    // Check if the filename is too long.
    if (strlen(path) > MAX_LOC_NAME) {
        Con_Printf("filename too long. Max allowed is %d characters\n", MAX_LOC_NAME);
        return false;
    }

    // Get the default path for loc-files and make sure the path won't be too long.
    q_strlcpy(locname, "locs/", sizeof(locname));
    if (strlen(path) + strlen(locname) + 2 + 4 > MAX_OSPATH) {
        Con_Printf("path name exceeds MAX_OSPATH\n");
        return false;
    }

    // Add an extension if it doesn't exist already.
    q_strlcat(locname, path, sizeof(locname) - strlen(locname));
    COM_DefaultExtension(locname, ".loc", MAX_OSPATH);

    // Calculate the total buffer size required for the file content.
    // Each location entry can have coordinates and the name, hence (MAX_LOC_NAME + 24).
    buf = (char*)malloc(loc_count * (MAX_LOC_NAME + 24));
    if (!buf) {
        Con_Printf("could not initialize buffer.\n");
        return false;
    }
    buf[0] = '\0'; // Initialize buffer with an empty string

    // Write all the nodes to the buffer.
    node = locdata;
    while (node) {
        char row[2 * MAX_LOC_NAME];
        q_snprintf(row, sizeof(row), "%4d %4d %4d %s\n",
            Q_rint(8 * node->coord[0]),
            Q_rint(8 * node->coord[1]),
            Q_rint(8 * node->coord[2]),
            node->name);
        q_strlcat(buf, row, loc_count * (MAX_LOC_NAME + 24));
        node = node->next;
    }

    // Write buffer contents to the file using COM_WriteFile.
    COM_WriteFile(locname, buf, strlen(buf));

    // Free buffer after writing.
    free(buf);

    if (!quiet) {
        Con_Printf("locations saved to %s.\n", locname);
    }

    return true;
}

void TP_SaveLocFile_f (void) // woods #locext
{
    if (Cmd_Argc() != 2) {
        Con_Printf("saveloc <filename> : save a loc file\n");
        return;
    }
    TP_SaveLocFile(Cmd_Argv(1), false);

    if (strstr(Cmd_Argv(1), cl.mapname)) // Reload the locations after saving
      LOC_LoadLocations ();

}

static void TP_AddLocNode (vec3_t coord, const char* name) // woods #locext
{
    locdata_t* newnode, * node;

    newnode = (locdata_t*)malloc(sizeof(locdata_t));
    newnode->name = strdup(name);
    newnode->next = NULL;
    memcpy(newnode->coord, coord, sizeof(vec3_t));

    if (!locdata) {
        locdata = newnode;
        loc_count++;
        return;
    }

    for (node = locdata; node->next; node = node->next)
        ;

    node->next = newnode;
    loc_count++;
}

void TP_AddLoc (const char* locname) // woods #locext
{
    vec3_t location;

    // We need to be up and running.
    if (cls.state != ca_connected) {
        Con_Printf("need to be active to add a location.\n");
        return;
    }

    VectorCopy(cl.entities[cl.viewentity].origin, location);

    TP_AddLocNode(location, locname);

    Con_Printf("added location \"%s\" at (%4.0f, %4.0f, %4.0f)\n", locname, location[0], location[1], location[2]);
}

static void TP_AddLoc_f (void) // woods #locext
{
     char locname[MAX_LOC_NAME] = {0}; // Assuming the max name length is 256 characters
    
    if (Cmd_Argc() == 2 && strcmp(Cmd_Argv(1), "auto") == 0) // check if the user wants to add locations automatically
    {
        StoreEntityNameAndLocation();

        for (int i = 0; i < entityCount; i++)
        {
            // Add location for each stored entity
            TP_AddLocNode(entityArray[i].position, entityArray[i].name);
            Con_Printf("added location \"%s\" at (%4.0f, %4.0f, %4.0f)\n",
                entityArray[i].name,
                entityArray[i].position[0],
                entityArray[i].position[1],
                entityArray[i].position[2]);
        }
        return;
    }
    
    if (Cmd_Argc() < 2) 
    {
        Con_Printf("addloc <name of location> : add a location\n");
        return;
    }

    // Concatenate all arguments from index 1 onward to form the location name
    for (int i = 1; i < Cmd_Argc(); i++) {
        strcat(locname, Cmd_Argv(i));
        if (i < Cmd_Argc() - 1) {
            strcat(locname, " "); // Add space between arguments
        }
    }

    TP_AddLoc(locname);
}

qboolean TP_LoadLocFile(const char* path, qboolean quiet) // woods #locext
{
    char* buf, * p, locname[MAX_OSPATH] = { 0 }, location[MAX_LOC_NAME];
    int i, n, sign, line, nameindex, overflow;
    vec3_t coord;
    qboolean hasPrintedFormat = false;  // Ensure we only print the format detection once

    if (!*path) {
        return false;
    }

    q_strlcpy(locname, "locs/", sizeof(locname));
    if (strlen(path) + strlen(locname) + 2 + 4 > MAX_OSPATH) {
        Con_Printf("path name > MAX_OSPATH\n");
        return false;
    }

    q_strlcat(locname, path, sizeof(locname) - strlen(locname));
    COM_DefaultExtension(locname, ".loc", MAX_OSPATH);

    if (!(buf = (char*)COM_LoadMallocFile(locname, NULL))) {
        if (!quiet) {
            Con_Printf("could not load %s\n", locname);
        }
        return false;
    }

    TP_ClearLocs();

    // Parse the whole file now
    p = buf;
    line = 1;

    while (1) {
        SKIPBLANKS(p);

        if (!*p) {
            goto _endoffile;
        }
        else if (*p == '\n') {
            p++;
            goto _endofline;
        }
        else if (*p == '/' && p[1] == '/') {
            SKIPTOEOL(p);
            goto _endofline;
        }

        // Detect format based on the first coordinate line
        if (LOC_DetectFormat(p, strchr(p, '\n'), &hasPrintedFormat)) {
            // If NetQuake format is detected, stop loading the file
            Con_Printf("detected netquake format, ^mloadloc^m only supports qw locs\n");
            free(buf);
            return false;
        }

        // parse three ints
        for (i = 0; i < 3; i++) {
            n = 0;
            sign = 1;
            while (1) {
                switch (*p++) {
                case ' ':
                case '\t':
                    goto _next;
                case '-':
                    if (n) {
                        Con_Printf("locfile error (line %d): unexpected '-'\n", line);
                        SKIPTOEOL(p);
                        goto _endofline;
                    }
                    sign = -1;
                    break;
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    n = n * 10 + (p[-1] - '0');
                    break;
                default:	// including eol or eof
                    Con_Printf("locfile error (line %d): couldn't parse coords\n", line);
                    SKIPTOEOL(p);
                    goto _endofline;
                }
            }
        _next:
            n *= sign;
            coord[i] = n / 8.0;

            SKIPBLANKS(p);
        }

        // parse location name
        overflow = nameindex = 0;
        while (1) {
            switch (*p) {
            case '\r':
                p++;
                break;
            case '\n':
            case '\0':
                location[nameindex] = 0;

                // ADD YOUR LOGIC HERE TO MODIFY THE LOCATION NAME
                char* separator;
                while ((separator = strstr(location, "$loc_name_separator")) != NULL)
                {
                    *separator = '-'; // Replace the separator with "-"
                    char* restOfString = separator + strlen("$loc_name_separator");
                    memmove(separator + 1, restOfString, strlen(restOfString) + 1);
                }

                char* locNameInstance;
                while ((locNameInstance = strstr(location, "$loc_name_")) != NULL)
                {
                    size_t prefixLength = strlen("$loc_name_"); // remove the prefix
                    memmove(locNameInstance, locNameInstance + prefixLength, strlen(locNameInstance + prefixLength) + 1);
                }

                char* dollarDot;
                while ((dollarDot = strstr(location, "$.")) != NULL)
                {
                    *dollarDot = '-'; // Replace "$." with "-"
                    memmove(dollarDot + 1, dollarDot + 2, strlen(dollarDot + 2) + 1); // +1 for null terminator
                }

                // ADD LOCATION NODE AFTER MODIFICATIONS
                TP_AddLocNode(coord, location);

                if (*p == '\n')
                    p++;
                goto _endofline;
            default:
                if (nameindex < MAX_LOC_NAME - 1) {
                    location[nameindex++] = *p;
                }
                else if (!overflow) {
                    overflow = 1;
                    Con_Printf("locfile warning (line %d): truncating loc name to %d chars\n", line, MAX_LOC_NAME - 1);
                }
                p++;
            }
        }
    _endofline:
        line++;
    }
_endoffile:

    free(buf);

    if (loc_count) {
        if (!quiet) {
            Con_Printf("loaded locfile \"%s\" (%i loc points)\n", COM_SkipPath(locname), loc_count); // loc_numentries);
        }
    }
    else {
        TP_ClearLocs();
        if (!quiet) {
            Con_Printf("locfile \"%s\" was empty\n", COM_SkipPath(locname));
        }
    }

    return true;
}


void TP_LoadLocFile_f (void) // woods #locext
{
    if (Cmd_Argc() != 2) {
        Con_Printf("loadloc <filename> : load a loc file\n");
        return;
    }
    TP_LoadLocFile(Cmd_Argv(1), false);
}

static void TP_RemoveClosestLoc (vec3_t location) // woods #locext
{
    float dist, mindist;
    vec3_t vec;
    locdata_t* node, * best, * previous, * best_previous;

    best_previous = previous = best = NULL;
    mindist = 0;

    // Find the closest loc.
    node = locdata;
    while (node) {
        // Get the distance to the loc.
        VectorSubtract(location, node->coord, vec);
        dist = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];

        // Check if it's closer than the previously best.
        if (!best || dist < mindist) {
            best_previous = previous;
            best = node;
            mindist = dist;
        }

        // Advance and save the previous node.
        previous = node;
        node = node->next;
    }

    if (!best) {
        Con_Printf("there is no locations left for deletion!\n");
        return;
    }

    Con_Printf("removed location \"%s\" at (%4.0f, %4.0f, %4.0f)\n", best->name, best->coord[0], best->coord[1], best->coord[2]);

    // If the node we're trying to delete has a
    // next node attached to it, copy the data from
    // that node into the node we're deleting, and then
    // delete the next node instead.
    if (best->next) {
        locdata_t* temp;

        // Copy the data from the next node into the one we're deleting.
        VectorCopy(best->next->coord, best->coord);

        free(best->name);
        best->name = (char*)malloc(strlen(best->next->name) + 1);
        strcpy(best->name, best->next->name);

        // Save the pointer to the next node.
        temp = best->next->next;

        // Free the current next node.
        free(best->next->name);
        free(best->next);

        // Set the pointer to the next node.
        best->next = temp;
    }
    else {
        // Free the current node.
        free(best->name);
        free(best);
        best = NULL;

        // Make sure the previous node doesn't point to garbage.
        if (best_previous != NULL) {
            best_previous->next = NULL;
        }
    }

    // Decrease the loc count.
    loc_count--;

    // If this was the last loc, remove the entire node list.
    if (loc_count <= 0) {
        locdata = NULL;
    }
}

static void TP_RemoveLoc_f (void) // woods #locext
{
    if (Cmd_Argc() == 1) {

            TP_RemoveClosestLoc(cl.entities[cl.viewentity].origin);
    }
    else {
        Con_Printf("removeloc : remove the closest location\n");
        return;
    }
}

void TP_DrawLocsWithWirePoints (void) // woods #locext
{
    locdata_t* node;
    vec3_t player_origin, vec;
    float dist, mindist = FLT_MAX;
    locdata_t* closest_node = NULL;


    if (!r_showlocs.value || cl.maxclients > 1 || !sv.active || cls.signon < SIGNONS)
        return;

    VectorCopy(cl.entities[cl.viewentity].origin, player_origin);

    for (node = locdata; node; node = node->next) // Iterate through all locs and find the closest one
    {
        VectorSubtract(player_origin, node->coord, vec); // Calculate the squared distance from the player origin to the loc
        dist = vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2];

        if (dist < mindist) // Keep track of the closest location
        {
            mindist = dist;
            closest_node = node;            
        }
    }

    if (closest_node)
        strncpy(closest_loc_str, closest_node->name, sizeof(closest_loc_str) - 1);
    else
        strncpy(closest_loc_str, "no locs, use addloc or loadloc", sizeof(closest_loc_str) - 1);

    for (node = locdata; node; node = node->next) // Now draw wireframe points for all locations
    {
        uint32_t color;

        // If this is the closest node, set the color to red (full red with alpha 1)
        if (node == closest_node) {
            color = 0xFF0000FF;  // Red with full opacity
        }
        else {
            color = 0xffffffff;  // White with full opacity
        }

        glDisable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        GL_PolygonOffset(OFFSET_SHOWTRIS);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_CULL_FACE);

        R_EmitPin(node->coord, 4, color, 16); // Emit a wireframe point for this location

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        GL_PolygonOffset(OFFSET_NONE);
        glEnable(GL_DEPTH_TEST);

    }
}

void TP_DrawClosestLocText (void) // woods #locext
{
    if (!r_showlocs.value || cls.signon < SIGNONS)
        return;
    
    GL_SetCanvas(CANVAS_CROSSHAIR2); // Set the canvas where the string will be drawn

    char str[64]; // woods added padding
    
    int x = 0; // X position of the string
    int y = r_showlocs_y.value; // Y position of the string
    if (r_showlocs.value >= 2)
        sprintf(str, "%s", loc_str); // Copy the name of the location to a string)
    else
        sprintf(str, "%s", closest_loc_str); // Copy the name of the location to a string)

    int string_width = strlen(str) * 8; // each character is 8 pixels wide

    Draw_FillPlayer(x - (string_width / 2) - 2, y - 1, string_width + 4, 11, CL_PLColours_Parse("0x000000"), r_showlocs.value - 3);

    Draw_String(x - (string_width / 2), y, str);
}

void TP_LocFiles_Init (void) // woods #locext
{
    Cmd_AddCommand("addloc", TP_AddLoc_f);
    Cmd_AddCommand("saveloc", TP_SaveLocFile_f);
    Cmd_AddCommand("clearlocs", TP_ClearLocs_f);
    Cmd_AddCommand("loadloc", TP_LoadLocFile_f);
    Cmd_AddCommand("removeloc", TP_RemoveLoc_f);
}
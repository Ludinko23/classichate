#ifndef CC_CCPSSERVERS_H
#define CC_CCPSSERVERS_H
#include "Core.h"
#include "String.h"
CC_BEGIN_HEADER

/*
  Manages custom private server list providers (CCPS).
  Servers are stored in ./ccps_servers.txt (pipe-delimited).
  The active server ONLY affects the server list API endpoint.
  Login, auth, and flag CDN always use classicube.net defaults.
*/

#define CCPS_MAX_SERVERS 8

struct CCPSServer {
	char displayName[64];
	char url[256];   /* API base URL, e.g. https://example.com/api */
	char port[8];    /* Stored metadata only, not used for API calls */
};

/* All saved servers */
extern struct CCPSServer CCPS_Servers[CCPS_MAX_SERVERS];
/* How many servers are saved */
extern int CCPS_ServerCount;
/* Index of active server, or -1 for default ClassiCube */
extern int CCPS_ActiveServer;

/* Loads servers from ccps_servers.txt and restores active server from options */
void CCPS_LoadServers(void);
/* Saves servers to ccps_servers.txt and persists active server index */
void CCPS_SaveServers(void);

/* Adds a new server. Returns false if at max capacity. */
cc_bool CCPS_AddServer(const cc_string* name, const cc_string* url, const cc_string* port);
/* Deletes the server at the given index. Adjusts active server if needed. */
void CCPS_DeleteServer(int index);
/* Updates the server at the given index with new values. */
void CCPS_UpdateServer(int index, const cc_string* name, const cc_string* url, const cc_string* port);
/* Sets the active server index (-1 = default ClassiCube). Saves immediately. */
void CCPS_SetActive(int index);

/*
  Returns the base URL of the active custom server, or NULL if default ClassiCube is active.
  Only used to override the /servers API endpoint - all other API calls remain on classicube.net.
*/
const char* CCPS_GetActiveBaseURL(void);

CC_END_HEADER
#endif

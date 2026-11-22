#include "CCPSServers.h"
#include "String.h"
#include "Stream.h"
#include "Platform.h"
#include "Options.h"
#include "Logger.h"

struct CCPSServer CCPS_Servers[CCPS_MAX_SERVERS];
int CCPS_ServerCount;
int CCPS_ActiveServer = -1;

#define CCPS_FILE        "ccps_servers.txt"
#define CCPS_ACTIVE_KEY  "ccps-active-server"

static void CCPS_ParseLine(const cc_string* line) {
	cc_string parts[3];
	struct CCPSServer* s;
	int count;

	if (CCPS_ServerCount >= CCPS_MAX_SERVERS) return;
	/* Skip blank lines */
	if (!line->length) return;

	count = String_UNSAFE_Split(line, '|', parts, 3);
	if (count < 2) return;

	s = &CCPS_Servers[CCPS_ServerCount++];
	String_CopyToRaw(s->displayName, sizeof(s->displayName), &parts[0]);
	String_CopyToRaw(s->url,         sizeof(s->url),         &parts[1]);
	if (count >= 3)
		String_CopyToRaw(s->port, sizeof(s->port), &parts[2]);
	else
		s->port[0] = '\0';
}

void CCPS_LoadServers(void) {
	cc_string path = String_FromConst(CCPS_FILE);
	cc_string line; char lineBuffer[512];
	struct Stream stream;
	cc_result res;

	CCPS_ServerCount  = 0;
	CCPS_ActiveServer = Options_GetInt(CCPS_ACTIVE_KEY, -1, CCPS_MAX_SERVERS - 1, -1);

	res = Stream_OpenFile(&stream, &path);
	if (res == ReturnCode_FileNotFound) return;
	if (res) { Logger_SysWarn(res, "opening " CCPS_FILE); return; }

	String_InitArray(line, lineBuffer);
	while (!Stream_ReadLine(&stream, &line)) {
		CCPS_ParseLine(&line);
		line.length = 0;
	}
	stream.Close(&stream);

	/* Clamp: if saved index is now out of range, reset to default */
	if (CCPS_ActiveServer >= CCPS_ServerCount)
		CCPS_ActiveServer = -1;
}

void CCPS_SaveServers(void) {
	cc_string path = String_FromConst(CCPS_FILE);
	cc_string line; char lineBuffer[512];
	struct Stream stream;
	cc_result res;
	int i;

	res = Stream_CreateFile(&stream, &path);
	if (res) { Logger_SysWarn(res, "saving " CCPS_FILE); return; }

	for (i = 0; i < CCPS_ServerCount; i++) {
		String_InitArray(line, lineBuffer);
		String_AppendConst(&line, CCPS_Servers[i].displayName);
		String_Append(&line, '|');
		String_AppendConst(&line, CCPS_Servers[i].url);
		String_Append(&line, '|');
		String_AppendConst(&line, CCPS_Servers[i].port);
		Stream_WriteLine(&stream, &line);
	}
	stream.Close(&stream);

	Options_SetInt(CCPS_ACTIVE_KEY, CCPS_ActiveServer);
	Options_SaveIfChanged();
}

cc_bool CCPS_AddServer(const cc_string* name, const cc_string* url, const cc_string* port) {
	struct CCPSServer* s;
	if (CCPS_ServerCount >= CCPS_MAX_SERVERS) return false;

	s = &CCPS_Servers[CCPS_ServerCount++];
	String_CopyToRaw(s->displayName, sizeof(s->displayName), name);
	String_CopyToRaw(s->url,         sizeof(s->url),         url);
	String_CopyToRaw(s->port,        sizeof(s->port),        port);
	CCPS_SaveServers();
	return true;
}

void CCPS_DeleteServer(int index) {
	int i;
	if (index < 0 || index >= CCPS_ServerCount) return;

	for (i = index; i < CCPS_ServerCount - 1; i++)
		CCPS_Servers[i] = CCPS_Servers[i + 1];
	CCPS_ServerCount--;

	if (CCPS_ActiveServer == index)
		CCPS_ActiveServer = -1;
	else if (CCPS_ActiveServer > index)
		CCPS_ActiveServer--;

	CCPS_SaveServers();
}

void CCPS_UpdateServer(int index, const cc_string* name, const cc_string* url, const cc_string* port) {
	struct CCPSServer* s;
	if (index < 0 || index >= CCPS_ServerCount) return;

	s = &CCPS_Servers[index];
	String_CopyToRaw(s->displayName, sizeof(s->displayName), name);
	String_CopyToRaw(s->url,         sizeof(s->url),         url);
	String_CopyToRaw(s->port,        sizeof(s->port),        port);
	CCPS_SaveServers();
}

void CCPS_SetActive(int index) {
	CCPS_ActiveServer = index;
	CCPS_SaveServers();
}

const char* CCPS_GetActiveBaseURL(void) {
	if (CCPS_ActiveServer < 0 || CCPS_ActiveServer >= CCPS_ServerCount)
		return NULL;
	return CCPS_Servers[CCPS_ActiveServer].url;
}

#ifndef CC_FRIENDS_H
#define CC_FRIENDS_H

#include "String.h"

struct FriendEntry {
	cc_string name;
	cc_string server;
	int status;
};

struct FriendRequestEntry {
	char nameBuf[64];
};

enum {
	FRIEND_OFFLINE = 0,
	FRIEND_ONLINE  = 1
};

static const int Friends_ListVersion = 0;
static const int Friends_Count       = 0;
static const int Friends_RequestVersion = 0;
static const int Friends_RequestCount   = 0;

static struct FriendEntry Friends_List[1];
static struct FriendRequestEntry Friends_Requests[1];

static void Friends_Tick(void) { }
static cc_bool Friends_HasUsername(void) { return false; }
static void Friends_Login(const cc_string* username) { (void)username; }
static void Friends_FetchList(void) { }
static void Friends_Remove(const cc_string* name) { (void)name; }
static void Friends_SendRequest(const cc_string* name) { (void)name; }
static void Friends_FetchRequests(void) { }
static void Friends_AcceptRequest(const cc_string* name) { (void)name; }
static void Friends_DeclineRequest(const cc_string* name) { (void)name; }

#endif

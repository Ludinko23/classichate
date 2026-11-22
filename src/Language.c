#include "Language.h"
#include "Platform.h"
#include "Stream.h"
#include "String_.h"
#include "Options.h"
#include "Utils.h"
#include "Funcs.h"

#define OPT_LANGUAGE   "language"
#define LANG_FOLDER    "languages"
#define LANG_MAX       32
#define LANG_KEY_NAME  "_name"

/*########################################################################################################################*
*---------------------------------------------------------JSON parsing----------------------------------------------------*
*#########################################################################################################################*/
typedef void (*Lang_OnPair)(const char* key, const char* value);

static cc_bool Lang_Equals(const char* a, const char* b) {
	for (; *a && *a == *b; a++, b++) { }
	return *a == *b;
}

static void Lang_CopyRaw(char* dst, int capacity, const char* src) {
	int i = 0, n;
	while (src[i]) 
	{
		n = src[i] == STRING_UNI_LEAD ? STRING_UNI_BYTES : 1; /* don't split an escape */
		if (i + n > capacity - 1) break;
		Mem_Copy(dst + i, src + i, n);
		i += n;
	}
	dst[i] = '\0';
}

static int Lang_HexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Stores the codepoint the same way chat does (see String_AppendCodepoint): */
/* ASCII as a single byte, anything else as a 4 byte escape. Needs 4 bytes of room. */
static void Lang_PutCodepoint(char** dst, cc_codepoint cp) {
	cc_string tmp = String_Init(*dst, 0, STRING_UNI_BYTES);
	int len = String_AppendCodepoint(&tmp, cp);

	if (len) { *dst += len; } else { *(*dst)++ = '?'; } /* invalid, e.g. control chars */
}

static cc_bool Lang_ReadHex4(const char** cur, const char* end, cc_codepoint* cp) {
	const char* p = *cur;
	int i, h; cc_codepoint v = 0;
	for (i = 0; i < 4; i++, p++) 
	{
		if (p >= end || (h = Lang_HexDigit(*p)) < 0) return false;
		v = (v << 4) | h;
	}
	*cp = v; *cur = p;
	return true;
}

/* Reads a JSON string starting at the opening quote. Writes decoded text to dst (NUL terminated) */
/* Returns pointer after the closing quote, or NULL on error */
static const char* Lang_ReadString(const char* cur, const char* end, char* dst) {
	cc_codepoint cp, low;
	int i, len;

	if (cur >= end || *cur != '"') return NULL;
	cur++;

	while (cur < end && *cur != '"') 
	{
		if (*cur == '\\') {
			cur++;
			if (cur >= end) return NULL;
			switch (*cur) 
			{
			case 'n': *dst++ = '\n'; cur++; break;
			case 't': *dst++ = '\t'; cur++; break;
			case 'r': cur++; break;
			case 'u':
				cur++;
				if (!Lang_ReadHex4(&cur, end, &cp)) return NULL;
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					/* high surrogate, should be followed by \uDC00-\uDFFF */
					const char* next = cur;
					if (next + 1 < end && next[0] == '\\' && next[1] == 'u') {
						next += 2;
						if (Lang_ReadHex4(&next, end, &low) && low >= 0xDC00 && low <= 0xDFFF) {
							cp  = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
							cur = next;
						}
					}
				}
				Lang_PutCodepoint(&dst, cp);
				break;
			default: *dst++ = *cur++; break; /* \" \\ \/ */
			}
		} else if ((cc_uint8)*cur < 0x80) {
			*dst++ = *cur++;
		} else {
			len = (int)(end - cur);
			i   = Convert_Utf8ToCodepoint(&cp, (const cc_uint8*)cur, len);
			if (i <= 0) { cp = '?'; i = 1; }
			Lang_PutCodepoint(&dst, cp);
			cur += i;
		}
	}
	if (cur >= end) return NULL;
	*dst = '\0';
	return cur + 1;
}

static const char* Lang_SkipSpace(const char* cur, const char* end) {
	while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n')) cur++;
	return cur;
}

/* Parses a flat { "key": "value", ... } object. Non-string values are ignored. */
static cc_bool Lang_ParseJson(const char* data, int len, Lang_OnPair onPair) {
	const char* cur = data;
	const char* end = data + len;
	char* key; char* val;
	cc_bool ok = true;

	/* Skip UTF-8 BOM */
	if (len >= 3 && (cc_uint8)data[0] == 0xEF && (cc_uint8)data[1] == 0xBB && (cc_uint8)data[2] == 0xBF) cur += 3;

	/* Decoded text is at most twice as long as the raw text (2 byte UTF-8 -> 4 byte escape) */
	key = (char*)Mem_TryAlloc(len * 2 + 1, 2);
	if (!key) return false;
	val = key + (len * 2 + 1);

	cur = Lang_SkipSpace(cur, end);
	if (cur >= end || *cur != '{') { Mem_Free(key); return false; }
	cur++;

	for (;;) 
	{
		cur = Lang_SkipSpace(cur, end);
		if (cur >= end) { ok = false; break; }
		if (*cur == '}') break;
		if (*cur == ',') { cur++; continue; }

		cur = Lang_ReadString(cur, end, key);
		if (!cur) { ok = false; break; }
		cur = Lang_SkipSpace(cur, end);
		if (cur >= end || *cur != ':') { ok = false; break; }
		cur = Lang_SkipSpace(cur + 1, end);
		if (cur >= end) { ok = false; break; }

		if (*cur == '"') {
			cur = Lang_ReadString(cur, end, val);
			if (!cur) { ok = false; break; }
			onPair(key, val);
		} else {
			/* skip number/bool/null */
			while (cur < end && *cur != ',' && *cur != '}') cur++;
		}
	}
	Mem_Free(key);
	return ok;
}

/* Appends a codepoint to dst as UTF-8, returns number of bytes written */
static int Lang_EncodeUtf8(char* dst, cc_codepoint cp) {
	if (cp < 0x80)    { dst[0] = (char)cp; return 1; }
	if (cp < 0x800)   { dst[0] = (char)(0xC0 | (cp >> 6));  dst[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
	if (cp < 0x10000) { dst[0] = (char)(0xE0 | (cp >> 12)); dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); 
	                    dst[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
	dst[0] = (char)(0xF0 | (cp >> 18)); dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); dst[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

/* Whether the file is UTF-16LE: either has a FF FE byte order mark, */
/* or has no BOM but starts with '{' followed by a zero byte (e.g. "{\0") */
static cc_bool Lang_IsUtf16LE(const char* data, int len) {
	if (len < 2) return false;
	if ((cc_uint8)data[0] == 0xFF && (cc_uint8)data[1] == 0xFE) return true;
	return data[0] == '{' && data[1] == '\0';
}

/* Converts UTF-16LE text to UTF-8. Frees the old buffer and returns the new one (or NULL on failure) */
static char* Lang_Utf16LEToUtf8(char* data, int* len) {
	const cc_uint8* src = (const cc_uint8*)data;
	int units = *len / 2, i = 0, n = 0;
	cc_codepoint cp, lo;
	char* out;

	/* Each 16 bit unit needs at most 3 bytes of UTF-8 */
	out = (char*)Mem_TryAlloc(units * 3 + 1, 1);
	if (!out) { Mem_Free(data); return NULL; }

	if (units && src[0] == 0xFF && src[1] == 0xFE) i = 1; /* skip BOM */

	for (; i < units; i++)
	{
		cp = src[i * 2] | (src[i * 2 + 1] << 8);

		if (cp >= 0xD800 && cp <= 0xDBFF) {
			/* high surrogate - must be followed by a low surrogate */
			lo = (i + 1 < units) ? (src[(i + 1) * 2] | (src[(i + 1) * 2 + 1] << 8)) : 0;
			if (lo >= 0xDC00 && lo <= 0xDFFF) {
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				i++;
			} else cp = '?';
		} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
			cp = '?'; /* lone low surrogate */
		}
		n += Lang_EncodeUtf8(out + n, cp);
	}
	out[n] = '\0';
	Mem_Free(data);
	*len = n;
	return out;
}

static char* Lang_ReadFile(const cc_string* path, int* len) {
	struct Stream s;
	cc_uint32 size = 0;
	char* data;
	cc_result res;

	if (Stream_OpenFile(&s, path)) return NULL;
	res = s.Length(&s, &size);
	if (res || !size || size > 512 * 1024) { s.Close(&s); return NULL; }

	data = (char*)Mem_TryAlloc(size + 1, 1);
	if (!data) { s.Close(&s); return NULL; }

	res = Stream_Read(&s, (cc_uint8*)data, size);
	s.Close(&s);
	if (res) { Mem_Free(data); return NULL; }

	data[size] = '\0';
	*len = (int)size;

	/* Language files can be saved as either UTF-8 or UTF-16LE */
	if (Lang_IsUtf16LE(data, *len)) data = Lang_Utf16LEToUtf8(data, len);
	return data;
}


/*########################################################################################################################*
*-------------------------------------------------------Language list-----------------------------------------------------*
*#########################################################################################################################*/
static char lang_codes[LANG_MAX][24];
static char lang_names[LANG_MAX][40];
static int  lang_count;
static int  lang_cur = -1; /* -1 is built-in English */

static char lang_nameTmp[40];
static void Lang_OnPairName(const char* key, const char* val) {
	if (!Lang_Equals(key, LANG_KEY_NAME)) return;
	Lang_CopyRaw(lang_nameTmp, sizeof(lang_nameTmp), val);
}

static void Lang_FilterFile(const cc_string* path, void* obj, int isDirectory) {
	static const cc_string ext = String_FromConst(".json");
	cc_string name = *path;
	cc_string path_ = *path;
	char* data; int len;

	if (isDirectory || lang_count >= LANG_MAX) return;
	if (!String_CaselessEnds(path, &ext)) return;

	Utils_UNSAFE_GetFilename(&name);
	if (name.buffer[0] == '_') return; /* e.g. _template.json is not a language */
	name.length -= ext.length;
	if (!name.length || name.length >= (int)sizeof(lang_codes[0])) return;

	/* use the "_name" entry as the display name, falling back to the file name */
	String_CopyToRaw(lang_names[lang_count], sizeof(lang_names[0]), &name);
	data = Lang_ReadFile(&path_, &len);
	if (data) {
		lang_nameTmp[0] = '\0';
		Lang_ParseJson(data, len, Lang_OnPairName);
		if (lang_nameTmp[0]) Lang_CopyRaw(lang_names[lang_count], sizeof(lang_names[0]), lang_nameTmp);
		Mem_Free(data);
	}
	String_CopyToRaw(lang_codes[lang_count], sizeof(lang_codes[0]), &name);
	lang_count++;
}


/*########################################################################################################################*
*--------------------------------------------------------Lookup table-----------------------------------------------------*
*#########################################################################################################################*/
static char**   lang_keys;   /* pointers into lang_blob */
static char**   lang_vals;
static int*     lang_slots;  /* open addressing hash table, stores entry index + 1 */
static int      lang_slotMask, lang_entries, lang_maxEntries;
static char*    lang_blob;
static char*    lang_blobCur;

static cc_uint32 Lang_Hash(const char* s) {
	cc_uint32 h = 2166136261u;
	for (; *s; s++) { h ^= (cc_uint8)*s; h *= 16777619u; }
	return h;
}

static void Lang_AddPair(const char* key, const char* val) {
	cc_uint32 slot;
	int klen, vlen;
	if (lang_entries >= lang_maxEntries) return;
	if (key[0] == '_') return; /* meta entries like "_name" */
	if (!val[0]) return;       /* empty translation = not translated yet, keep English */

	klen = String_Length(key) + 1;
	vlen = String_Length(val) + 1;
	Mem_Copy(lang_blobCur, key, klen); lang_keys[lang_entries] = lang_blobCur; lang_blobCur += klen;
	Mem_Copy(lang_blobCur, val, vlen); lang_vals[lang_entries] = lang_blobCur; lang_blobCur += vlen;

	slot = Lang_Hash(key) & lang_slotMask;
	while (lang_slots[slot]) slot = (slot + 1) & lang_slotMask;
	lang_slots[slot] = ++lang_entries;
}

static void Lang_Unload(void) {
	Mem_Free(lang_blob);  lang_blob  = NULL;
	Mem_Free(lang_keys);  lang_keys  = NULL;
	Mem_Free(lang_vals);  lang_vals  = NULL;
	Mem_Free(lang_slots); lang_slots = NULL;
	lang_entries = 0;
}

static void Lang_LoadFile(const char* code) {
	cc_string path; char pathBuffer[FILENAME_SIZE];
	char* data; int len, slots;

	String_InitArray(path, pathBuffer);
	String_Format1(&path, LANG_FOLDER "/%c.json", code);

	data = Lang_ReadFile(&path, &len);
	if (!data) return;

	/* Worst case, each pair needs at least 6 raw characters: "a":"b", */
	lang_maxEntries = len / 6 + 1;
	for (slots = 16; slots < lang_maxEntries * 2; slots <<= 1) { }
	lang_slotMask = slots - 1;

	lang_keys  = (char**)Mem_TryAllocCleared(lang_maxEntries, sizeof(char*));
	lang_vals  = (char**)Mem_TryAllocCleared(lang_maxEntries, sizeof(char*));
	lang_slots = (int*)  Mem_TryAllocCleared(slots, sizeof(int));
	lang_blob  = (char*) Mem_TryAlloc(len * 2 + lang_maxEntries * 2 + 16, 1);

	if (lang_keys && lang_vals && lang_slots && lang_blob) {
		lang_blobCur = lang_blob;
		Lang_ParseJson(data, len, Lang_AddPair);
	} else {
		Lang_Unload();
	}
	Mem_Free(data);
}

const char* Lang_Get(const char* english) {
	cc_uint32 slot;
	int idx;
	if (!lang_entries || !english) return english;

	slot = Lang_Hash(english) & lang_slotMask;
	while ((idx = lang_slots[slot]) != 0) 
	{
		if (Lang_Equals(lang_keys[idx - 1], english))
			return lang_vals[idx - 1];
		slot = (slot + 1) & lang_slotMask;
	}
	return english;
}


/*########################################################################################################################*
*---------------------------------------------------------Public API------------------------------------------------------*
*#########################################################################################################################*/
static void Lang_Select(int index) {
	Lang_Unload();
	lang_cur = index;
	if (index >= 0) Lang_LoadFile(lang_codes[index]);
	/* a language file that failed to load behaves like English */
	if (index >= 0 && !lang_entries) lang_cur = -1;
}

cc_bool Lang_Translate(const cc_string* src, cc_string* dst) {
	char tmp[256]; const char* res; cc_string tr;
	if (!lang_entries || src->length <= 0 || src->length >= (int)sizeof(tmp)) return false;

	Mem_Copy(tmp, src->buffer, src->length);
	tmp[src->length] = '\0';

	res = Lang_Get(tmp);
	if (res == tmp) return false; /* no translation found */

	tr = String_FromReadonly(res);
	String_AppendString(dst, &tr);
	return true;
}

void Lang_AppendStored(cc_string* dst, const char* stored) {
	/* Stored text is already in chat's format (see Lang_PutCodepoint), so it must NOT go through */
	/* String_AppendConst (which re-encodes each byte as CP437) or String_AppendUtf8 (wrong format). */
	/* String_AppendString keeps the 4 byte unicode escapes intact. */
	cc_string src = String_FromReadonly(stored);
	String_AppendString(dst, &src);
}

void Lang_AppendTo(cc_string* dst, const char* english) {
	Lang_AppendStored(dst, Lang_Get(english));
}

const char* Lang_CurrentName(void) {
	return lang_cur < 0 ? "English" : lang_names[lang_cur];
}

void Lang_SetNext(void) {
	cc_string value;
	int next = lang_cur + 1;
	if (next >= lang_count) next = -1;
	Lang_Select(next);

	value = String_FromReadonly(lang_cur < 0 ? "" : lang_codes[lang_cur]);
	Options_Set(OPT_LANGUAGE, &value);
}

void Lang_Init(void) {
	cc_string saved; char savedBuffer[STRING_SIZE];
	static const cc_string folder = String_FromConst(LANG_FOLDER);
	int i;

	Utils_EnsureDirectory(LANG_FOLDER);
	lang_count = 0;
	Directory_Enum(&folder, NULL, Lang_FilterFile);

	String_InitArray(saved, savedBuffer);
	Options_Get(OPT_LANGUAGE, &saved, "");
	for (i = 0; i < lang_count; i++)
	{
		if (!String_CaselessEqualsConst(&saved, lang_codes[i])) continue;
		Lang_Select(i);
		return;
	}
}

void Lang_Free(void) {
	Lang_Unload();
	lang_cur = -1;
}

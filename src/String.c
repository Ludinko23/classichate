#include "String_.h"
#include "Funcs.h"
#include "Logger.h"
#include "Platform.h"
#include "Stream.h"
#include "Utils.h"

#ifdef __cplusplus
const cc_string String_Empty = { NULL, 0, 0 };
#else
const cc_string String_Empty;
#endif

int String_CalcLen(const char* raw, int capacity) {
	int length = 0;
	while (length < capacity && *raw) { raw++; length++; }
	return length;
}

int String_Length(const char* raw) {
	int length = 0;
	while (length < UInt16_MaxValue && *raw) { raw++; length++; }
	return length;
}

cc_string String_FromRaw(STRING_REF char* buffer, int capacity) {
	return String_Init(buffer, String_CalcLen(buffer, capacity), capacity);
}

cc_string String_FromReadonly(STRING_REF const char* buffer) {
	int len = String_Length(buffer);
	return String_Init((char*)buffer, len, len);
}


void String_Copy(cc_string* dst, const cc_string* src) {
	dst->length = 0;
	String_AppendString(dst, src);
}

int String_CopyToRaw(char* dst, int capacity, const cc_string* src) {
	int i, len = String_SplitPoint(src, min(capacity, src->length));
	for (i = 0; i < len; i++) dst[i] = src->buffer[i];

	/* add \0 to mark end of used portion of buffer */
	if (len < capacity) dst[len] = '\0';
	return len;
}

cc_string String_UNSAFE_Substring(STRING_REF const cc_string* str, int offset, int length) {
	if (offset < 0 || offset > str->length) {
		Process_Abort("Offset for substring out of range");
	}
	if (length < 0 || length > str->length) {
		Process_Abort("Length for substring out of range");
	}
	if (offset + length > str->length) {
		Process_Abort("Result substring is out of range");
	}
	return String_Init(str->buffer + offset, length, length);
}

cc_string String_UNSAFE_SubstringAt(STRING_REF const cc_string* str, int offset) {
	cc_string sub;
	if (offset < 0 || offset > str->length) Process_Abort("Sub offset out of range");

	sub.buffer   = str->buffer + offset;
	sub.length   = str->length - offset;
	sub.capacity = str->length - offset; /* str->length to match String_UNSAFE_Substring */
	return sub;
}

int String_UNSAFE_Split(STRING_REF const cc_string* str, char c, cc_string* subs, int maxSubs) {
	int beg = 0, end, count, i;

	for (i = 0; i < maxSubs && beg <= str->length; i++) 
	{
		end = String_IndexOfAt(str, beg, c);
		if (end == -1 || i == (maxSubs - 1)) end = str->length;

		subs[i] = String_UNSAFE_Substring(str, beg, end - beg);
		beg = end + 1;
	}

	count = i;
	/* If not enough split substrings, make remaining NULL */
	for (; i < maxSubs; i++) { subs[i] = String_Empty; }
	return count;
}

void String_UNSAFE_SplitBy(STRING_REF cc_string* str, char c, cc_string* part) {
	int idx = String_IndexOf(str, c);
	if (idx == -1) {
		*part = *str;
		*str  = String_Empty;
	} else {
		*part = String_UNSAFE_Substring(str, 0, idx); idx++;
		*str  = String_UNSAFE_SubstringAt(str, idx);
	}
}

int String_UNSAFE_Separate(STRING_REF const cc_string* str, char c, cc_string* key, cc_string* value) {
	int idx = String_IndexOf(str, c);
	if (idx == -1) {
		*key   = *str;
		*value = String_Empty;
		return false;
	}

	*key   = String_UNSAFE_Substring(str, 0, idx); idx++;
	*value = String_UNSAFE_SubstringAt(str, idx);

	/* Trim key [c] value to just key[c]value */
	String_UNSAFE_TrimEnd(key);
	String_UNSAFE_TrimStart(value);
	return key->length > 0 && value->length > 0;
}


int String_Equals(const cc_string* a, const cc_string* b) {
	return a->length == b->length && Mem_Equal(a->buffer, b->buffer, a->length);
} 

int String_CaselessEquals(const cc_string* a, const cc_string* b) {
	int i;
	char aCur, bCur;
	if (a->length != b->length) return false;

	for (i = 0; i < a->length; i++) {
		aCur = a->buffer[i]; Char_MakeLower(aCur);
		bCur = b->buffer[i]; Char_MakeLower(bCur);
		if (aCur != bCur) return false;
	}
	return true;
}

int String_CaselessEqualsConst(const cc_string* a, const char* b) {
	int i;
	char aCur, bCur;

	for (i = 0; i < a->length; i++) {
		aCur = a->buffer[i]; Char_MakeLower(aCur);
		bCur = b[i];         Char_MakeLower(bCur);
		if (aCur != bCur || bCur == '\0') return false;
	}
	/* ensure at end of string */
	return b[a->length] == '\0';
}


static void String_AppendRaw(cc_string* str, char c) {
	if (str->length == str->capacity) return;
	str->buffer[str->length++] = c;
}

void String_Append(cc_string* str, char c) {
#if _MSC_VER && _DEBUG
	if (str->length == 0xCCCC && str->capacity == 0xCCCC)
		Process_Abort("String must be initialised before calling String_Append");
#endif
	if ((cc_uint8)c >= 0x80 || (cc_uint8)c == (cc_uint8)STRING_UNI_LEAD) {
		String_AppendCodepoint(str, Convert_CP437ToUnicode(c));
		return;
	}
	String_AppendRaw(str, c);
}

void String_AppendBool(cc_string* str, cc_bool value) {
	String_AppendConst(str, value ? "True" : "False");
}

int String_MakeUInt32(cc_uint32 num, char* digits) {
	int len = 0;
	do {
		digits[len] = '0' + (num % 10); num /= 10; len++;
	} while (num > 0);
	return len;
}

void String_AppendInt(cc_string* str, int num) {
	if (num < 0) {
		num = -num;
		String_Append(str, '-');
	}
	String_AppendUInt32(str, (cc_uint32)num);
}

void String_AppendUInt32(cc_string* str, cc_uint32 num) {
	char digits[STRING_INT_CHARS];
	int i, count = String_MakeUInt32(num, digits);

	for (i = count - 1; i >= 0; i--) {
		String_Append(str, digits[i]);
	}
}

void String_AppendPaddedInt(cc_string* str, int num, int minDigits) {
	char digits[STRING_INT_CHARS];
	int i, count;
	for (i = 0; i < minDigits; i++) { digits[i] = '0'; }

	count = String_MakeUInt32(num, digits);
	if (count < minDigits) count = minDigits;

	for (i = count - 1; i >= 0; i--) {
		String_Append(str, digits[i]);
	}
}

void String_AppendFloat(cc_string* str, float num, int fracDigits) {
	int i, whole, digit;
	double frac;

	if (num < 0.0f) {
		String_Append(str, '-'); /* don't need to check success */
		num = -num;
	}

	whole = (int)num;
	String_AppendUInt32(str, whole);

	frac = (double)num - (double)whole;
	if (frac == 0.0) return;
	String_Append(str, '.'); /* don't need to check success */

	for (i = 0; i < fracDigits; i++) {
		frac *= 10;
		digit = (int)frac % 10;
		String_Append(str, '0' + digit);
	}
}

void String_AppendHex(cc_string* str, cc_uint8 value) {
	/* 48 = index of 0, 55 = index of (A - 10) */
	cc_uint8 hi = (value >> 4) & 0xF;
	char c_hi  = hi < 10 ? (hi + 48) : (hi + 55);
	cc_uint8 lo = value & 0xF;
	char c_lo  = lo < 10 ? (lo + 48) : (lo + 55);

	String_Append(str, c_hi);
	String_Append(str, c_lo);
}

CC_NOINLINE static void String_Hex32(cc_string* str, cc_uint32 value) {
	int shift;

	for (shift = 24; shift >= 0; shift -= 8) {
		cc_uint8 part = (cc_uint8)(value >> shift);
		String_AppendHex(str, part);
	}
}

CC_NOINLINE static void String_Hex64(cc_string* str, cc_uint64 value) {
	int shift;

	for (shift = 56; shift >= 0; shift -= 8) {
		cc_uint8 part = (cc_uint8)(value >> shift);
		String_AppendHex(str, part);
	}
}

void String_AppendConst(cc_string* str, const char* src) {
	for (; *src; src++) {
		if (*src == STRING_UNI_LEAD && src[1] && src[2] && src[3]
			&& ((cc_uint8)src[1] & 0x80) && ((cc_uint8)src[2] & 0x80) && ((cc_uint8)src[3] & 0x80)) {
			/* translated text: a unicode escape (see String_AppendCodepoint), copy as-is */
			if (str->capacity - str->length >= STRING_UNI_BYTES) {
				str->buffer[str->length++] = src[0]; str->buffer[str->length++] = src[1];
				str->buffer[str->length++] = src[2]; str->buffer[str->length++] = src[3];
			}
			src += STRING_UNI_BYTES - 1; continue;
		}
		String_Append(str, *src);
	}
}

void String_AppendAll(cc_string* str, const void* data, int len) {
	const char* src = (const char*)data;
	int i;
	for (i = 0; i < len; i++) String_AppendRaw(str, src[i]);
}

void String_AppendString(cc_string* str, const cc_string* src) {
	int i, len, j;
	for (i = 0; i < src->length; i += len) {
		len = String_CharLength(src, i);
		if (len == 1 && (cc_uint8)src->buffer[i] >= 0x80) {
			if (str->capacity - str->length < STRING_UNI_BYTES) break;
			String_AppendCodepoint(str, Convert_CP437ToUnicode(src->buffer[i]));
			continue;
		}
		if (str->capacity - str->length < len) break;
		for (j = 0; j < len; j++) String_AppendRaw(str, src->buffer[i + j]);
	}
}

void String_AppendColorless(cc_string* str, const cc_string* src) {
	int i, len, j;
	for (i = 0; i < src->length; i += len) {
		if (src->buffer[i] == '&' && i + 1 < src->length
			&& String_CharLength(src, i + 1) == 1 && (cc_uint8)src->buffer[i + 1] < 0x80) {
			len = 2; continue;
		}
		len = String_CharLength(src, i);
		if (len == 1 && (cc_uint8)src->buffer[i] >= 0x80) {
			if (str->capacity - str->length < STRING_UNI_BYTES) break;
			String_AppendCodepoint(str, Convert_CP437ToUnicode(src->buffer[i]));
			continue;
		}
		if (str->capacity - str->length < len) break;
		for (j = 0; j < len; j++) String_AppendRaw(str, src->buffer[i + j]);
	}
}


int String_IndexOfAt(const cc_string* str, int offset, char c) {
	int i;
	for (i = offset; i < str->length; i++) {
		if (str->buffer[i] == c) return i;
	}
	return -1;
}

int String_LastIndexOfAt(const cc_string* str, int offset, char c) {
	int i;
	for (i = (str->length - 1) - offset; i >= 0; i--) {
		if (str->buffer[i] == c) return i;
	}
	return -1;
}

static void String_InsertRawAt(cc_string* str, int offset, char c) {
	int i;
	if (offset < 0 || offset > str->length) Process_Abort("Offset for InsertAt out of range");
	if (str->length == str->capacity) Process_Abort("Cannot insert character into full string");
	for (i = str->length; i > offset; i--) str->buffer[i] = str->buffer[i - 1];
	str->buffer[offset] = c;
	str->length++;
}

void String_InsertAt(cc_string* str, int offset, char c) {
	if ((cc_uint8)c >= 0x80 || (cc_uint8)c == (cc_uint8)STRING_UNI_LEAD) {
		String_InsertCodepointAt(str, offset, Convert_CP437ToUnicode(c));
		return;
	}
	String_InsertRawAt(str, offset, c);
}

void String_DeleteAt(cc_string* str, int offset) {
	int i;

	if (offset < 0 || offset >= str->length) {
		Process_Abort("Offset for DeleteAt out of range");
	}
	
	for (i = offset; i < str->length - 1; i++) {
		str->buffer[i] = str->buffer[i + 1];
	}
	str->buffer[str->length - 1] = '\0';
	str->length--;
}

void String_UNSAFE_TrimStart(cc_string* str) {
	int i;
	for (i = 0; i < str->length; i++) {
		if (str->buffer[i] != ' ') break;
			
		str->buffer++;
		str->length--; i--;
	}
}

void String_UNSAFE_TrimEnd(cc_string* str) {
	int i;
	for (i = str->length - 1; i >= 0; i--) {
		if (str->buffer[i] != ' ') break;
		str->length--;
	}
}

int String_IndexOfConst(const cc_string* str, const char* sub) {
	int i, j;

	for (i = 0; i < str->length; i++) {
		for (j = 0; sub[j] && (i + j) < str->length; j++) {

			if (str->buffer[i + j] != sub[j]) break;
		}
		if (sub[j] == '\0') return i;
	}
	return -1;
}

int String_CaselessContains(const cc_string* str, const cc_string* sub) {
	char strCur, subCur;
	int i, j;

	for (i = 0; i < str->length; i++) {
		for (j = 0; j < sub->length && (i + j) < str->length; j++) {

			strCur = str->buffer[i + j]; Char_MakeLower(strCur);
			subCur = sub->buffer[j];     Char_MakeLower(subCur);
			if (strCur != subCur) break;
		}
		if (j == sub->length) return true;
	}
	return false;
}

int String_CaselessStarts(const cc_string* str, const cc_string* sub) {
	char strCur, subCur;
	int i;
	if (str->length < sub->length) return false;

	for (i = 0; i < sub->length; i++) {
		strCur = str->buffer[i]; Char_MakeLower(strCur);
		subCur = sub->buffer[i]; Char_MakeLower(subCur);
		if (strCur != subCur) return false;
	}
	return true;
}

int String_CaselessEnds(const cc_string* str, const cc_string* sub) {
	char strCur, subCur;
	int i, j = str->length - sub->length;	
	if (j < 0) return false; /* sub longer than str */

	for (i = 0; i < sub->length; i++) {
		strCur = str->buffer[j + i]; Char_MakeLower(strCur);
		subCur = sub->buffer[i];     Char_MakeLower(subCur);
		if (strCur != subCur) return false;
	}
	return true;
}

int String_Compare(const cc_string* a, const cc_string* b) {
	char aCur, bCur;
	int i, minLen = min(a->length, b->length);

	for (i = 0; i < minLen; i++) {
		aCur = a->buffer[i]; Char_MakeLower(aCur);
		bCur = b->buffer[i]; Char_MakeLower(bCur);

		if (aCur == bCur) continue;
		return (cc_uint8)aCur - (cc_uint8)bCur;
	}

	/* all chars are equal here - same string, or a substring */
	return a->length - b->length;
}

void String_Format1(cc_string* str, const char* format, const void* a1) {
	String_Format4(str, format, a1, NULL, NULL, NULL);
}
void String_Format2(cc_string* str, const char* format, const void* a1, const void* a2) {
	String_Format4(str, format, a1, a2, NULL, NULL);
}
void String_Format3(cc_string* str, const char* format, const void* a1, const void* a2, const void* a3) {
	String_Format4(str, format, a1, a2, a3, NULL);
}
void String_Format4(cc_string* str, const char* format, const void* a1, const void* a2, const void* a3, const void* a4) {
	const void* arg;
	int i, j = 0, digits;

	const void* args[4];
	args[0] = a1; args[1] = a2; args[2] = a3; args[3] = a4;

	for (i = 0; format[i]; i++) {
		if (format[i] == STRING_UNI_LEAD && format[i + 1] && format[i + 2] && format[i + 3]
			&& ((cc_uint8)format[i + 1] & 0x80) && ((cc_uint8)format[i + 2] & 0x80) && ((cc_uint8)format[i + 3] & 0x80)) {
			/* translated text: a unicode escape (see String_AppendCodepoint), copy as-is */
			if (str->capacity - str->length >= STRING_UNI_BYTES) {
				str->buffer[str->length++] = format[i];     str->buffer[str->length++] = format[i + 1];
				str->buffer[str->length++] = format[i + 2]; str->buffer[str->length++] = format[i + 3];
			}
			i += STRING_UNI_BYTES - 1; continue;
		}
		if (format[i] != '%') { String_Append(str, format[i]); continue; }
		arg = args[j++];

		switch (format[++i]) {
		case 'b': 
			String_AppendInt(str, *((cc_uint8*)arg)); 
			break;
		case 'i': 
			String_AppendInt(str, *((int*)arg)); 
			break;
		case 'f': 
			digits = format[++i] - '0';
			String_AppendFloat(str, *((float*)arg), digits); 
			break;
		case 'p':
			digits = format[++i] - '0';
			String_AppendPaddedInt(str, *((int*)arg), digits); 
			break;
		case 't': 
			String_AppendBool(str, *((cc_bool*)arg)); 
			break;
		case 'c': 
			String_AppendConst(str, (char*)arg);  
			break;
		case 's': 
			String_AppendString(str, (cc_string*)arg);  
			break;
		case 'r':
			String_Append(str, *((char*)arg)); 
			break;
		case 'x':
			if (sizeof(cc_uintptr) == 4) {
				String_Hex32(str, *((cc_uint32*)arg));
			} else {
				String_Hex64(str, *((cc_uint64*)arg));
			}
			break;
		case 'h':
			String_Hex32(str, *((cc_uint32*)arg)); 
			break;
		case 'e':
			digits = *((int*)arg);
			if (digits >= -0xFFFF && digits <= 0xFFFF) {
				String_AppendInt(str, digits);
			} else {
				String_Hex32(str, (cc_uint32)digits); 
			}
			break;
		case '%':
			String_Append(str, '%'); 
			break;
		case 'N':
			str->buffer[str->length++] = '\0'; /* Assumes using String_InitArray_NT */
			break;
		default: 
			Process_Abort("Invalid type for string format");
		}
	}
}


/*########################################################################################################################*
*------------------------------------------------Character set conversions------------------------------------------------*
*#########################################################################################################################*/
static const cc_unichar controlChars[32] = {
	0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
	0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
	0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
	0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC
};

static const cc_unichar extendedChars[129] = { 0x2302,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
	0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
	0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
	0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

cc_unichar Convert_CP437ToUnicode(char c) {
	cc_uint8 raw = (cc_uint8)c;
	if (raw < 0x20) return controlChars[raw];
	if (raw < 0x7F) return raw;
	return extendedChars[raw - 0x7F];
}

char Convert_CodepointToCP437(cc_codepoint cp) {
	char c; Convert_TryCodepointToCP437(cp, &c); return c;
}

static cc_codepoint ReduceEmoji(cc_codepoint cp) {
	if (cp == 0x1F31E) return 0x263C;
	if (cp == 0x1F3B5) return 0x266B;
	if (cp == 0x1F642) return 0x263A;

	if (cp == 0x1F600 || cp == 0x1F601 || cp == 0x1F603) return 0x263A;
	if (cp == 0x1F604 || cp == 0x1F606 || cp == 0x1F60A) return 0x263A;
	return cp;
}

cc_bool Convert_TryCodepointToCP437(cc_codepoint cp, char* c) {
	int i;
	if (cp >= 0x20 && cp < 0x7F) { *c = (char)cp; return true; }
	if (cp >= 0x1F000) cp = ReduceEmoji(cp);

	for (i = 0; i < Array_Elems(controlChars); i++) {
		if (controlChars[i] == cp) { *c = i; return true; }
	}
	for (i = 0; i < Array_Elems(extendedChars); i++) {
		if (extendedChars[i] == cp) { *c = i + 0x7F; return true; }
	}

	*c = '?'; return false;
}

int Convert_Utf8ToCodepoint(cc_codepoint* cp, const cc_uint8* data, cc_uint32 len) {
	cc_uint8 a, b, c, d;
	*cp = 0;
	if (!len) return 0;
	a = data[0];
	if (a <= 0x7F) { *cp = a; return 1; }
	if (a >= 0xC2 && a <= 0xDF) {
		if (len < 2 || (data[1] & 0xC0) != 0x80) return 0;
		*cp = ((a & 0x1F) << 6) | (data[1] & 0x3F); return 2;
	}
	if (a >= 0xE0 && a <= 0xEF) {
		if (len < 3) return 0;
		b = data[1]; c = data[2];
		if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80) return 0;
		if ((a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0)) return 0;
		*cp = ((a & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F); return 3;
	}
	if (a >= 0xF0 && a <= 0xF4) {
		if (len < 4) return 0;
		b = data[1]; c = data[2]; d = data[3];
		if ((b & 0xC0) != 0x80 || (c & 0xC0) != 0x80 || (d & 0xC0) != 0x80) return 0;
		if ((a == 0xF0 && b < 0x90) || (a == 0xF4 && b >= 0x90)) return 0;
		*cp = ((a & 0x07) << 18) | ((b & 0x3F) << 12) | ((c & 0x3F) << 6) | (d & 0x3F);
		return 4;
	}
	return 0;
}

/* Encodes a Unicode codepoint in UTF8, returning number of bytes written. */
static int Convert_UnicodeToUtf8(cc_codepoint cp, cc_uint8* data) {
	if (cp <= 0x7F) {
		data[0] = (cc_uint8)cp; return 1;
	} else if (cp <= 0x7FF) {
		data[0] = 0xC0 | ((cp >> 6) & 0x1F);
		data[1] = 0x80 | (cp & 0x3F); return 2;
	} else if (cp <= 0xFFFF) {
		data[0] = 0xE0 | ((cp >> 12) & 0x0F);
		data[1] = 0x80 | ((cp >> 6) & 0x3F);
		data[2] = 0x80 | (cp & 0x3F); return 3;
	} else {
		data[0] = 0xF0 | ((cp >> 18) & 0x07);
		data[1] = 0x80 | ((cp >> 12) & 0x3F);
		data[2] = 0x80 | ((cp >> 6) & 0x3F);
		data[3] = 0x80 | (cp & 0x3F); return 4;
	}
}

int Convert_CP437ToUtf8(char c, cc_uint8* data) {
	if (c >= 0x20 && c < 0x7F) { data[0] = (cc_uint8)c; return 1; }
	return Convert_UnicodeToUtf8(Convert_CP437ToUnicode(c), data);
}

cc_bool String_IsValidCodepoint(cc_codepoint cp) {
	return cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)
		&& !(cp < 0x20 && cp != 0x09 && cp != 0x0A && cp != 0x0D)
		&& !(cp >= 0x7F && cp <= 0x9F);
}

int String_AppendCodepoint(cc_string* str, cc_codepoint cp) {
	if (!String_IsValidCodepoint(cp)) return 0;
	/* Keep ASCII byte-compatible. Non-ASCII Unicode always uses the escape form,
	   even when CP437 happens to contain the same codepoint, so custom glyph pages
	   can override the legacy CP437 glyphs. */
	if (cp < 0x80) {
		if (str->length >= str->capacity) return 0;
		String_AppendRaw(str, (char)cp); return 1;
	}
	if (str->capacity - str->length < STRING_UNI_BYTES) return 0;
	String_AppendRaw(str, STRING_UNI_LEAD);
	String_AppendRaw(str, (char)(0x80 | ((cp >> 14) & 0x7F)));
	String_AppendRaw(str, (char)(0x80 | ((cp >> 7) & 0x7F)));
	String_AppendRaw(str, (char)(0x80 | (cp & 0x7F)));
	return STRING_UNI_BYTES;
}

static int EscapeLengthAt(const char* buf, int avail) {
	int n;
	if (avail <= 0 || buf[0] != STRING_UNI_LEAD) return 1;
	for (n = 1; n < STRING_UNI_BYTES && n < avail && ((cc_uint8)buf[n] & 0x80); n++) { }
	return n;
}

int String_CharLength(const cc_string* str, int index) {
	if (index < 0 || index >= str->length) return 0;
	return EscapeLengthAt(&str->buffer[index], str->length - index);
}

int String_CharLengthBefore(const cc_string* str, int index) {
	int n;
	if (index <= 0 || index > str->length) return 0;
	for (n = 0; n < STRING_UNI_BYTES - 1 && n < index
		&& ((cc_uint8)str->buffer[index - 1 - n] & 0x80); n++) { }
	if (n > 0 && n < index && str->buffer[index - 1 - n] == STRING_UNI_LEAD) return n + 1;
	return 1;
}

cc_codepoint String_GetCodepoint(const cc_string* str, int index, int* len) {
	const cc_uint8* b;
	cc_codepoint cp;
	*len = String_CharLength(str, index);
	if (!*len) return 0;
	if (*len == 1) return Convert_CP437ToUnicode(str->buffer[index]);
	if (*len != STRING_UNI_BYTES) return 0xFFFD;
	b = (const cc_uint8*)&str->buffer[index];
	cp = ((cc_codepoint)(b[1] & 0x7F) << 14) | ((cc_codepoint)(b[2] & 0x7F) << 7) | (b[3] & 0x7F);
	return String_IsValidCodepoint(cp) ? cp : 0xFFFD;
}

cc_bool String_HasUnicode(const cc_string* str) {
	int i;
	for (i = 0; i < str->length; i += String_CharLength(str, i)) {
		if (str->buffer[i] == STRING_UNI_LEAD && String_CharLength(str, i) > 1) return true;
	}
	return false;
}

void String_AppendCP437Only(cc_string* dst, const cc_string* src) {
	int i, len; cc_codepoint cp; char c;
	for (i = 0; i < src->length; i += len) {
		cp = String_GetCodepoint(src, i, &len);
		if (len == 1) String_Append(dst, src->buffer[i]);
		else String_Append(dst, cp && Convert_TryCodepointToCP437(cp, &c) ? c : '?');
	}
}

void String_AppendCP437(cc_string* dst, const void* data, int numBytes) {
	const cc_uint8* bytes = (const cc_uint8*)data;
	int i;
	for (i = 0; i < numBytes; i++) {
		if (!bytes[i]) {
			String_Append(dst, '?');
		} else if (bytes[i] >= 0x80 || bytes[i] == (cc_uint8)STRING_UNI_LEAD) {
			if (dst->capacity - dst->length < STRING_UNI_BYTES) break;
			String_Append(dst, (char)bytes[i]);
		} else {
			String_Append(dst, (char)bytes[i]);
		}
	}
}

int String_SplitPoint(const cc_string* str, int maxLen) {
	int i;
	if (maxLen >= str->length) return str->length;
	if (maxLen <= 0) return 0;
	for (i = max(0, maxLen - 3); i < maxLen; i++) {
		if (str->buffer[i] == STRING_UNI_LEAD && i + String_CharLength(str, i) > maxLen) return i;
	}
	return maxLen;
}

int String_InsertCodepointAt(cc_string* str, int offset, cc_codepoint cp) {
	char tmp[STRING_UNI_BYTES]; cc_string part; int len;
	if (offset < 0 || offset > str->length) return 0;
	len = String_AppendCodepoint(&(cc_string){ tmp, 0, STRING_UNI_BYTES }, cp);
	if (!len || str->capacity - str->length < len) return 0;
	part = String_Init(tmp, len, len);
	{ int i; for (i = 0; i < len; i++) String_InsertRawAt(str, offset + i, part.buffer[i]); }
	return part.length;
}

int String_DeleteCharAt(cc_string* str, int offset) {
	int len = String_CharLength(str, offset), i;
	if (!len) return 0;
	for (i = offset; i + len < str->length; i++) str->buffer[i] = str->buffer[i + len];
	str->length -= len;
	str->buffer[str->length] = '\0';
	return len;
}

int String_CharToUtf8(const cc_string* str, int* index, cc_uint8* data) {
	int len; cc_codepoint cp;
	if (*index < 0 || *index >= str->length) return 0;
	cp = String_GetCodepoint(str, *index, &len);
	*index += len;
	return Convert_UnicodeToUtf8(cp, data);
}

int String_EncodeUtf8(void* data, const cc_string* src) {
	cc_uint8* dst = (cc_uint8*)data;
	int i, len = 0;
	for (i = 0; i < src->length; ) len += String_CharToUtf8(src, &i, dst + len);
	dst[len] = '\0';
	return len;
}

int String_EncodeUtf16(void* data, const cc_string* src) {
	cc_uint8* dst = (cc_uint8*)data;
	int i, len = 0, bytes; cc_codepoint cp, unit;
	for (i = 0; i < src->length; ) {
		cp = String_GetCodepoint(src, i, &bytes); i += bytes;
		if (cp <= 0xFFFF) {
			unit = cp;
			dst[len * 2] = (cc_uint8)unit;
			dst[len * 2 + 1] = (cc_uint8)(unit >> 8);
			len++;
		} else {
			cp -= 0x10000;
			unit = (cc_uint16)(0xD800 | (cp >> 10));
			dst[len * 2] = (cc_uint8)unit;
			dst[len * 2 + 1] = (cc_uint8)(unit >> 8);
			len++;
			unit = (cc_uint16)(0xDC00 | (cp & 0x3FF));
			dst[len * 2] = (cc_uint8)unit;
			dst[len * 2 + 1] = (cc_uint8)(unit >> 8);
			len++;
		}
	}
	dst[len * 2] = dst[len * 2 + 1] = 0;
	return len;
}

void String_AppendUtf16(cc_string* value, const void* data, int numBytes) {
	const cc_unichar* chars = (const cc_unichar*)data;
	int i, count = numBytes / 2; cc_codepoint cp, high; 
	for (i = 0; i < count; i++) {
		cp = chars[i];
		if (cp >= 0xD800 && cp <= 0xDBFF) {
			if (i + 1 >= count) break;
			high = cp;
			if (chars[i + 1] < 0xDC00 || chars[i + 1] > 0xDFFF) continue;
			cp = 0x10000 + ((high - 0xD800) << 10) + (chars[++i] - 0xDC00);
		} else if (cp >= 0xDC00 && cp <= 0xDFFF) continue;
		if (!String_AppendCodepoint(value, cp)) break;
	}
}

void String_AppendUtf8(cc_string* value, const void* data, int numBytes) {
	const cc_uint8* chars = (const cc_uint8*)data;
	int len; cc_codepoint cp;
	while (numBytes > 0) {
		len = Convert_Utf8ToCodepoint(&cp, chars, numBytes);
		if (!len) return;
		if (String_IsValidCodepoint(cp) && !String_AppendCodepoint(value, cp)) return;
		chars += len; numBytes -= len;
	}
}

void String_AppendCP1252(cc_string* value, const void* data, int numBytes) {
	static const cc_codepoint cp1252Ext[32] = {
		0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
		0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
	};
	const cc_uint8* chars = (const cc_uint8*)data;
	int i; cc_codepoint cp;
	for (i = 0; i < numBytes; i++) {
		cp = chars[i];
		if (cp >= 0x80 && cp <= 0x9F) cp = cp1252Ext[cp - 0x80];
		if (cp != 0xFFFD) String_AppendCodepoint(value, cp);
	}
}

/*########################################################################################################################*
*--------------------------------------------------Numerical conversions--------------------------------------------------*
*#########################################################################################################################*/
cc_bool Convert_ParseUInt8(const cc_string* str, cc_uint8* value) {
	int tmp; 
	*value = 0;
	if (!Convert_ParseInt(str, &tmp) || tmp < 0 || tmp > UInt8_MaxValue) return false;
	*value = (cc_uint8)tmp; return true;
}

cc_bool Convert_ParseUInt16(const cc_string* str, cc_uint16* value) {
	int tmp; 
	*value = 0;
	if (!Convert_ParseInt(str, &tmp) || tmp < 0 || tmp > UInt16_MaxValue) return false;
	*value = (cc_uint16)tmp; return true;
}

static int Convert_CompareDigits(const char* digits, const char* magnitude) {
	int i;
	for (i = 0; ; i++) {
		if (magnitude[i] == '\0')     return  0;
		if (digits[i] > magnitude[i]) return  1;
		if (digits[i] < magnitude[i]) return -1;
	}
	return 0;
}

static cc_bool Convert_TryParseDigits(const cc_string* str, cc_bool* negative, char* digits, int maxDigits) {
	char* start = digits;
	int offset = 0, i;

	*negative = false;
	if (!str->length) return false;
	digits += (maxDigits - 1);

	/* Handle number signs */
	if (str->buffer[0] == '-') { *negative = true; offset = 1; }
	if (str->buffer[0] == '+') { offset = 1; }

	/* add digits, starting at last digit */
	for (i = str->length - 1; i >= offset; i--) 
	{
		char c = str->buffer[i];
		if (c < '0' || c > '9' || digits < start) return false;
		*digits-- = c;
	}

	for (; digits >= start; ) { *digits-- = '0'; }
	return true;
}

#define INT32_DIGITS 10
cc_bool Convert_ParseInt(const cc_string* str, int* value) {
	cc_bool negative;
	char digits[INT32_DIGITS];
	int i, compare, sum = 0;

	*value = 0;	
	if (!Convert_TryParseDigits(str, &negative, digits, INT32_DIGITS)) return false;
	
	if (negative) {
		compare = Convert_CompareDigits(digits, "2147483648");
		/* Special case, since |largest min value| is > |largest max value| */
		if (compare == 0) { *value = Int32_MinValue; return true; }
	} else {
		compare = Convert_CompareDigits(digits, "2147483647");
	}
	if (compare > 0) return false;

	for (i = 0; i < INT32_DIGITS; i++) {
		sum *= 10; sum += digits[i] - '0';
	}

	if (negative) sum = -sum;
	*value = sum;
	return true;
}

#define UINT64_DIGITS 20
cc_bool Convert_ParseUInt64(const cc_string* str, cc_uint64* value) {
	cc_bool negative;
	char digits[UINT64_DIGITS];
	int i, compare;
	cc_uint64 sum = 0;

	*value = 0;
	if (!Convert_TryParseDigits(str, &negative, digits, UINT64_DIGITS)) return false;

	compare = Convert_CompareDigits(digits, "18446744073709551615");
	if (negative || compare > 0) return false;

	for (i = 0; i < UINT64_DIGITS; i++) {
		sum *= 10; sum += digits[i] - '0';
	}

	*value = sum;
	return true;
}

cc_bool Convert_ParseFloat(const cc_string* str, float* value) {
	int i = 0;
	cc_bool negate = false;
	double sum, whole, fract, divide = 10.0;
	*value = 0.0f;

	/* Handle number signs */
	if (!str->length) return false;
	if (str->buffer[0] == '-') { negate = true; i++; }
	if (str->buffer[0] == '+') { i++; }

	for (whole = 0.0; i < str->length; i++) {
		char c = str->buffer[i];
		if (c == '.' || c == ',') { i++; break; }

		if (c < '0' || c > '9') return false;
		whole *= 10; whole += (c - '0');
	}

	for (fract = 0.0; i < str->length; i++) {
		char c = str->buffer[i];
		if (c < '0' || c > '9') return false;

		fract += (c - '0') / divide; divide *= 10;
	}

	sum = whole + fract;
	if (negate) sum = -sum;
	*value = (float)sum;
	return true;
}

cc_bool Convert_ParseBool(const cc_string* str, cc_bool* value) {
	if (String_CaselessEqualsConst(str, "True")) {
		*value = true; return true;
	}
	if (String_CaselessEqualsConst(str, "False")) {
		*value = false; return true;
	}
	*value = false; return false;
}


/*########################################################################################################################*
*------------------------------------------------------StringsBuffer------------------------------------------------------*
*#########################################################################################################################*/
#define STRINGSBUFFER_BUFFER_EXPAND_SIZE 8192

#define StringsBuffer_GetOffset(raw)  ((raw) >> buffer->_lenShift)
#define StringsBuffer_GetLength(raw)  ((raw)  & buffer->_lenMask)
#define StringsBuffer_PackOffset(off) ((off) << buffer->_lenShift)

void StringsBuffer_Init(struct StringsBuffer* buffer) {
	buffer->count       = 0;
	buffer->totalLength = 0;
	buffer->textBuffer     = buffer->_defaultBuffer;
	buffer->flagsBuffer    = buffer->_defaultFlags;
	buffer->_textCapacity  = STRINGSBUFFER_BUFFER_DEF_SIZE;
	buffer->_flagsCapacity = STRINGSBUFFER_FLAGS_DEF_ELEMS;

	if (buffer->_lenShift) return;
	StringsBuffer_SetLengthBits(buffer, STRINGSBUFFER_DEF_LEN_SHIFT);
}

void StringsBuffer_SetLengthBits(struct StringsBuffer* buffer, int bits) {
	buffer->_lenShift = bits;
	buffer->_lenMask  = (1 << bits) - 1;
}

void StringsBuffer_Clear(struct StringsBuffer* buffer) {
	/* Never initialised to begin with */
	if (!buffer->_flagsCapacity) return;

	if (buffer->textBuffer != buffer->_defaultBuffer) {
		Mem_Free(buffer->textBuffer);
	}
	if (buffer->flagsBuffer != buffer->_defaultFlags) {
		Mem_Free(buffer->flagsBuffer);
	}
	StringsBuffer_Init(buffer);
}

cc_string StringsBuffer_UNSAFE_Get(struct StringsBuffer* buffer, int i) {
	cc_uint32 flags, offset, len;
	if (i < 0 || i >= buffer->count) Process_Abort("Tried to get String past StringsBuffer end");

	flags  = buffer->flagsBuffer[i];
	offset = StringsBuffer_GetOffset(flags);
	len    = StringsBuffer_GetLength(flags);
	return String_Init(&buffer->textBuffer[offset], len, len);
}

void StringsBuffer_UNSAFE_GetRaw(struct StringsBuffer* buffer, int i, cc_string* dst) {
	cc_uint32 flags = buffer->flagsBuffer[i];
	dst->buffer     = buffer->textBuffer + StringsBuffer_GetOffset(flags);
	dst->length     = StringsBuffer_GetLength(flags);
	dst->capacity   = 0;
}

void StringsBuffer_Add(struct StringsBuffer* buffer, const cc_string* str) {
	int textOffset;
	/* StringsBuffer hasn't been initialised yet, do it here */
	if (!buffer->_flagsCapacity) StringsBuffer_Init(buffer);

	if (buffer->count == buffer->_flagsCapacity) {
		Utils_Resize((void**)&buffer->flagsBuffer, &buffer->_flagsCapacity,
					4, STRINGSBUFFER_FLAGS_DEF_ELEMS, 512);
	}

	if (str->length > buffer->_lenMask) {
		Process_Abort("String too big to insert into StringsBuffer");
	}

	textOffset = buffer->totalLength;
	if (textOffset + str->length >= buffer->_textCapacity) {
		Utils_Resize((void**)&buffer->textBuffer, &buffer->_textCapacity,
					1, STRINGSBUFFER_BUFFER_DEF_SIZE, 8192);
	}

	Mem_Copy(&buffer->textBuffer[textOffset], str->buffer, str->length);
	buffer->flagsBuffer[buffer->count] = str->length | StringsBuffer_PackOffset(textOffset);

	buffer->count++;
	buffer->totalLength += str->length;
}

void StringsBuffer_Remove(struct StringsBuffer* buffer, int index) {
	cc_uint32 flags, offset, len;
	cc_uint32 i, offsetAdj;
	if (index < 0 || index >= buffer->count) Process_Abort("Tried to remove String past StringsBuffer end");

	flags  = buffer->flagsBuffer[index];
	offset = StringsBuffer_GetOffset(flags);
	len    = StringsBuffer_GetLength(flags);

	/* Imagine buffer is this: AAXXYYZZ, and want to delete XX */
	/* We iterate from first char of Y to last char of Z, */
	/* shifting each character two to the left. */
	for (i = offset + len; i < buffer->totalLength; i++) {
		buffer->textBuffer[i - len] = buffer->textBuffer[i]; 
	}

	/* Adjust text offset of elements after this element */
	/* Elements may not be in order so must account for that */
	offsetAdj = StringsBuffer_PackOffset(len);
	for (i = index; i < buffer->count - 1; i++) {
		buffer->flagsBuffer[i] = buffer->flagsBuffer[i + 1];
		if (buffer->flagsBuffer[i] >= flags) {
			buffer->flagsBuffer[i] -= offsetAdj;
		}
	}
	
	buffer->count--;
	buffer->totalLength -= len;
}

static struct StringsBuffer* sort_buffer;
static void StringsBuffer_QuickSort(int left, int right) {
	struct StringsBuffer* buffer = sort_buffer;
	cc_uint32* keys = buffer->flagsBuffer; cc_uint32 key;

	while (left < right) {
		int i = left, j = right;
		cc_string pivot = StringsBuffer_UNSAFE_Get(buffer, (i + j) >> 1);
		cc_string strI, strJ;

		/* partition the list */
		while (i <= j) {
			while ((strI = StringsBuffer_UNSAFE_Get(buffer, i), String_Compare(&pivot, &strI)) > 0) i++;
			while ((strJ = StringsBuffer_UNSAFE_Get(buffer, j), String_Compare(&pivot, &strJ)) < 0) j--;
			QuickSort_Swap_Maybe();
		}
		/* recurse into the smaller subset */
		QuickSort_Recurse(StringsBuffer_QuickSort)
	}
}

void StringsBuffer_Sort(struct StringsBuffer* buffer) {
	sort_buffer = buffer;
	StringsBuffer_QuickSort(0, buffer->count - 1);
}


/*########################################################################################################################*
*------------------------------------------------------Word wrapper-------------------------------------------------------*
*#########################################################################################################################*/
static cc_bool WordWrap_IsWrapper(char c) {
	return c == '\0' || c == ' ' || c == '-' || c == '>' || c == '<' || c == '/' || c == '\\';
}

void WordWrap_Do(STRING_REF cc_string* text, cc_string* lines, int numLines, int lineLen) {
	int i, lineStart, lineEnd, nextLineStart, chars, cutLen;
	cc_bool foundWrapper;
	for (i = 0; i < numLines; i++) lines[i] = String_Empty;

	for (i = 0, lineStart = 0; i < numLines; i++) {
		nextLineStart = lineStart;
		for (chars = 0; chars < lineLen && nextLineStart < text->length; chars++) {
			nextLineStart += String_CharLength(text, nextLineStart);
		}
		if (nextLineStart >= text->length) {
			lines[i] = String_UNSAFE_SubstringAt(text, lineStart); return;
		}
		foundWrapper = false;
		for (lineEnd = nextLineStart; lineEnd >= lineStart; ) {
			if (lineEnd < text->length && WordWrap_IsWrapper(text->buffer[lineEnd])
				&& String_CharLength(text, lineEnd) == 1) { foundWrapper = true; break; }
			if (lineEnd == lineStart) break;
			lineEnd -= String_CharLengthBefore(text, lineEnd);
		}
		if (!foundWrapper) lineEnd = lineStart - 1;
		lineEnd++;
		if (lineEnd <= lineStart || lineEnd >= nextLineStart) {
			cutLen = nextLineStart - lineStart;
			lines[i] = String_UNSAFE_Substring(text, lineStart, cutLen);
			lineStart = nextLineStart;
		} else {
			lines[i] = String_UNSAFE_Substring(text, lineStart, lineEnd - lineStart);
			lineStart = lineEnd;
		}
	}
}

void WordWrap_GetCoords(int index, const cc_string* lines, int numLines, int* coordX, int* coordY) {
	int y, offset = 0, lineLen;
	if (index == -1) index = Int32_MaxValue;
	*coordX = -1; *coordY = 0;
	for (y = 0; y < numLines; y++) {
		lineLen = lines[y].length;
		if (!lineLen) break;
		*coordY = y;
		if (index < offset + lineLen) {
			*coordX = index - offset; break;
		}
		offset += lineLen;
	}
	if (*coordX == -1) *coordX = lines[*coordY].length;
}

int WordWrap_GetBackLength(const cc_string* text, int index) {
	int start = index;
	if (index <= 0) return 0;
	if (index >= text->length) Process_Abort("WordWrap_GetBackLength - index past end of string");
	while (index > 0 && text->buffer[index] == ' ') index -= String_CharLengthBefore(text, index);
	while (index > 0 && text->buffer[index] != ' ') index -= String_CharLengthBefore(text, index);
	return start - index;
}

int WordWrap_GetForwardLength(const cc_string* text, int index) {
	int start = index, length = text->length;
	if (index == -1) return 0;
	if (index >= text->length) Process_Abort("WordWrap_GetForwardLength - index past end of string");
	while (index < length && text->buffer[index] != ' ') index += String_CharLength(text, index);
	while (index < length && text->buffer[index] == ' ') index += String_CharLength(text, index);
	return index - start;
}

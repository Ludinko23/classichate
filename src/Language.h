#ifndef CC_LANGUAGE_H
#define CC_LANGUAGE_H
#include "Core.h"
CC_BEGIN_HEADER

/*
Simple runtime translation support.
 - Translations live in "languages/<code>.json" next to the executable
 - Each file is a flat JSON object: { "English text": "Translated text", ... }
 - The special key "_name" is the name shown in the language switcher
 - English text is the built-in default, so a missing key just shows English
*/

/* Scans the languages folder and loads the language saved in options */
void Lang_Init(void);
/* Frees all loaded translations */
void Lang_Free(void);

/* Returns the translation of the given English text, or the text itself if there is none */
/* NOTE: The returned pointer is only valid until the language changes - don't store it */
const char* Lang_Get(const char* english);

/* Appends the translation of the given English text to dst */
/* (Lang_Get returns text already stored in chat's unicode format, so it must be appended */
/*  with String_AppendString - String_AppendConst would re-encode each byte separately) */
void Lang_AppendTo(cc_string* dst, const char* english);
/* Looks up src (English) and appends its translation to dst. Returns false if there is none. */
/* This is what makes every button/label automatically translatable. */
cc_bool Lang_Translate(const cc_string* src, cc_string* dst);
/* Appends text returned by Lang_Get/Lang_CurrentName (already in chat's format) to dst */
void Lang_AppendStored(cc_string* dst, const char* stored);

/* Display name of the currently selected language (e.g. "Deutsch") */
const char* Lang_CurrentName(void);
/* Switches to the next available language (wraps back to English) and saves it */
void Lang_SetNext(void);

CC_END_HEADER
#endif

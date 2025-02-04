/*
 *  Copyright (C) SplinterGU (Fenix/BennuGD) (Since 2006)
 *  Copyright (C) 2002-2006 Fenix Team (Fenix)
 *  Copyright (C) 1999-2002 José Luis Cebrián Pagüe (Fenix)
 *
 *  This file is part of Bennu Game Development
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty. In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "identifiers.h"
#include "compiler.h"

/* ---------------------------------------------------------------------- */
/* Identifier manager                                                     */
/* ---------------------------------------------------------------------- */

static identifier * identifier_hash[64];
static int64_t      identifier_code = 1;
int                 identifier_count = 0;

int identifier_hash_value(const char * string) {
	const char * ptr = string;
    int t = 0;

/*	while (*ptr) t = (t << 1) | *ptr++; */
	while (*ptr) t = (t << 3) | ( (*ptr++) & 0x07 ); /* Improved hashing dispersion */

	return (t & 63);
}

identifier * identifier_first() {
	int n;
	for (n = 0; n < 64; n++) if (identifier_hash[n]) return identifier_hash[n];
	return 0;
}

identifier * identifier_next(identifier * id) {
	int n;
	if (id->next) return id->next;
	n = identifier_hash_value(id->name);
	for (n++; n < 64; n++) if (identifier_hash[n]) return identifier_hash[n];
	return 0;
}

void identifier_init() {
	int i;
	for (i = 0; i < 64; i++) identifier_hash[i] = 0;
	identifier_count = 0;
	identifier_code = 1;
}

void identifier_dump() {
	identifier * ptr;
    int i;
	printf("\n---- %d identifiers ----\n\n", identifier_count);
	for (i = 0; i < 64; i++) {
		ptr = identifier_hash[i];
		int ii = 0;
		while (ptr) {
			ii++;
			printf("%4d: %-32s [%04d] [%3d]\n", ( int ) ptr->code, ptr->name, i, ii);
			ptr = ptr->next;
		}
	}
}

int64_t identifier_add_as(const char * string, int64_t code) {
	int hash = identifier_hash_value(string);
	identifier * w = (identifier *)calloc(1, sizeof(identifier));

	if (!w) {
		fprintf(stdout, "identifier_add: out of memory\n");
		exit(1);
	}

	w->name = strdup(string);
	if (!w->name) {
		fprintf(stdout, "identifier_add: out of memory\n");
		exit(1);
	}
	w->line = line_count; /* Save First appearance */
	w->f = current_file;  /* Save File info */
	w->code = code;
	w->next = identifier_hash[hash];
	identifier_hash[hash] = w;
	identifier_count++;

	return 1;
}

int64_t identifier_add(const char * string) {
	int code = identifier_code++;
	if (!identifier_add_as(string, code)) return 0;
	return code;
}

int64_t identifier_search(const char * string) {
	int hash = identifier_hash_value(string);
	identifier * ptr = identifier_hash[hash];

	while (ptr) {
		if (!ptr->name) return 0;
		if (ptr->name[0] == *string) {
			if (strcmp(string, ptr->name) == 0) break;
		}
		ptr = ptr->next;
	}
	return ptr ? ptr->code : 0;
}

/* Return line for the identifier */
int identifier_line(int64_t code) {
	identifier * ptr;
    int i;

	for (i = 0; i < 64; i++) {
		ptr = identifier_hash[i];
		while (ptr) {
			if (ptr->code == code) return ptr->line;
			ptr = ptr->next;
		}
	}
	return 0;
}

/* Return file for the identifier */
int identifier_file(int64_t code) {
	identifier * ptr;
    int i;

	for (i = 0; i < 64; i++) {
		ptr = identifier_hash[i];
		while (ptr) {
			if (ptr->code == code) return ptr->f;
			ptr = ptr->next;
		}
	}
	return 0;
}

const char * identifier_name(int64_t code) {
	identifier * ptr;
    int i;

	for (i = 0; i < 64; i++) {
		ptr = identifier_hash[i];
		while (ptr) {
			if (ptr->code == code) return ptr->name;
			ptr = ptr->next;
		}
	}
	return 0;
}

int64_t identifier_search_or_add(const char * string) {
	int64_t result = identifier_search(string);
	return result ? result : identifier_add(string);
}

int64_t identifier_next_code() {
	return identifier_code;
}

int identifier_is_basic_type(int64_t id) {
	return (
        id == identifier_int64 		||
		id == identifier_int32 		||
		id == identifier_short 		||
		id == identifier_int8 		||
		id == identifier_char 		||
        id == identifier_qword 		||
		id == identifier_dword 		||
		id == identifier_word 		||
		id == identifier_byte 		||
		id == identifier_signed 	||
		id == identifier_unsigned 	||
        id == identifier_double 	||
		id == identifier_float 		||
		id == identifier_string
        //|| id == identifier_struct
		);
}

int identifier_is_type(int64_t id) {
	return identifier_is_basic_type(id) || segment_by_name(id) != NULL;
}

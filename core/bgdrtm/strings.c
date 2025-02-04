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

/****************************************************************************/
/* FILE        : strings.c                                                  */
/* DESCRIPTION : Strings management. Includes any function related to       */
/*               variable-length strings. Those strings are allocated       */
/*               in dynamic memory with reference counting.                 */
/****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

//#ifdef TARGET_BEOS
//#include <posix/assert.h>
//#else
//#include <assert.h>
//#endif

#include "files.h"
#include "xctype.h"

/****************************************************************************/

#define BLOCK_INCR  1024

#define bit_set(m,b)    (((uint64_t *)(m))[(b)>>6] |=   1ULL<<((b)&0x3F))
#define bit_clr(m,b)    (((uint64_t *)(m))[(b)>>6] &= ~(1ULL<<((b)&0x3F)))
#define bit_tst(m,b)    (((uint64_t *)(m))[(b)>>6] &   (1ULL<<((b)&0x3F)))

/****************************************************************************/
/* STATIC VARIABLES :                                                       */
/****************************************************************************/

/* Fixed string memory. The DCB fixed strings are stored here */
static unsigned char    * string_mem = NULL;

static int              string_reserved = 0;        /* Last fixed string */

static unsigned char    ** string_ptr = NULL;       /* Pointers to each string's text. Every string is allocated using strdup() or malloc().
                                                       A pointer of a unused slot is 0.
                                                       Exception: "fixed" strings are stored in a separate memory block and should not be freed */
static uint64_t         * string_uct = NULL;        /* Usage count for each string. An unused slot has a count of 0 */

static uint64_t         * string_bmp = NULL;        /* Bitmap for speed up string creation, and reused freed slots */

static int              string_allocated = 0;       /* How many string slots are available in the ptr, uct and dontfree arrays */

static int              string_bmp_start = 0;       /* Offset of assignable string for reused (64bits each one) */

static int              string_last_id = 1;         /* How many strings slots are used. This is only the bigger id in use + 1.
                                                      There may be unused slots in this many positions */

/* --------------------------------------------------------------------------- */

void _string_ptoa( unsigned char *t, void * ptr )  {
    unsigned char c;
    int64_t p = ( int64_t )( intptr_t )ptr;

    c = ((( p ) & 0xf000000000000000 ) >> 60 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x0f00000000000000 ) >> 56 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x00f0000000000000 ) >> 52 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x000f000000000000 ) >> 48 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x0000f00000000000 ) >> 44 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x00000f0000000000 ) >> 40 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x000000f000000000 ) >> 36 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x0000000f00000000 ) >> 32 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x00000000f0000000 ) >> 28 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x000000000f000000 ) >> 24 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x0000000000f00000 ) >> 20 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x00000000000f0000 ) >> 16 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x000000000000f000 ) >> 12 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x0000000000000f00 ) >>  8 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ((( p ) & 0x00000000000000f0 ) >>  4 );
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    c = ( p ) & 0x000000000000000f;
    *t++ = ( c > 9 ? '7' : '0' ) + c; /* '7' + 10 = 'A' */

    *t = '\0';
}

/* --------------------------------------------------------------------------- */

int64_t string_atop( unsigned char *str )  {
    unsigned char c;
    if ( !str ) return 0;

    int64_t result = 0, value;

    while ( ( c = *str ) ) {
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        } else {
            return result;
        }
        result = (result << 4) | value;
        str++;
    }

    return result;
}

/* --------------------------------------------------------------------------- */

void _string_ntoa( unsigned char *p, uint64_t n ) {
    unsigned char * i = p;

    p += 20; // Max int64 digits - 1
    if ( ( int64_t ) n < 0 ) {
        * i++ = '-';
        n = ( uint64_t ) (- ( int64_t ) n );
    }

    * p = '\0';
    do {
        * --p = '0' + ( n % 10LL );
    } while ( n /= 10LL );

    if ( p > i ) while (( *i++ = *p++ ));
}

/* --------------------------------------------------------------------------- */

void _string_utoa( unsigned char *p, uint64_t n ) {
    unsigned char * i = p;

    p += 20; // Max int64 digits - 1

    * p = '\0';
    do {
        * --p = '0' + ( n % 10LL );
    } while ( n /= 10LL );

    if ( p > i ) while (( *i++ = *p++ ) );
}

/* --------------------------------------------------------------------------- */

/****************************************************************************/
/* FUNCTION : string_alloc                                                  */
/****************************************************************************/
/* int bytes: how many new strings we could need                            */
/****************************************************************************/
/* Increase the size of the internal string arrays. This limits how many    */
/* strings you can have in memory at the same time, and this should be      */
/* called when every identifier slot available is already used.             */
/****************************************************************************/

static void string_alloc( int count ) {
    int lim = ( string_allocated >> 6 );

    count = (( count >> 6 ) + 1 ) << 6;

    string_allocated += count;

    string_ptr = ( unsigned char ** ) realloc( string_ptr, string_allocated * sizeof( unsigned char * ) );
    string_uct = ( uint64_t * ) realloc( string_uct, string_allocated * sizeof( uint64_t ) );
    string_bmp = ( uint64_t * ) realloc( string_bmp, ( string_allocated >> 6 ) * sizeof( uint64_t ) );

    if ( !string_ptr || !string_uct || !string_bmp ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit( 0 );
    }

    memset( &string_bmp[ lim ], '\0', ( count >> 6 ) * sizeof ( uint64_t ) );
}

/****************************************************************************/
/* FUNCTION : string_init                                                   */
/****************************************************************************/
/* Allocate memory for the dynamic arrays. You should call this function    */
/* before anything else in this file. There is enough space for about       */
/* BLOCK_INCR short strings, that should be enough for simple programs.     */
/* More space is allocated as needed.                                       */
/****************************************************************************/

void string_init() {
    string_alloc( BLOCK_INCR );

    /* Create an empty string with ID 0 */

    string_last_id = 0;
    string_reserved = 0;
    string_bmp_start = 0;
}

/****************************************************************************/
/* FUNCTION : string_dump                                                   */
/****************************************************************************/
/* Shows all the strings in memory in the console, including the reference  */
/* count (usage count) of each string.                                      */
/****************************************************************************/

void string_dump( int ( *wlog )( const char *fmt, ... ) ) {
    int used = 0;
    wlog(   "[STRING] --ID --Uses Type-- String--------\n" );
    for ( int i = 0; i < string_allocated; i++ ) {
        if ( bit_tst( string_bmp, i ) && string_ptr[i] ) {
            if ( !string_uct[i] ) {
                if ( i >= string_reserved ) {
                    free( string_ptr[i] );
                    string_ptr[i] = NULL;
                    bit_clr( string_bmp, i );
                }
                continue;
            }
            used++;
            wlog( "[STRING] %4d %6" PRId64 " %6s {%s}\n", i, string_uct[i], ( i >= string_reserved ) ? "" : "STATIC", string_ptr[i] );
        }
    }

    wlog(   "[STRING] ---- ------ ------ --------------\n"
            "[STRING] Total Strings Used=%d MaxID=%d\n", used, string_allocated );
}

/****************************************************************************/
/* FUNCTION : string_get                                                    */
/****************************************************************************/
/* int code: identifier of the string you want                              */
/****************************************************************************/
/* Returns the contens of an string. Beware: this pointer with only be      */
/* valid while no other string function is called.                          */
/****************************************************************************/

const unsigned char * string_get( int64_t code ) {
//    assert( code < string_allocated && code >= 0 );
    if ( code >= string_allocated || code < 0 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
    return string_ptr[code];
}

/****************************************************************************/
/* FUNCTION : string_load                                                   */
/****************************************************************************/
/* file * fp: the DCB file (must be opened)                                 */
/*                                                                          */
/* This function uses the global "dcb" struct. It should be already filled. */
/****************************************************************************/
/* Loads the string portion of a DCB file. This includes an area with all   */
/* the text (that will be stored in the string_mem pointer) and an array of */
/* the offsets of every string. This function fills the internal arrayswith */
/* all this data and allocates memory if needed.                            */
/****************************************************************************/

void string_load( void * fp, int64_t ostroffs, int64_t ostrdata, int64_t nstrings, int64_t totalsize ) {
    uint64_t * string_offset;
    int n;

    string_mem = malloc( totalsize );
    if ( !string_mem ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( string_mem );

    string_offset = ( uint64_t * ) malloc( sizeof( uint64_t ) * nstrings );
    if ( !string_offset ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( string_offset );

    file_seek(( file * )fp, ostroffs, SEEK_SET );
    file_readUint64A(( file * )fp, string_offset, nstrings );

    if ( string_last_id + nstrings > string_allocated )
        string_alloc((( string_last_id + nstrings - string_allocated ) / BLOCK_INCR + 1 ) * BLOCK_INCR );

    file_seek(( file * )fp, ostrdata, SEEK_SET );
    file_read(( file * )fp, string_mem, totalsize );

    for ( n = 0; n < nstrings; n++ ) {
        string_ptr[string_last_id + n] = string_mem + string_offset[n];
        string_uct[string_last_id + n] = 0;
        bit_set( string_bmp, string_last_id + n );
    }

    string_last_id += nstrings;

    string_last_id = ( string_last_id + 64 ) & ~0x3F;

    string_reserved = string_last_id;
    string_bmp_start = string_last_id >> 6;

    free( string_offset );
}

/****************************************************************************/
/* FUNCTION : string_use                                                    */
/****************************************************************************/
/* int code: identifier of the string you are using                         */
/****************************************************************************/
/* Increase the usage counter of an string. Use this when you store the     */
/* identifier of the string somewhere.                                      */
/****************************************************************************/

void string_use( int64_t code ) {
    string_uct[code]++;
}

/****************************************************************************/
/* FUNCTION : string_discard                                                */
/****************************************************************************/
/* int code: identifier of the string you don't need anymore                */
/****************************************************************************/
/* Decrease the usage counter of an string. Use this when you retrieve the  */
/* identifier of the string and discard it, or some memory (like private    */
/* variables) containing the string identifier is destroyed. If the usage   */
/* count is decreased to zero, the string will be discarted, and the        */
/* identifier may be used in the future by other string.                    */
/****************************************************************************/

void string_discard( int64_t code ) {
    if ( code < 0 || code > string_allocated || !string_ptr[code] || !string_uct[code] ) return;

    string_uct[code]--;

    if ( !string_uct[code] ) {
        if ( code >= string_reserved ) {
            free( string_ptr[code] );
            string_ptr[code] = NULL;
            bit_clr( string_bmp, code );
        }
    }
}

/****************************************************************************/
/* FUNCTION : string_getid                                                  */
/****************************************************************************/
/* Searchs for an available ID and returns it. If none available, more space*/
/* is allocated for the new string. This is used for new strings only.      */
/****************************************************************************/

static int64_t string_getid() {
    int64_t n, nb, lim, ini;

    /* If I have enough allocated, return the next one according to string_last_id */
    if ( string_last_id < string_allocated ) {
        if ( !bit_tst( string_bmp, string_last_id ) ) {
            bit_set( string_bmp, string_last_id );
            return string_last_id++;
        }
    }

    /* No more space, so I look for a free one between ~+64 from the last fixed and ~-64 from the last assigned */

    ini = ( string_last_id < string_allocated ) ? ( string_last_id >> 6 ) : string_reserved >> 6;
    lim = ( string_allocated >> 6 );

    while ( 1 ) {
        for ( n = ini; n < lim; n++ ) {
            if ( string_bmp[n] != ( uint64_t ) 0xFFFFFFFFFFFFFFFF ) { /* Here, there's 1 free, I'll find which one it is */
                for ( nb = 0; nb < 64; nb++ ) {
                    if ( !bit_tst( string_bmp + n, nb ) ) {
                        string_last_id = ( n << 6 ) + nb;
                        bit_set( string_bmp, string_last_id );
                        return string_last_id++;
                    }
                }
            }
        }
        if ( ini == string_reserved >> 6) break;
        lim = ini;
        ini = string_reserved >> 6;
    }

    string_last_id = string_allocated;

    /* Increment space, there were no free ones */
    string_alloc( BLOCK_INCR );

//    assert( !bit_tst( string_bmp, string_last_id ) );

    /* Return string_last_id and increment by 1, as now I have BLOCK_INCR more than before */
    bit_set( string_bmp, string_last_id );
    return string_last_id++;
}

/****************************************************************************/
/* FUNCTION : string_new                                                    */
/****************************************************************************/
/* Create a new string. It returns its ID. Note that it uses strdup()       */
/****************************************************************************/

int64_t string_new( const char * ptr ) {
    unsigned char * str = strdup( ptr );
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }

//    assert( str );

    id = string_getid();

    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/*
 *  FUNCTION : string_newa
 *
 *  Create a new string from a text buffer section
 *
 *  PARAMS:
 *              ptr         Pointer to the text buffer at start position
 *              count       Number of characters
 *
 *  RETURN VALUE:
 *      ID of the new string
 */

int64_t string_newa( const unsigned char * ptr, unsigned count ) {
    unsigned char * str = malloc( count + 1 );
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );
    id = string_getid();

    strncpy( str, ptr, count );
    str[count] = '\0';
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_concat                                                 */
/****************************************************************************/
/* Add some text to an string and return the resulting string. This does not*/
/* modify the original string, but creates a new one.                       */
/****************************************************************************/

int64_t string_concat( int64_t code1, unsigned char * str2 ) {
    unsigned char * str1;
    int len1, len2;

    if ( code1 >= string_allocated || code1 < 0 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( code1 < string_allocated && code1 >= 0 );

    str1 = string_ptr[code1];
    if ( !str1 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str1 );

    len1 = strlen( str1 );
    len2 = strlen( str2 ) + 1;

    str1 = ( char * ) realloc( str1, len1 + len2 );
    if ( !str1 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str1 );

    memmove( str1 + len1, str2, len2 );

    string_ptr[code1] = str1;

    return code1;
}

/****************************************************************************/
/* FUNCTION : string_add                                                    */
/****************************************************************************/
/* Add an string to another one and return the resulting string. This does  */
/* not modify the original string, but creates a new one.                   */
/****************************************************************************/

int64_t string_add( int64_t code1, int64_t code2 ) {
    const unsigned char * str1 = string_get( code1 );
    const unsigned char * str2 = string_get( code2 );
    unsigned char * str3;
    int64_t id;
    int len1, len2;

    if ( !str1 || !str2 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str1 );
//    assert( str2 );

    len1 = strlen( str1 );
    len2 = strlen( str2 ) + 1;

    str3 = ( char * ) malloc( len1 + len2 );
    if ( !str3 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str3 );

    memmove( str3, str1, len1 );
    memmove( str3 + len1, str2, len2 );

    id = string_getid();

    string_ptr[id] = str3;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_ptoa                                                   */
/****************************************************************************/
/* Convert a pointer to a new created string and return its ID.             */
/****************************************************************************/

int64_t string_ptoa( void * n ) {
    unsigned char * str;
    int64_t id;

    str = ( unsigned char * ) malloc( 17 );
    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str ) ;

    _string_ptoa( str, n );

    id = string_getid();
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_ftoa                                                   */
/****************************************************************************/
/* Convert a float to a new created string and return its ID.               */
/****************************************************************************/

int64_t string_ftoa( double n ) {
    unsigned char * str = ( unsigned char * ) malloc( 384 ), * ptr = str;
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str ) ;

    ptr += sprintf( str, "%lf", n ) - 1;

    while ( ptr >= str ) {
        if ( *ptr != '0' ) break;
        *ptr-- = 0;
    }
    if ( ptr >= str && *ptr == '.' ) *ptr = 0;
    if ( *str == 0 ) {
        *str = '0';
        *( str + 1 ) = '\0';
    }

    id = string_getid();
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_itoa                                                   */
/****************************************************************************/
/* Convert an integer to a new created string and return its ID.            */
/****************************************************************************/

int64_t string_itoa( int64_t n ) {
    unsigned char * str;
    int64_t id;

    str = ( unsigned char * ) malloc( 22 );
    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str ) ;

    _string_ntoa( str, n );

    id = string_getid();
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_uitoa                                                  */
/****************************************************************************/
/* Convert an unsigned integer to a new created string and return its ID.   */
/****************************************************************************/

int64_t string_uitoa( uint64_t n ) {
    unsigned char * str;
    int64_t id;

    str = ( unsigned char * ) malloc( 22 );
    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str ) ;

    _string_utoa( str, n );

    id = string_getid();
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/****************************************************************************/
/* FUNCTION : string_comp                                                   */
/****************************************************************************/
/* Compare two strings using strcmp and return the result                   */
/****************************************************************************/

int64_t string_comp( int64_t code1, int64_t code2 ) {
    const unsigned char * str1 = string_get( code1 );
    const unsigned char * str2 = string_get( code2 );
    return strcmp( str1, str2 );
}

/****************************************************************************/
/* FUNCTION : string_char                                                   */
/****************************************************************************/
/* Extract a character from a string. The parameter nchar can be:           */
/* - 0 or positive: return this character from the left (0 = leftmost)      */
/* - negative: return this character from the right (-1 = rightmost)        */
/* The result is not the ASCII value, but the identifier of a new string    */
/* that is create in the process and contains only the extracted character  */
/****************************************************************************/

uint64_t string_char( int64_t n, int nchar ) {
    const unsigned char * str = ( const unsigned char * ) string_get( n );

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );

    if ( nchar < 0 ) {
        nchar = strlen( str ) + nchar;
        if ( nchar < 0 ) return 0;
    }

    return str[nchar];
}

/****************************************************************************/
/* FUNCTION : string_substr                                                 */
/****************************************************************************/
/* Extract a substring from a string. The parameters can be:                */
/* - 0 or positive: count this character from the left (0 = leftmost)       */
/* - negative: count this character from the right (-1 = rightmost)         */
/*                                                                          */
/* negative value on len remove characters ( if len < 0 then use len + 1 )  */
/* NO MORE: If first > last, the two values are swapped before returning    */
/*          the result                                                      */
/****************************************************************************/

int64_t string_substr( int64_t code, int first, int len ) {
    const unsigned char * str = string_get( code );
    unsigned char * ptr;
    int rlen;
    int64_t n;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );
    rlen = strlen( str );

    if ( first < 0 ) {
        first = rlen + first;
//        if ( first < 0 ) return string_new( "" );
        if ( first < 0 ) first = 0;
    } else
        if ( first > ( rlen - 1 ) ) return string_new( "" );

    if ( len < 0 ) {
        len = ( rlen - first ) + ( len + 1 );
//        len = rlen + ( len + 2 ) - first - 1;
        if ( len < 1 ) return string_new( "" );
    }

    if ( first + len > rlen ) len = ( rlen - first );

    ptr = ( unsigned char * ) malloc( len + 1 );
    if ( !ptr ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
    memcpy( ptr, str + first, len );
    ptr[len] = '\0';

    n = string_getid();
    string_ptr[n] = ptr;
    string_uct[n] = 0;

    return n;
}

/*
 *  FUNCTION : string_find
 *
 *  Find a substring. Returns the position of the leftmost character (0
 *  for the leftmost position) or -1 if the string was not found.
 *
 *  PARAMS:
 *              code1                   Code of the string
 *              code2                   Code of the substring
 *              first                   Character to start the search
 *                                      (negative to search backwards)
 *
 *  RETURN VALUE:
 *      Result of the comparison
 */

int64_t string_find( int64_t code1, int64_t code2, int first ) {
    unsigned char * str1 = ( unsigned char * ) string_get( code1 );
    unsigned char * str2 = ( unsigned char * ) string_get( code2 );
    unsigned char * p = str1, * p1, * p2;

    if ( !str1 || !str2 ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str1 && str2 );

    if ( first < 0 ) {
        first += strlen( str1 );
        if ( first < 0 ) return -1;
        str1 += first;
    } else {
        /* Avoid use strlen */
        while ( first-- && *str1 ) str1++;
        if ( !*str1 ) return -1;
    }

    while ( *str1 ) {
        if ( *str1 == *str2 ) {
            p1 = str1 + 1;
            p2 = str2 + 1;

            while ( *p1 && *p2 && *p1 == *p2 )
            {
                p1++;
                p2++;
            }
            if ( !*p2 ) return str1 - p;
        }
        str1++;
    }

    return -1;
}

/*
 *  FUNCTION : string_ucase
 *
 *  Convert an string to upper case. It does not alter the given string, but
 *  creates a new string in the correct case and returns its id.
 *
 *  PARAMS:
 *              code                    Internal code of original string
 *
 *  RETURN VALUE:
 *      Code of the resulting string
 */

int64_t string_ucase( int64_t code ) {
    const unsigned char * str = string_get( code );
    unsigned char * base, * ptr;
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );

    base = ( unsigned char * ) malloc( strlen( str ) + 1 );
    if ( !base ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( base );

    for ( ptr = base; *str; ptr++, str++ ) *ptr = TOUPPER( *str );
    ptr[0] = '\0';

    id = string_getid();
    string_ptr[id] = base;
    string_uct[id] = 0;

    return id;
}

/*
 *  FUNCTION : string_lcase
 *
 *  Convert an string to lower case. It does not alter the given string, but
 *  creates a new string in the correct case and returns its id.
 *
 *  PARAMS:
 *              code                    Internal code of original string
 *
 *  RETURN VALUE:
 *      Code of the resulting string
 */

int64_t string_lcase( int64_t code ) {
    const unsigned char * str = string_get( code );
    unsigned char * base, * ptr;
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );

    base = ( unsigned char * ) malloc( strlen( str ) + 1 );
    if ( !base ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( base );

    for ( ptr = base; *str; ptr++, str++ ) *ptr = TOLOWER( *str );
    ptr[0] = '\0';

    id = string_getid();
    string_ptr[id] = base;
    string_uct[id] = 0;

    return id;
}

/*
 *  FUNCTION : string_strip
 *
 *  Create a copy of a string, without any leading or ending blanks
 *
 *  PARAMS:
 *              code                    Internal code of original string
 *
 *  RETURN VALUE:
 *      Code of the resulting string
 */

int64_t string_strip( int64_t code ) {
    const unsigned char * str = string_get( code );
    unsigned char * base, * ptr;
    int64_t id = string_new( str );

    ptr = base = ( unsigned char * )string_get( id );

    if ( !ptr ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( ptr );

    while ( *str == ' ' || *str == '\n' || *str == '\r' || *str == '\t' ) str++;
    while ( *str ) *ptr++ = *str++;
    while ( ptr > base && ( ptr[-1] == ' ' || ptr[-1] == '\n' || ptr[-1] == '\r' || ptr[-1] == '\t' ) ) ptr--;
    *ptr = '\0';

    return id;
}

/*
 *  FUNCTION : string_format
 *
 *  Format a number using the given characters
 *
 *  PARAMS:
 *              number                  Number to format
 *              spec                    Format specification
 *
 *  RETURN VALUE:
 *      Code of the resulting string
 */

int64_t string_format( double number, int dec, char point, char thousands ) {
    unsigned char * str = malloc( 384 );
    unsigned char * s = str, * t, * p = NULL;
    int c, neg;
    int64_t id;

    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );

    if ( dec == -1 ) s += sprintf( str, "%f", number );
    else             s += sprintf( str, "%.*f", dec, number );

    neg = (*str == '-') ? 1 : 0;

    if ( dec ) {
        p = str;
        while ( *p && *p != '.' ) p++;
        if ( *p ) *p = point;
    } else {
        p = s;
    }

    /* p = where decimal point is */
    /* s = where '\0' is */

    if ( thousands ) t = s + (p - (str + neg) - 1 ) / 3;
    else             t = s;

    c = 0;
    while ( s >= str ) {
        if ( isdigit( *s ) && s < p ) {
            if ( c == 3 ) {
                *t-- = thousands;
                c = 0;
                continue;
            } else
                c++;
        }
        *t-- = *s--;
    }

    id = string_getid();

    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

/*
 *  FUNCTION : string_casecmp
 *
 *  Compare two strings (case-insensitive version)
 *
 *  PARAMS:
 *              code1                   Code of the first string
 *              code2                   Code of the second string
 *
 *  RETURN VALUE:
 *      Result of the comparison
 */

int64_t string_casecmp( int64_t code1, int64_t code2 ) {
    unsigned char * str1 = ( unsigned char * ) string_get( code1 );
    unsigned char * str2 = ( unsigned char * ) string_get( code2 );

    while ( *str1 || *str2 ) {
        if ( TOUPPER( *str1 ) != TOUPPER( *str2 ) ) return TOUPPER( *str1 ) - TOUPPER( *str2 );

        str1++;
        str2++;
    }

    return 0;
}

/*
 *  FUNCTION : string_pad
 *
 *  Add spaces to the left or right of a string
 *
 *  PARAMS:
 *              code                    Code of the string
 *              total                   Total length of the final string
 *              align                   0 = align to the right; 1 = align to the left
 *
 *  RETURN VALUE:
 *      Result of the comparison
 */

int64_t string_pad( int64_t code, int total, int align ) {
    const unsigned char * ptr = string_get( code );
    int len, spaces = 0;
    int64_t id;
    unsigned char * str;

    if ( !ptr ) {
        fprintf( stderr, "ERROR: Runtime error - %s: internal error\n", __FUNCTION__ );
        exit(2);
    }
//    assert( ptr );
    len = strlen( ptr );
    if ( len < total ) spaces = total - len;

    if ( !spaces ) return string_new( ptr );

    str = malloc( total + 1 );
    if ( !str ) {
        fprintf( stderr, "ERROR: Runtime error - %s: out of memory\n", __FUNCTION__ );
        exit(2);
    }
//    assert( str );

    if ( !align ) {
        memset( str, ' ', spaces );
        strcpy( str + spaces, ptr );
    } else {
        strcpy( str, ptr );
        memset( str + len, ' ', spaces );
        str[total] = '\0';
    }

    id = string_getid();
    string_ptr[id] = str;
    string_uct[id] = 0;

    return id;
}

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
#include <math.h>

#include <inttypes.h>

#include "bgdrtm.h"
#include "dcb.h"

#include "sysprocs_p.h"
#include "pslang.h"
#include "instance.h"
#include "offsets.h"
#include "xstrings.h"

/* ---------------------------------------------------------------------- */
/* Interpreter's main module                                              */
/* ---------------------------------------------------------------------- */

int64_t exit_value = 0;
int64_t must_exit = 0;

int64_t frame_completed = 0;

int64_t debugger_show_console = 0;          // debuggin
int64_t debugger_trace = 0;                 // 1 single sentence
int64_t debugger_step = 0;                  // execute 1 sentence or 1 procedure or 1 function as an unit
// int64_t debugger_step_out = 0;           // execute until return or exit procedure or function
// int64_t debugger_goto_next_instance = 0; // break on next instance
// int64_t debugger_next_frame = 0;         // run until next frame is complete

int64_t trace_sentence = -1;
INSTANCE * trace_instance = NULL;

/* ---------------------------------------------------------------------- */

static int stack_dump( INSTANCE * r ) {
    register int64_t * ptr = &r->stack[1];
    register int i = 0;

    while ( ptr < r->stack_ptr ) {
        if ( i == 2 ) {
            i = 0;
            printf( "\n" );
        }
        printf( "%016"PRIX64" ", *ptr++ );
        i++;
    }

    return i;
}

/* ---------------------------------------------------------------------- */

int64_t instance_go_all() {
    INSTANCE * i = NULL;
    int64_t n, status, i_count;

    must_exit = 0;

    while ( first_instance ) {
        frame_completed = 0;

        // Reset iterator by priority
        instance_reset_iterator_by_priority(); // Don't must be neccessary
        i = instance_next_by_priority();

        i_count = 0;
        while ( i ) {
            if ( LOCINT64( i, FRAME_PERCENT ) < 100 ) {
                status = LOCQWORD( i, STATUS );
                if ( status == STATUS_RUNNING ) {
                    /* Run instance */
                    /* Hook */
                    if ( process_exec_hook_count )
                        for ( n = 0; n < process_exec_hook_count; n++ )
                            process_exec_hook_list[n]( i );
                    /* Hook */
                } else if ( status & ~( STATUS_KILLED | STATUS_DEAD ) ) { /* STATUS_SLEEPING OR STATUS_FROZEN OR STATUS_WAITING_MASK OR STATUS_PAUSED_MASK */
                    i = instance_next_by_priority();
                    continue;
                }
                /* If instance is KILLED or DEAD, run instance without exec_hook executed. */

                instance_go( i );

                i_count++;

                if ( must_exit ) goto instance_go_all_exit;

            }

            i = instance_next_by_priority();
        }

        /* If frame is complete, then update internal vars and execute main hooks. */

        if ( !i_count ) {
            frame_completed = 1;
            /* Honors the signal-changed status of the process and
             * saves so it is used in this loop the next frame
             */
            i = first_instance;
            while ( i ) {
                status = LOCQWORD( i, SAVED_STATUS ) = LOCQWORD( i, STATUS );
                // if status == STATUS_KILLED or STATUS_DEAD then the process still lives
                if ( status == STATUS_DEAD || status == STATUS_KILLED || status == STATUS_RUNNING ) LOCINT64( i, FRAME_PERCENT ) -= 100;

                if ( i->last_priority != LOCINT64( i, PRIORITY ) ) {
                    instance_dirty( i );
                    LOCINT64( i, SAVED_PRIORITY ) = LOCINT64( i, PRIORITY );
                }

                i = i->next;
            }

            if ( !first_instance ) break;

            /* Hook */
            if ( handler_hook_count )
                for ( n = 0; n < handler_hook_count; n++ )
                    handler_hook_list[n].hook();
            /* Hook */

            continue;
        }
    }

instance_go_all_exit:

    return exit_value;

}

/* ---------------------------------------------------------------------- */

int64_t instance_go( INSTANCE * r ) {
    if ( !r ) return 0;

    register int64_t * ptr = r->codeptr;

    uint64_t return_value = LOCQWORD( r, PROCESS_ID );
    int64_t n, sz;
    SYSPROC * p = NULL;
    INSTANCE * i = NULL;
    char * str = NULL;
    int64_t status;

    /* Pointer to the current process's code (it may be a called one) */

    int64_t child_is_alive = 0;

    int64_t debugger_step_pending = 0; // local to instance

    /* ------------------------------------------------------------------------------- */
    /* Restore if exit by debug                                                        */

    if ( debug > 0 ) {
        printf( "\n>>> Instance:%s ProcID:%" PRId64 " StackUsed:%" PRId64 "/%" PRId64 "\n", r->proc->name,
                                                                 LOCQWORD( r, PROCESS_ID ),
                                                                 ( int64_t )
                                                                 ( ( void * ) r->stack_ptr - ( void * ) r->stack ) / sizeof( r->stack[0] ),
                                                                 ( r->stack[0] & ~STACK_RETURN_VALUE )
              );
    }

    /* Start process or return from frame */
    /* Hook */
    if ( instance_pre_execute_hook_count )
        for ( n = 0; n < instance_pre_execute_hook_count; n++ )
            instance_pre_execute_hook_list[n]( r );
    /* Hook */

    // breakpoint on entry
    if ( r->proc->breakpoint || r->breakpoint ) {
        debugger_show_console = 1; // *** call debugger when info is available
        debugger_trace = 1;
    }

main_loop_instance_go:
    trace_sentence = -1;

    while ( !must_exit ) {
        /* If I was killed or I'm waiting status, then exit */
        status = LOCQWORD( r, STATUS );
        if ( status & ( STATUS_KILLED | STATUS_WAITING_MASK | STATUS_PAUSED_MASK ) ) {
            r->codeptr = ptr;
            return_value = LOCQWORD( r, PROCESS_ID );
            goto break_all;
        }

        if ( trace_sentence != -1 ) {
             while( debugger_show_console ) {
                /* Hook */
                if ( handler_hook_count )
                    for ( n = 0; n < handler_hook_count; n++ )
                        handler_hook_list[n].hook();
                /* Hook */
            }
        }

        /* debug output */
        if ( debug > 0 ) {
            if ( debug > 2 ) {
                int c = 34 - stack_dump( r ) * 17;
                if ( debug > 1 ) printf( "%*.*s[%4" PRIu64 "] ", c, c, "", ( uint64_t ) ( ptr - r->code ) );
            }
            else if ( debug > 1 ) printf( "[%4" PRIu64 "] ", ( uint64_t ) ( ptr - r->code ) );
            mnemonic_dump( *ptr, ptr[1] );
            fflush(stdout);
        }

        switch ( *ptr ) {

            /* No operation */
            case MN_NOP:
                ptr++;
                break;

            /* Stack manipulation */

            case MN_DUP:
                *r->stack_ptr = r->stack_ptr[-1];
                r->stack_ptr++;
                ptr++;
                break;

            case MN_PUSH:
                *r->stack_ptr++ = ptr[1];
                ptr += 2;
                break;

            case MN_POP:
                r->stack_ptr--;
                ptr++;
                break;

            // array[var] (maybe data type is nonsense)
            case MN_INDEX | MN_QWORD:
            case MN_INDEX | MN_QWORD | MN_UNSIGNED:
            case MN_INDEX | MN_DWORD:
            case MN_INDEX | MN_DWORD | MN_UNSIGNED:
            case MN_INDEX | MN_WORD:
            case MN_INDEX | MN_WORD | MN_UNSIGNED:
            case MN_INDEX | MN_BYTE:
            case MN_INDEX | MN_BYTE | MN_UNSIGNED:
            case MN_INDEX | MN_STRING:
            case MN_INDEX | MN_FLOAT:
            case MN_INDEX | MN_DOUBLE:
                r->stack_ptr[-1] += ptr[1];
                ptr += 2;
                break;

            case MN_ARRAY:
                r->stack_ptr[-2] += ( ptr[1] * r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr += 2;
                break;

            /* Process calls */

            case MN_CLONE:
                i = instance_duplicate( r );
                i->codeptr = ptr + 2;
                ptr = r->code + ptr[1];
                continue;

            case MN_CALL:
            case MN_PROC:
            {
                PROCDEF * proc = procdef_get( ptr[1] );

                if ( !proc ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Unknown process\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }

                /* Process uses FRAME or locals, must create an instance */
                i = instance_new( proc, r );
                /* Can't create instante, return -1 */
                if ( !i ) {
                    r->stack_ptr -= proc->params;
                    if ( *ptr == MN_CALL ) {
                        r->stack[0] |= STACK_RETURN_VALUE;
                        *r->stack_ptr = -1;
                        r->stack_ptr++;
                    } else {
                        r->stack[0] &= ~STACK_RETURN_VALUE;
                    }
                    ptr += 2;
                    break;
                }

                for ( n = 0; n < proc->params; n++ )
                    PRIQWORD( i, sizeof( uint64_t ) * n ) = r->stack_ptr[-proc->params+n];

                r->stack_ptr -= proc->params;

                /* I go to waiting status (by default) */
                LOCQWORD( r, STATUS ) |= STATUS_WAITING_MASK;
                i->called_by   = r;

                if ( debugger_step ) {
                    debugger_step_pending = 1;
                    debugger_step = 0;
                }

                /* Run the process/function */
                if ( *ptr == MN_CALL ) {
                    r->stack[0] |= STACK_RETURN_VALUE;
                    r->stack_ptr++;
                    *r->stack_ptr = instance_go( i );
                } else {
                    r->stack[0] &= ~STACK_RETURN_VALUE;
                    instance_go( i );
                }

                if ( debugger_step_pending ) {
                    debugger_step_pending = 0;
                    debugger_step = 1;
                }

                child_is_alive = instance_exists( i );

                ptr += 2;

                /* If the process is a function in a frame, save the stack and leave */
                /* If the process/function still running, then it is in a FRAME.
                   If the process/function is running code, then it his status is RUNNING */
                if ( child_is_alive && LOCQWORD( r, STATUS ) & ( STATUS_WAITING_MASK | STATUS_FROZEN | STATUS_SLEEPING ) ) {
                    /* I go to sleep and return from this process/function */
                    i->called_by   = r;

                    /* Save the instruction pointer */
                    /* This instance don't run other code until the child return */
                    r->codeptr = ptr;

                    /* If it don't was a CALL, then I set a flag in "len" for no return value */
                    if ( ptr[-2] == MN_CALL )   r->stack[0] |= STACK_RETURN_VALUE;
                    else                        r->stack[0] &= ~STACK_RETURN_VALUE;

                    return 0;
                }

                /* Wake up! */
                LOCQWORD( r, STATUS ) &= ~STATUS_WAITING_MASK;
                if ( child_is_alive ) i->called_by = NULL;

                break;
            }

            case MN_SYSCALL:
                p = sysproc_get( ptr[1] );
                if ( !p ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Unknown system function\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }

                r->stack_ptr -= p->params;
                *r->stack_ptr = ( *p->func )( r, r->stack_ptr );
                r->stack_ptr++;
                ptr += 2;
                break;

            case MN_SYSPROC:
                p = sysproc_get( ptr[1] );
                if ( !p ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Unknown system process\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr -= p->params;
                ( *p->func )( r, r->stack_ptr );
                ptr += 2;
                break;

            /* Access to variables address */

            case MN_PRIVATE | MN_QWORD:
            case MN_PRIVATE | MN_QWORD | MN_UNSIGNED:
            case MN_PRIVATE | MN_DWORD:
            case MN_PRIVATE | MN_DWORD | MN_UNSIGNED:
            case MN_PRIVATE | MN_WORD:
            case MN_PRIVATE | MN_WORD | MN_UNSIGNED:
            case MN_PRIVATE | MN_BYTE:
            case MN_PRIVATE | MN_BYTE | MN_UNSIGNED:
            case MN_PRIVATE | MN_STRING:
            case MN_PRIVATE | MN_DOUBLE:
            case MN_PRIVATE | MN_FLOAT:
                *r->stack_ptr++ = ( uint64_t )( intptr_t )&PRIQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_PUBLIC | MN_QWORD:
            case MN_PUBLIC | MN_QWORD | MN_UNSIGNED:
            case MN_PUBLIC | MN_DWORD:
            case MN_PUBLIC | MN_DWORD | MN_UNSIGNED:
            case MN_PUBLIC | MN_WORD:
            case MN_PUBLIC | MN_WORD | MN_UNSIGNED:
            case MN_PUBLIC | MN_BYTE:
            case MN_PUBLIC | MN_BYTE | MN_UNSIGNED:
            case MN_PUBLIC | MN_STRING:
            case MN_PUBLIC | MN_DOUBLE:
            case MN_PUBLIC | MN_FLOAT:
                *r->stack_ptr++ = ( uint64_t )( intptr_t )&PUBQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_LOCAL | MN_QWORD:
            case MN_LOCAL | MN_QWORD | MN_UNSIGNED:
            case MN_LOCAL | MN_DWORD:
            case MN_LOCAL | MN_DWORD | MN_UNSIGNED:
            case MN_LOCAL | MN_WORD:
            case MN_LOCAL | MN_WORD | MN_UNSIGNED:
            case MN_LOCAL | MN_BYTE:
            case MN_LOCAL | MN_BYTE | MN_UNSIGNED:
            case MN_LOCAL | MN_STRING:
            case MN_LOCAL | MN_DOUBLE:
            case MN_LOCAL | MN_FLOAT:
                *r->stack_ptr++ = ( uint64_t )( intptr_t )&LOCQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GLOBAL | MN_QWORD:
            case MN_GLOBAL | MN_QWORD | MN_UNSIGNED:
            case MN_GLOBAL | MN_DWORD:
            case MN_GLOBAL | MN_DWORD | MN_UNSIGNED:
            case MN_GLOBAL | MN_WORD:
            case MN_GLOBAL | MN_WORD | MN_UNSIGNED:
            case MN_GLOBAL | MN_BYTE:
            case MN_GLOBAL | MN_BYTE | MN_UNSIGNED:
            case MN_GLOBAL | MN_STRING:
            case MN_GLOBAL | MN_DOUBLE:
            case MN_GLOBAL | MN_FLOAT:
                *r->stack_ptr++ = ( uint64_t )( intptr_t )&GLOQWORD( ptr[1] );
                ptr += 2;
                break;

            case MN_REMOTE | MN_QWORD:
            case MN_REMOTE | MN_QWORD | MN_UNSIGNED:
            case MN_REMOTE | MN_DWORD:
            case MN_REMOTE | MN_DWORD | MN_UNSIGNED:
            case MN_REMOTE | MN_WORD:
            case MN_REMOTE | MN_WORD | MN_UNSIGNED:
            case MN_REMOTE | MN_BYTE:
            case MN_REMOTE | MN_BYTE | MN_UNSIGNED:
            case MN_REMOTE | MN_STRING:
            case MN_REMOTE | MN_DOUBLE:
            case MN_REMOTE | MN_FLOAT:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = ( uint64_t )( intptr_t )&LOCQWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_REMOTE_PUBLIC | MN_QWORD:
            case MN_REMOTE_PUBLIC | MN_QWORD | MN_UNSIGNED:
            case MN_REMOTE_PUBLIC | MN_DWORD:
            case MN_REMOTE_PUBLIC | MN_DWORD | MN_UNSIGNED:
            case MN_REMOTE_PUBLIC | MN_WORD:
            case MN_REMOTE_PUBLIC | MN_WORD | MN_UNSIGNED:
            case MN_REMOTE_PUBLIC | MN_BYTE:
            case MN_REMOTE_PUBLIC | MN_BYTE | MN_UNSIGNED:
            case MN_REMOTE_PUBLIC | MN_STRING:
            case MN_REMOTE_PUBLIC | MN_DOUBLE:
            case MN_REMOTE_PUBLIC | MN_FLOAT:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = ( uint64_t )( intptr_t )&PUBQWORD( i, ptr[1] );
                ptr += 2;
                break;

            /* Access to variables QWORD type */

            case MN_GET_PRIV | MN_QWORD:
            case MN_GET_PRIV | MN_QWORD | MN_UNSIGNED:
            case MN_GET_PRIV | MN_DOUBLE:
                *r->stack_ptr++ = PRIQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_QWORD:
            case MN_GET_PUBLIC | MN_QWORD | MN_UNSIGNED:
            case MN_GET_PUBLIC | MN_DOUBLE:
                *r->stack_ptr++ = PUBQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_QWORD:
            case MN_GET_LOCAL | MN_QWORD | MN_UNSIGNED:
            case MN_GET_LOCAL | MN_DOUBLE:
                *r->stack_ptr++ = LOCQWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_QWORD:
            case MN_GET_GLOBAL | MN_QWORD | MN_UNSIGNED:
            case MN_GET_GLOBAL | MN_DOUBLE:
                *r->stack_ptr++ = GLOQWORD( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_QWORD:
            case MN_GET_REMOTE | MN_QWORD | MN_UNSIGNED:
            case MN_GET_REMOTE | MN_DOUBLE:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCQWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_QWORD:
            case MN_GET_REMOTE_PUBLIC | MN_QWORD | MN_UNSIGNED:
            case MN_GET_REMOTE_PUBLIC | MN_DOUBLE:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBQWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_PTR | MN_QWORD:
            case MN_PTR | MN_QWORD | MN_UNSIGNED:
            case MN_PTR | MN_DOUBLE:
                r->stack_ptr[-1] = *( int64_t * )( intptr_t )r->stack_ptr[-1];
                ptr++;
                break;

            /* Access to variables DWORD type */

            case MN_GET_PRIV | MN_DWORD:
                *r->stack_ptr++ = PRIINT32( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PRIV | MN_DWORD | MN_UNSIGNED:
            case MN_GET_PRIV | MN_FLOAT:
                *r->stack_ptr++ = PRIDWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_DWORD:
                *r->stack_ptr++ = PUBINT32( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_DWORD | MN_UNSIGNED:
            case MN_GET_PUBLIC | MN_FLOAT:
                *r->stack_ptr++ = PUBDWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_DWORD:
                *r->stack_ptr++ = LOCINT32( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_DWORD | MN_UNSIGNED:
            case MN_GET_LOCAL | MN_FLOAT:
                *r->stack_ptr++ = LOCDWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_DWORD:
                *r->stack_ptr++ = GLOINT32( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_DWORD | MN_UNSIGNED:
            case MN_GET_GLOBAL | MN_FLOAT:
                *r->stack_ptr++ = GLODWORD( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_DWORD:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCINT32( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_DWORD | MN_UNSIGNED:
            case MN_GET_REMOTE | MN_FLOAT:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCDWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_DWORD:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBINT32( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_DWORD | MN_UNSIGNED:
            case MN_GET_REMOTE_PUBLIC | MN_FLOAT:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBDWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_PTR | MN_DWORD:
                r->stack_ptr[-1] = *( int32_t * )( intptr_t )r->stack_ptr[-1];
                ptr++;
                break;

            case MN_PTR | MN_DWORD | MN_UNSIGNED:
            case MN_PTR | MN_FLOAT:
                r->stack_ptr[-1] = *( uint32_t * )( intptr_t )r->stack_ptr[-1];
                ptr++;
                break;

            /* Access to variables STRING type */

            case MN_PUSH | MN_STRING:
                *r->stack_ptr++ = ptr[1];
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_PRIV | MN_STRING:
                *r->stack_ptr++ = PRIQWORD( r, ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_STRING:
                *r->stack_ptr++ = PUBQWORD( r, ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_STRING:
                *r->stack_ptr++ = LOCQWORD( r, ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_STRING:
                *r->stack_ptr++ = GLOQWORD( ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_STRING:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCQWORD( i, ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_STRING:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBQWORD( i, ptr[1] );
                string_use( r->stack_ptr[-1] );
                ptr += 2;
                break;

            case MN_PTR | MN_STRING:
                r->stack_ptr[-1] = *( int64_t * )( intptr_t )r->stack_ptr[-1];
                string_use( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_POP | MN_STRING:
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* Access to variables WORD type */

            case MN_GET_PRIV | MN_WORD:
                *r->stack_ptr++ = PRIINT16( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PRIV | MN_WORD | MN_UNSIGNED:
                *r->stack_ptr++ = PRIWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_WORD:
                *r->stack_ptr++ = PUBINT16( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_WORD | MN_UNSIGNED:
                *r->stack_ptr++ = PUBWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_WORD:
                *r->stack_ptr++ = LOCINT16( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_WORD | MN_UNSIGNED:
                *r->stack_ptr++ = LOCWORD( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_WORD:
                *r->stack_ptr++ = GLOINT16( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_WORD | MN_UNSIGNED:
                *r->stack_ptr++ = GLOWORD( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_WORD:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCINT16( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_WORD | MN_UNSIGNED:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_WORD:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBINT16( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_WORD | MN_UNSIGNED:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBWORD( i, ptr[1] );
                ptr += 2;
                break;

            case MN_PTR | MN_WORD:
                r->stack_ptr[-1] = *( int16_t * )( intptr_t )r->stack_ptr[-1];
                ptr++;
                break;

            case MN_PTR | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-1] = *( uint16_t * )( intptr_t )r->stack_ptr[-1];
                ptr++;
                break;

            /* Access to variables BYTE type */

            case MN_GET_PRIV | MN_BYTE:
                *r->stack_ptr++ = PRIINT8( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PRIV | MN_BYTE | MN_UNSIGNED:
                *r->stack_ptr++ = PRIBYTE( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_BYTE:
                *r->stack_ptr++ = PUBINT8( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_PUBLIC | MN_BYTE | MN_UNSIGNED:
                *r->stack_ptr++ = PUBBYTE( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_BYTE:
                *r->stack_ptr++ = LOCINT8( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_LOCAL | MN_BYTE | MN_UNSIGNED:
                *r->stack_ptr++ = LOCBYTE( r, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_BYTE:
                *r->stack_ptr++ = GLOINT8( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_GLOBAL | MN_BYTE | MN_UNSIGNED:
                *r->stack_ptr++ = GLOBYTE( ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_BYTE:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCINT8( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE | MN_BYTE | MN_UNSIGNED:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = LOCBYTE( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_BYTE:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBINT8( i, ptr[1] );
                ptr += 2;
                break;

            case MN_GET_REMOTE_PUBLIC | MN_BYTE | MN_UNSIGNED:
                i = instance_get( r->stack_ptr[-1] );
                if ( !i ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Process %" PRId64 " not active\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), r->stack_ptr[-1] );
                    exit( 0 );
                }
                r->stack_ptr[-1] = PUBBYTE( i, ptr[1] );
                ptr += 2;
                break;

            case MN_PTR | MN_BYTE:
                r->stack_ptr[-1] = *(( int8_t * )( intptr_t )r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_PTR | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-1] = *(( uint8_t * )( intptr_t )r->stack_ptr[-1] );
                ptr++;
                break;

            /* Floating point math (double) */

            case MN_DOUBLE | MN_NEG:
                *( double * )&r->stack_ptr[-1] = -*(( double * ) &r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_DOUBLE | MN_NOT:
                *( double * )&r->stack_ptr[-1] = ( double ) !*(( double * ) &r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_DOUBLE | MN_ADD:
                *( double * )&r->stack_ptr[-2] += *(( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DOUBLE | MN_SUB:
                *( double * )&r->stack_ptr[-2] -= *(( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DOUBLE | MN_MUL:
                *( double * )&r->stack_ptr[-2] *= *(( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DOUBLE | MN_DIV:
                *( double * )&r->stack_ptr[-2] /= *(( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DOUBLE | MN_MOD:
                *( double * )&r->stack_ptr[-2] = fmod( *( double * )&r->stack_ptr[-2], *(( double * ) &r->stack_ptr[-1] ) );
                r->stack_ptr--;
                ptr++;
                break;

            /* Floating point math */

            case MN_FLOAT | MN_NEG:
                *( float * )&r->stack_ptr[-1] = -*(( float * ) &r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_FLOAT | MN_NOT:
                *( float * )&r->stack_ptr[-1] = ( float ) !*(( float * ) &r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_FLOAT | MN_ADD:
                *( float * )&r->stack_ptr[-2] += *(( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_FLOAT | MN_SUB:
                *( float * )&r->stack_ptr[-2] -= *(( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_FLOAT | MN_MUL:
                *( float * )&r->stack_ptr[-2] *= *(( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_FLOAT | MN_DIV:
                *( float * )&r->stack_ptr[-2] /= *(( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_FLOAT | MN_MOD:
                *( float * )&r->stack_ptr[-2] = fmod( *( float * )&r->stack_ptr[-2], *(( float * ) &r->stack_ptr[-1] ) );
                r->stack_ptr--;
                ptr++;
                break;

            /* Convert */

            case MN_DOUBLE2INT | MN_QWORD:
                r->stack_ptr[-ptr[1] - 1] = ( int64_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint64_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_DWORD:
                r->stack_ptr[-ptr[1] - 1] = ( int32_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint32_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_WORD:
                r->stack_ptr[-ptr[1] - 1] = ( int16_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint16_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_BYTE:
                r->stack_ptr[-ptr[1] - 1] = ( int8_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2INT | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint8_t ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_QWORD:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( int64_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_QWORD | MN_UNSIGNED:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( uint64_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_DWORD:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( int32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_DWORD | MN_UNSIGNED:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( uint32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_WORD:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( int16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_UNSIGNED | MN_WORD:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( uint16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_BYTE:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( int8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DOUBLE | MN_UNSIGNED | MN_BYTE:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) ( uint8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_QWORD:
                r->stack_ptr[-ptr[1] - 1] = ( int64_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint64_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_DWORD:
                r->stack_ptr[-ptr[1] - 1] = ( int32_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint32_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_WORD:
                r->stack_ptr[-ptr[1] - 1] = ( int16_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint16_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_BYTE:
                r->stack_ptr[-ptr[1] - 1] = ( int8_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2INT | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint8_t ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_QWORD:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( int64_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_QWORD | MN_UNSIGNED:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( uint64_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_DWORD:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( int32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_DWORD | MN_UNSIGNED:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( uint32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_WORD:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( int16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_UNSIGNED | MN_WORD:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( uint16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_BYTE:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( int8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2FLOAT | MN_UNSIGNED | MN_BYTE:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) ( uint8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DWORD:
                r->stack_ptr[-ptr[1] - 1] = ( int32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2DWORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint32_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2WORD:
                r->stack_ptr[-ptr[1] - 1] = ( int16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2WORD | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint16_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2BYTE:
                r->stack_ptr[-ptr[1] - 1] = ( int8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_INT2BYTE | MN_UNSIGNED:
                r->stack_ptr[-ptr[1] - 1] = ( uint8_t ) r->stack_ptr[-ptr[1] - 1];
                ptr += 2;
                break;

            case MN_DOUBLE2FLOAT:
                *( float * )&( r->stack_ptr[-ptr[1] - 1] ) = ( float ) * ( double * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2DOUBLE:
                *( double * )&( r->stack_ptr[-ptr[1] - 1] ) = ( double ) * ( float * ) &( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            /* Mathematical operations */

            case MN_NEG:
            case MN_NEG | MN_UNSIGNED:
                r->stack_ptr[-1] = -r->stack_ptr[-1];
                ptr++;
                break;

            case MN_NOT:
            case MN_NOT | MN_UNSIGNED:
                r->stack_ptr[-1] = !( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_ADD:
            case MN_ADD | MN_UNSIGNED:
                r->stack_ptr[-2] += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_SUB:
            case MN_SUB | MN_UNSIGNED:
                r->stack_ptr[-2] -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MUL | MN_QWORD:
            case MN_MUL | MN_DWORD:
            case MN_MUL | MN_WORD:
            case MN_MUL | MN_BYTE:
                r->stack_ptr[-2] *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MUL | MN_QWORD | MN_UNSIGNED:
            case MN_MUL | MN_DWORD | MN_UNSIGNED:
            case MN_MUL | MN_WORD | MN_UNSIGNED:
            case MN_MUL | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-2] = ( uint64_t )r->stack_ptr[-2] * ( uint64_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DIV | MN_QWORD:
            case MN_DIV | MN_DWORD:
            case MN_DIV | MN_WORD:
            case MN_DIV | MN_BYTE:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] /= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_DIV | MN_QWORD | MN_UNSIGNED:
            case MN_DIV | MN_DWORD | MN_UNSIGNED:
            case MN_DIV | MN_WORD | MN_UNSIGNED:
            case MN_DIV | MN_BYTE | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] = ( uint64_t )r->stack_ptr[-2] / ( uint64_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_QWORD:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] %= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_DWORD:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] %= ( int32_t ) r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_WORD:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] %= ( int16_t ) r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_BYTE:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] %= ( int8_t ) r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_QWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] = ( uint64_t )r->stack_ptr[-2] % ( uint64_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_DWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] = ( uint32_t )r->stack_ptr[-2] % ( uint32_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_WORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] = ( uint16_t )r->stack_ptr[-2] % ( uint16_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_MOD | MN_BYTE | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                r->stack_ptr[-2] = ( uint8_t )r->stack_ptr[-2] % ( uint8_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* Bitwise operations */

            case MN_ROR | MN_QWORD:
                ( r->stack_ptr[-2] ) = (( int64_t )r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint64_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_DWORD:
                r->stack_ptr[-2] = (( int32_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint32_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_WORD:
                r->stack_ptr[-2] = (( int16_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint16_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_BYTE:
                r->stack_ptr[-2] = (( int8_t ) r->stack_ptr[-2] >> r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROR | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint8_t ) r->stack_ptr[-2] ) >> r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_QWORD:
                ( r->stack_ptr[-2] ) = (( int64_t )r->stack_ptr[-2] ) << r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* All the next ROL operations, don't could be necessaries, but well... */

            case MN_ROL | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = ( uint64_t )( r->stack_ptr[-2] << r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_DWORD:
                r->stack_ptr[-2] = (( int32_t )r->stack_ptr[-2] ) << r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = ( uint32_t )( r->stack_ptr[-2] << r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_WORD:
                r->stack_ptr[-2] = (( int16_t )r->stack_ptr[-2] ) << r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-2] = ( uint16_t )( r->stack_ptr[-2] << r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_BYTE:
                r->stack_ptr[-2] = (( int8_t )r->stack_ptr[-2] ) << r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ROL | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-2] = ( uint8_t )( r->stack_ptr[-2] << r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_BAND:
            case MN_BAND | MN_UNSIGNED:
                r->stack_ptr[-2] &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_BOR:
            case MN_BOR | MN_UNSIGNED:
                r->stack_ptr[-2] |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_BXOR:
            case MN_BXOR | MN_UNSIGNED:
                r->stack_ptr[-2] ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_BNOT | MN_QWORD:
            case MN_BNOT | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-1] = ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_BYTE:
                r->stack_ptr[-1] = ( int8_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_BYTE | MN_UNSIGNED:
                r->stack_ptr[-1] = ( uint8_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_WORD:
                r->stack_ptr[-1] = ( int16_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_WORD | MN_UNSIGNED:
                r->stack_ptr[-1] = ( uint16_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_DWORD:
                r->stack_ptr[-1] = ( int32_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_BNOT | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-1] = ( uint32_t ) ~( r->stack_ptr[-1] );
                ptr++;
                break;

            /* Logical operations */

            case MN_AND:
                r->stack_ptr[-2] = r->stack_ptr[-2] && r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_OR:
                r->stack_ptr[-2] = r->stack_ptr[-2] || r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_XOR:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] != 0 ) ^( r->stack_ptr[-1] != 0 );
                r->stack_ptr--;
                ptr++;
                break;

            /* Comparisons */

            case MN_EQ | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] == r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_NE | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] != r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] >= r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint64_t )r->stack_ptr[-2] >= ( uint64_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] <= r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint64_t )r->stack_ptr[-2] <= ( uint64_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] < r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint64_t )r->stack_ptr[-2] < ( uint64_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_QWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] > r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_QWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint64_t )r->stack_ptr[-2] > ( uint64_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

//            case MN_EQ:
            case MN_EQ | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] == r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_NE | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] != r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] >= r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint32_t )r->stack_ptr[-2] >= ( uint32_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] <= r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint32_t )r->stack_ptr[-2] <= ( uint32_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] < r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint32_t )r->stack_ptr[-2] < ( uint32_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_DWORD:
                r->stack_ptr[-2] = ( r->stack_ptr[-2] > r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_DWORD | MN_UNSIGNED:
                r->stack_ptr[-2] = (( uint32_t )r->stack_ptr[-2] > ( uint32_t )r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* Floating point comparisons (double) */

            case MN_EQ | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] == *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_NE | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] != *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] >= *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] <= *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] < *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_DOUBLE:
                r->stack_ptr[-2] = ( *( double * ) &r->stack_ptr[-2] > *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* Floating point comparisons */

            case MN_EQ | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] == *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_NE | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] != *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] >= *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] <= *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] < *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_FLOAT:
                r->stack_ptr[-2] = ( *( float * ) &r->stack_ptr[-2] > *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* String comparisons */

            case MN_EQ | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) == 0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            case MN_NE | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) != 0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GTE | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) >= 0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LTE | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) <= 0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LT | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) <  0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            case MN_GT | MN_STRING :
                n = string_comp( r->stack_ptr[-2], r->stack_ptr[-1] ) >  0;
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = n;
                r->stack_ptr--;
                ptr++;
                break;

            /* String operations */

            case MN_VARADD | MN_STRING:
                n = *( int64_t * )( intptr_t )( r->stack_ptr[-2] );
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) = string_add( n, r->stack_ptr[-1] );
                string_use( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) );
                string_discard( n );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LETNP | MN_STRING:
                string_discard( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) );
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_STRING:
                string_discard( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) );
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_ADD | MN_STRING:
                n = string_add( r->stack_ptr[-2], r->stack_ptr[-1] );
                string_use( n );
                string_discard( r->stack_ptr[-2] );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr--;
                r->stack_ptr[-1] = n;
                ptr++;
                break;

            case MN_INT2STR | MN_QWORD:
                r->stack_ptr[-ptr[1] - 1] = string_itoa( r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_UNSIGNED | MN_QWORD:
                r->stack_ptr[-ptr[1] - 1] = string_uitoa( r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_DWORD:
                r->stack_ptr[-ptr[1] - 1] = string_itoa( ( int32_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_UNSIGNED | MN_DWORD:
                r->stack_ptr[-ptr[1] - 1] = string_uitoa( ( uint32_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_WORD:
                r->stack_ptr[-ptr[1] - 1] = string_itoa( ( int16_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_UNSIGNED | MN_WORD:
                r->stack_ptr[-ptr[1] - 1] = string_uitoa( ( uint16_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_BYTE:
                r->stack_ptr[-ptr[1] - 1] = string_itoa( ( int8_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_INT2STR | MN_UNSIGNED | MN_BYTE:
                r->stack_ptr[-ptr[1] - 1] = string_uitoa( ( uint8_t ) r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_FLOAT2STR:
                r->stack_ptr[-ptr[1] - 1] = string_ftoa( *( float * ) &r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_DOUBLE2STR:
                r->stack_ptr[-ptr[1] - 1] = string_ftoa( *( double * ) &r->stack_ptr[-ptr[1] - 1] );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_CHR2STR:
            {
                char buffer[2];
                buffer[0] = ( uint8_t )r->stack_ptr[-ptr[1] - 1];
                buffer[1] = 0;
                r->stack_ptr[-ptr[1] - 1] = string_new( buffer );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;
            }

            case MN_STRI2CHR:
                n = string_char( r->stack_ptr[-2], r->stack_ptr[-1] );
                string_discard( r->stack_ptr[-2] );
                r->stack_ptr--;
                r->stack_ptr[-1] = n;
                ptr++;
                break;

            case MN_STR2CHR:
                n = r->stack_ptr[-ptr[1] - 1];
                r->stack_ptr[-1] = *string_get( n );
                string_discard( n );
                ptr += 2;
                break;

            case MN_STR2POINTER:
                n = r->stack_ptr[-ptr[1] - 1];
                r->stack_ptr[-ptr[1] - 1] = string_atop( ( char * )string_get( n ) );
                string_discard( n );
                ptr += 2;
                break;

            case MN_POINTER2STR:
                r->stack_ptr[-ptr[1] - 1] = string_ptoa( ( void * )( intptr_t )( r->stack_ptr[-ptr[1] - 1] ) );
                string_use( r->stack_ptr[-ptr[1] - 1] );
                ptr += 2;
                break;

            case MN_STR2DOUBLE:
                n = r->stack_ptr[-ptr[1] - 1];
                str = ( char * )string_get( n );
                *( double * )( &r->stack_ptr[-ptr[1] - 1] ) = str ? ( double )atof( str ) : 0.0f;
                string_discard( n );
                ptr += 2;
                break;

            case MN_STR2FLOAT:
                n = r->stack_ptr[-ptr[1] - 1];
                str = ( char * )string_get( n );
                *( float * )( &r->stack_ptr[-ptr[1] - 1] ) = str ? ( float )atof( str ) : 0.0f;
                string_discard( n );
                ptr += 2;
                break;

            case MN_STR2INT:
                n = r->stack_ptr[-ptr[1] - 1];
                str = ( char * )string_get( n );
                r->stack_ptr[-ptr[1] - 1] = str ? atoll( str ) : 0;
                string_discard( n );
                ptr += 2;
                break;

            /* Fixed-length strings operations*/

            case MN_A2STR:
                str = *( char ** )( &r->stack_ptr[-ptr[1] - 1] );
                n = string_new( str );
                string_use( n );
                r->stack_ptr[-ptr[1] - 1] = n;
                ptr += 2;
                break;

            case MN_STR2A:
                n = r->stack_ptr[-1];
                strncpy( *( char ** )( &r->stack_ptr[-2] ), string_get( n ), ptr[1] );
                (( char * )( intptr_t )( r->stack_ptr[-2] ) )[ptr[1]] = 0;
                r->stack_ptr[-2] = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr += 2;
                break;

            case MN_STRACAT:
                n = r->stack_ptr[-1];
                strncat( *( char ** )( &r->stack_ptr[-2] ), string_get( n ), (ptr[1]-1) - strlen( *( char ** )( &r->stack_ptr[-2] ) ) );
                (( char * )( intptr_t )( r->stack_ptr[-2] ) )[ptr[1]-1] = 0;
                r->stack_ptr[-2] = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr += 2;
                break;

            case MN_STR2CHARNUL:
                strcpy( *( char ** )( intptr_t )( r->stack_ptr[-2] ), string_get( r->stack_ptr[-1] ) );
                string_discard( r->stack_ptr[-1] );
                r->stack_ptr[-2] = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr += 2;
                break;

            /* Direct operations with variables QWORD type */

            case MN_LETNP | MN_QWORD:
            case MN_LETNP | MN_QWORD | MN_UNSIGNED:
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_QWORD:
            case MN_LET | MN_QWORD | MN_UNSIGNED:
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_QWORD:
            case MN_INC | MN_QWORD | MN_UNSIGNED:
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_QWORD:
            case MN_DEC | MN_QWORD | MN_UNSIGNED:
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_QWORD:
            case MN_POSTDEC | MN_QWORD | MN_UNSIGNED:
                ( *( int64_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( int64_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_QWORD:
            case MN_POSTINC | MN_QWORD | MN_UNSIGNED:
                *(( int64_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( int64_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_QWORD:
            case MN_VARADD | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_QWORD:
            case MN_VARSUB | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_QWORD:
            case MN_VARMUL | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_QWORD:
            case MN_VARDIV | MN_QWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) /= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_QWORD:
            case MN_VARMOD | MN_QWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) %= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_QWORD:
            case MN_VAROR | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_QWORD:
            case MN_VARXOR | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_QWORD:
            case MN_VARAND | MN_QWORD | MN_UNSIGNED:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_QWORD:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_QWORD | MN_UNSIGNED:
                *( uint64_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_QWORD:
                *( int64_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_QWORD | MN_UNSIGNED:
                *( uint64_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* Direct operations with variables DWORD type */

            case MN_LETNP | MN_DWORD:
                ( *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LETNP | MN_DWORD | MN_UNSIGNED:
                ( *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_DWORD:
                ( *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LET | MN_DWORD | MN_UNSIGNED:
                ( *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_DWORD:
                ( *( int32_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_INC | MN_DWORD | MN_UNSIGNED:
                ( *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_DWORD:
                ( *( int32_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_DWORD | MN_UNSIGNED:
                ( *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_DWORD:
                ( *( int32_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( int32_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_DWORD | MN_UNSIGNED:
                ( *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_DWORD:
                *(( int32_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( int32_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_DWORD | MN_UNSIGNED:
                *(( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARADD | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_DWORD:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) /= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_DWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) /= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_DWORD:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) %= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_DWORD | MN_UNSIGNED:
                if ( r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) %= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_DWORD:
                *( int32_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_DWORD | MN_UNSIGNED:
                *( uint32_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* Direct operations with variables WORD type */

            case MN_LETNP | MN_WORD:
                ( *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LETNP | MN_WORD | MN_UNSIGNED:
                ( *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_WORD:
                ( *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LET | MN_WORD | MN_UNSIGNED:
                ( *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_WORD:
                ( *( int16_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_INC | MN_WORD | MN_UNSIGNED:
                ( *( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_WORD:
                ( *( int16_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_WORD | MN_UNSIGNED:
                ( *( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_WORD:
                ( *( int16_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( int16_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_WORD | MN_UNSIGNED:
                ( *( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_WORD:
                *(( int16_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( int16_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_WORD | MN_UNSIGNED:
                *(( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( uint16_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARADD | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_WORD:
                if (( int16_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) /= ( int16_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_WORD | MN_UNSIGNED:
                if (( uint16_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) /= ( uint16_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_WORD:
                if (( int16_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) %= ( int16_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_WORD | MN_UNSIGNED:
                if (( uint16_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) %= ( uint16_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_WORD:
                *( int16_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_WORD | MN_UNSIGNED:
                *( uint16_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* Direct operations with variables BYTE type */

            case MN_LETNP | MN_BYTE:
                ( *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LETNP | MN_BYTE | MN_UNSIGNED:
                ( *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_BYTE:
                ( *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_LET | MN_BYTE | MN_UNSIGNED:
                ( *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) ) = r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_BYTE:
                ( *( int8_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_INC | MN_BYTE | MN_UNSIGNED:
                ( *( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_BYTE:
                ( *( int8_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_BYTE | MN_UNSIGNED:
                ( *( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_BYTE:
                ( *( int8_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( int8_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_BYTE | MN_UNSIGNED:
                ( *( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_BYTE:
                *(( int8_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( int8_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_BYTE | MN_UNSIGNED:
                *(( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( uint8_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARADD | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) += r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) -= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) *= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_BYTE:
                if (( int8_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) /= ( int8_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_BYTE | MN_UNSIGNED:
                if (( uint8_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) /= ( uint8_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_BYTE:
                if (( int8_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) %= ( int8_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_BYTE | MN_UNSIGNED:
                if (( uint8_t )r->stack_ptr[-1] == 0 ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Division by zero\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) %= ( uint8_t )r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VAROR | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) |= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARXOR | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) ^= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARAND | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) &= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROR | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) >>= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_BYTE:
                *( int8_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARROL | MN_BYTE | MN_UNSIGNED:
                *( uint8_t * )( intptr_t )( r->stack_ptr[-2] ) <<= r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            /* Direct operations with variables DOUBLE type */

            case MN_LETNP | MN_DOUBLE:
                ( *( double * )( intptr_t )( r->stack_ptr[-2] ) ) = *( double * ) &r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_DOUBLE:
                ( *( double * )( intptr_t )( r->stack_ptr[-2] ) ) = *( double * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_DOUBLE:
                ( *( double * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_DOUBLE:
                ( *( double * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_DOUBLE:
                ( *( double * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( uint64_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_DOUBLE:
                *(( double * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( uint64_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_DOUBLE:
                *( double * )( intptr_t )( r->stack_ptr[-2] ) += *( double * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_DOUBLE:
                *( double * )( intptr_t )( r->stack_ptr[-2] ) -= *( double * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_DOUBLE:
                *( double * )( intptr_t )( r->stack_ptr[-2] ) *= *( double * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_DOUBLE:
                *( double * )( intptr_t )( r->stack_ptr[-2] ) /= *( double * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_DOUBLE:
                *( double * )( intptr_t )( r->stack_ptr[-2] ) = fmod( *( double * )( intptr_t )( r->stack_ptr[-2] ), *( double * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* Direct operations with variables FLOAT type */

            case MN_LETNP | MN_FLOAT:
                ( *( float * )( intptr_t )( r->stack_ptr[-2] ) ) = *( float * ) &r->stack_ptr[-1];
                r->stack_ptr -= 2;
                ptr++;
                break;

            case MN_LET | MN_FLOAT:
                ( *( float * )( intptr_t )( r->stack_ptr[-2] ) ) = *( float * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_INC | MN_FLOAT:
                ( *( float * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                ptr += 2;
                break;

            case MN_DEC | MN_FLOAT:
                ( *( float * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                ptr += 2;
                break;

            case MN_POSTDEC | MN_FLOAT:
                ( *( float * )( intptr_t )( r->stack_ptr[-1] ) ) -= ptr[1];
                r->stack_ptr[-1] = *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) + ptr[1];
                ptr += 2;
                break;

            case MN_POSTINC | MN_FLOAT:
                *(( float * )( intptr_t )( r->stack_ptr[-1] ) ) += ptr[1];
                r->stack_ptr[-1] = *( uint32_t * )( intptr_t )( r->stack_ptr[-1] ) - ptr[1];
                ptr += 2;
                break;

            case MN_VARADD | MN_FLOAT:
                *( float * )( intptr_t )( r->stack_ptr[-2] ) += *( float * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARSUB | MN_FLOAT:
                *( float * )( intptr_t )( r->stack_ptr[-2] ) -= *( float * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMUL | MN_FLOAT:
                *( float * )( intptr_t )( r->stack_ptr[-2] ) *= *( float * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARDIV | MN_FLOAT:
                *( float * )( intptr_t )( r->stack_ptr[-2] ) /= *( float * ) &r->stack_ptr[-1];
                r->stack_ptr--;
                ptr++;
                break;

            case MN_VARMOD | MN_FLOAT:
                *( float * )( intptr_t )( r->stack_ptr[-2] ) = fmod( *( float * )( intptr_t )( r->stack_ptr[-2] ), *( float * ) &r->stack_ptr[-1] );
                r->stack_ptr--;
                ptr++;
                break;

            /* Jumps */

            case MN_JUMP:
                ptr = r->code + ptr[1];
                continue;

            case MN_JTRUE:
                r->stack_ptr--;
                if ( *r->stack_ptr ) {
                    ptr = r->code + ptr[1];
                    continue;
                }
                ptr += 2;
                break;

            case MN_JFALSE:
                r->stack_ptr--;
                if ( !*r->stack_ptr ) {
                    ptr = r->code + ptr[1];
                    continue;
                }
                ptr += 2;
                break;

            case MN_JTTRUE:
                if ( r->stack_ptr[-1] ) {
                    ptr = r->code + ptr[1];
                    continue;
                }
                ptr += 2;
                break;

            case MN_JTFALSE:
                if ( !r->stack_ptr[-1] ) {
                    ptr = r->code + ptr[1];
                    continue;
                }
                ptr += 2;
                break;

            case MN_NCALL:
                *r->stack_ptr++ = ptr - r->code + 2 ; /* Push next address */
                ptr = r->code + ptr[1] ; /* Call function */
                r->call_level++;
                break;

            /* Switch */

            case MN_SWITCH:
                r->switchval = *--r->stack_ptr;
                r->cased = 0;
                ptr++;
                break;

            case MN_SWITCH | MN_STRING:
                if ( r->switchval_string != 0 ) string_discard( r->switchval_string );
                r->switchval_string = *--r->stack_ptr;
                r->cased = 0;
                ptr++;
                break;

            case MN_CASE:
                if ( r->switchval == *--r->stack_ptr ) r->cased = 2;
                ptr++;
                break;

            case MN_CASE | MN_STRING:
                if ( string_comp( r->switchval_string, *--r->stack_ptr ) == 0 ) r->cased = 2;
                string_discard( *r->stack_ptr );
                string_discard( r->stack_ptr[-1] );
                ptr++;
                break;

            case MN_CASE_R:
                r->stack_ptr -= 2;
                if ( r->switchval >= r->stack_ptr[0] && r->switchval <= r->stack_ptr[1] ) r->cased = 1;
                ptr++;
                break;

            case MN_CASE_R | MN_STRING:
                r->stack_ptr -= 2;
                if ( string_comp( r->switchval_string, r->stack_ptr[0] ) >= 0 &&
                     string_comp( r->switchval_string, r->stack_ptr[1] ) <= 0 )
                    r->cased = 1;
                string_discard( r->stack_ptr[0] );
                string_discard( r->stack_ptr[1] );
                ptr++;
                break;

            case MN_JNOCASE:
                if ( r->cased < 1 ) {
                    ptr = r->code + ptr[1];
                    continue;
                }
                ptr += 2;
                break;

            /* Process control */

            case MN_TYPE:
            {
                PROCDEF * proct = procdef_get( ptr[1] );
                if ( !proct ) {
                    fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Invalid type\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    exit( 0 );
                }
                *r->stack_ptr++ = proct->type;
                ptr += 2;
                break;
            }

            case MN_FRAME:
                LOCINT64( r, FRAME_PERCENT ) += r->stack_ptr[-1];
                r->stack_ptr--;
                r->codeptr = ptr + 1;
                return_value = LOCQWORD( r, PROCESS_ID );

                if ( !( r->proc->flags & PROC_FUNCTION ) &&
                        r->called_by && instance_exists( r->called_by ) && ( LOCQWORD( r->called_by, STATUS ) & STATUS_WAITING_MASK ) )
                {
                    /* We're returning and the parent is waiting: wake it up */
                    if ( r->called_by->stack && ( r->called_by->stack[0] & STACK_RETURN_VALUE ) )
                        r->called_by->stack_ptr[-1] = return_value;

                    LOCQWORD( r->called_by, STATUS ) &= ~STATUS_WAITING_MASK;
                    r->called_by = NULL;
                }
                goto break_all;

            case MN_END:
                if ( r->call_level > 0 ) {
                    ptr = r->code + *--r->stack_ptr;
                    r->call_level--;
                    continue;
                }

                if ( LOCQWORD( r, STATUS ) != STATUS_DEAD ) LOCQWORD( r, STATUS ) = STATUS_KILLED;
                goto break_all;

            case MN_RETURN:
                if ( r->call_level > 0 ) {
                    ptr = r->code + *--r->stack_ptr;
                    r->call_level--;
                    continue;
                }

                if ( LOCQWORD( r, STATUS ) != STATUS_DEAD ) LOCQWORD( r, STATUS ) = STATUS_KILLED;
                r->stack_ptr--;
                return_value = *r->stack_ptr;
                goto break_all;

            /* Handlers */

            case MN_EXITHNDLR:
                r->exitcode = ptr[1];
                ptr += 2;
                break;

            case MN_ERRHNDLR:
                r->errorcode = ptr[1];
                ptr += 2;
                break;

            /* Others */

            case MN_DEBUG:
                if ( dcb.data.NSourceFiles ) {
                    if ( debug > 0 ) printf( "\n::: DEBUG from %s(%" PRId64 ")\n", r->proc->name, LOCQWORD( r, PROCESS_ID ) );
                    trace_sentence = -1;
                    debugger_show_console = 1;
                }
                ptr++;
                break;

            case MN_SENTENCE:
                trace_sentence     = ptr[1];
                trace_instance     = r;
                ptr += 2;
                if ( debugger_trace || debugger_step ) {
                    debugger_trace = 0;
                    debugger_step = 0;
                    debugger_show_console = 1;
                }
                break;


            case MN_COPY_ARRAY | MN_QWORD:
            case MN_COPY_ARRAY | MN_QWORD | MN_UNSIGNED:
            case MN_COPY_ARRAY | MN_DOUBLE:
                memmove( ( int8_t * )( ( intptr_t )r->stack_ptr[-3] ), ( int8_t * )( ( intptr_t )r->stack_ptr[-2] ), r->stack_ptr[-1] * sizeof( int64_t ));
                r->stack_ptr -= 3;
                ptr++;
                break;

            case MN_COPY_ARRAY | MN_DWORD:
            case MN_COPY_ARRAY | MN_DWORD | MN_UNSIGNED:
            case MN_COPY_ARRAY | MN_FLOAT:
                memmove( ( int8_t * )( ( intptr_t )r->stack_ptr[-3] ), ( int8_t * )( ( intptr_t )r->stack_ptr[-2] ), r->stack_ptr[-1] * sizeof( int32_t ) );
                r->stack_ptr -= 3;
                ptr++;
                break;

            case MN_COPY_ARRAY | MN_WORD:
            case MN_COPY_ARRAY | MN_WORD | MN_UNSIGNED:
                memmove( ( int8_t * )( ( intptr_t )r->stack_ptr[-3] ), ( int8_t * )( ( intptr_t )r->stack_ptr[-2] ), r->stack_ptr[-1] * sizeof( int16_t ) );
                r->stack_ptr -= 3;
                ptr++;
                break;

            case MN_COPY_ARRAY | MN_BYTE:
            case MN_COPY_ARRAY | MN_BYTE | MN_UNSIGNED:
                memmove( ( int8_t * )( ( intptr_t )r->stack_ptr[-3] ), ( int8_t * )( ( intptr_t )r->stack_ptr[-2] ), r->stack_ptr[-1] );
                r->stack_ptr -= 3;
                ptr++;
                break;

            case MN_COPY_ARRAY | MN_STRING:
            {
                // Size in elements
                int64_t * dst = ( int64_t * )( ( intptr_t )r->stack_ptr[-3] );
                int64_t * src = ( int64_t * )( ( intptr_t )r->stack_ptr[-2] );
                sz = r->stack_ptr[-1];
                while( sz-- ) {
                    string_discard( *dst );
                    *dst = *src;
                    string_use( *dst );
                    dst++; src++;
                }
                r->stack_ptr -= 3;
                ptr++;
                break;
            }


            case MN_COPY_ARRAY_REPEAT | MN_QWORD:
            case MN_COPY_ARRAY_REPEAT | MN_QWORD | MN_UNSIGNED:
            case MN_COPY_ARRAY_REPEAT | MN_DOUBLE:
            {
                int8_t * dst = ( int8_t * )( ( intptr_t )r->stack_ptr[-3] );
                int8_t * src = ( int8_t * )( ( intptr_t )r->stack_ptr[-2] );
                sz = r->stack_ptr[-1] * sizeof( int64_t );
                n = ptr[1];
                while( n-- ) {
                    memmove( dst, src, sz );
                    dst += sz;
                }
                r->stack_ptr -= 3;
                ptr += 2;
                break;
            }

            case MN_COPY_ARRAY_REPEAT | MN_DWORD:
            case MN_COPY_ARRAY_REPEAT | MN_DWORD | MN_UNSIGNED:
            case MN_COPY_ARRAY_REPEAT | MN_FLOAT:
            {
                int8_t * dst = ( int8_t * )( ( intptr_t )r->stack_ptr[-3] );
                int8_t * src = ( int8_t * )( ( intptr_t )r->stack_ptr[-2] );
                sz = r->stack_ptr[-1] * sizeof( int32_t );
                n = ptr[1];
                while( n-- ) {
                    memmove( dst, src, sz );
                    dst += sz;
                }
                r->stack_ptr -= 3;
                ptr += 2;
                break;
            }

            case MN_COPY_ARRAY_REPEAT | MN_WORD:
            case MN_COPY_ARRAY_REPEAT | MN_WORD | MN_UNSIGNED:
            {
                int8_t * dst = ( int8_t * )( ( intptr_t )r->stack_ptr[-3] );
                int8_t * src = ( int8_t * )( ( intptr_t )r->stack_ptr[-2] );
                sz = r->stack_ptr[-1] * sizeof( int16_t );
                n = ptr[1];
                while( n-- ) {
                    memmove( dst, src, sz );
                    dst += sz;
                }
                r->stack_ptr -= 3;
                ptr += 2;
                break;
            }

            case MN_COPY_ARRAY_REPEAT | MN_BYTE:
            case MN_COPY_ARRAY_REPEAT | MN_BYTE | MN_UNSIGNED:
            {
                int8_t * dst = ( int8_t * )( ( intptr_t )r->stack_ptr[-3] );
                int8_t * src = ( int8_t * )( ( intptr_t )r->stack_ptr[-2] );
                sz = r->stack_ptr[-1];
                n = ptr[1];
                while( n-- ) {
                    memmove( dst, src, sz );
                    dst += sz;
                }
                r->stack_ptr -= 3;
                ptr += 2;
                break;
            }

            case MN_COPY_ARRAY_REPEAT | MN_STRING:
            {
                // Size in elements
                int64_t * dst = ( int64_t * )( ( intptr_t )r->stack_ptr[-3] );
                int64_t * src = ( int64_t * )( ( intptr_t )r->stack_ptr[-2] );
                n = ptr[1];
                while( n-- ) {
                    sz = r->stack_ptr[-1];
                    while( sz-- ) {
                        string_discard( *dst );
                        *dst = *src;
                        string_use( *dst );
                        dst++; src++;
                    }
                }
                r->stack_ptr -= 3;
                ptr += 2;
                break;
            }

            case MN_COPY_STRUCT:
                copytypes(( void * )( intptr_t )r->stack_ptr[-5], ( void * )( intptr_t )r->stack_ptr[-4], ( DCB_TYPEDEF * )( intptr_t )r->stack_ptr[-3], r->stack_ptr[-2], r->stack_ptr[-1] );
                r->stack_ptr -= 5;
                ptr++;
                break;

            default:
                fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Mnemonic 0x%02"PRIX64" not implemented\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), *ptr );
                exit( 0 );
        }

        if ( r->stack_ptr < r->stack ) {
            fprintf( stderr, "ERROR: Runtime error in %s(%" PRId64 ") - Critical Stack Problem StackBase=%p StackPTR=%p\n", r->proc->name, LOCQWORD( r, PROCESS_ID ), (void *)r->stack, (void *)r->stack_ptr );
            exit( 0 );
        }

#ifdef EXIT_ON_EMPTY_STACK
        if ( r->stack_ptr == r->stack ) {
            r->codeptr = ptr;
            if ( LOCQWORD( r, STATUS ) != STATUS_RUNNING && LOCQWORD( r, STATUS ) != STATUS_DEAD ) break;
        }
#endif

    }

    /* *** GENERAL EXIT *** */
break_all:

    if ( !*ptr || *ptr == MN_RETURN || *ptr == MN_END || LOCQWORD( r, STATUS ) == STATUS_KILLED ) {
        /* Check for waiting parent */
        if ( r->called_by && instance_exists( r->called_by ) && ( LOCQWORD( r->called_by, STATUS ) & STATUS_WAITING_MASK ) ) {
            /* We're returning and the parent is waiting: wake it up */
            if ( r->called_by->stack && ( r->called_by->stack[0] & STACK_RETURN_VALUE ) )
                r->called_by->stack_ptr[-1] = return_value;

            LOCQWORD( r->called_by, STATUS ) &= ~STATUS_WAITING_MASK;
        }
        r->called_by = NULL;

        /* The process should be destroyed immediately, it is a function-type one */
        /* Run ONEXIT */
        if ( ( ( LOCQWORD( r, STATUS ) & ~( STATUS_WAITING_MASK | STATUS_PAUSED_MASK ) ) != STATUS_DEAD ) && r->exitcode ) {
            LOCQWORD( r, STATUS ) = ( STATUS_DEAD | ( LOCQWORD( r, STATUS ) & STATUS_PAUSED_MASK ) );
            r->codeptr = r->code + r->exitcode;
            ptr = r->codeptr;
            goto main_loop_instance_go;
//            instance_go( r );
//            if ( !instance_exists( r ) ) r = NULL;
        } else {
            instance_destroy( r );
            r = NULL;
        }
    }

    /* Hook */
    if ( r && instance_pos_execute_hook_count ) {
        for ( n = 0; n < instance_pos_execute_hook_count; n++ )
            instance_pos_execute_hook_list[n]( r );
    }
    /* Hook */
    if ( r && LOCQWORD( r, STATUS ) != STATUS_KILLED && r->first_run ) r->first_run = 0;

    return return_value;
}

/* ---------------------------------------------------------------------- */

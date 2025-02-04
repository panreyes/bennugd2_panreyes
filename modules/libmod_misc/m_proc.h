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

#ifndef __M_PROC_H
#define __M_PROC_H

/* ----------------------------------------------------------------- */

#define ALL_PROCESS         0

#define S_KILL              0
#define S_WAKEUP            1
#define S_SLEEP             2
#define S_FREEZE            3

#define S_FORCE             50
#define S_TREE              100

#define S_KILL_TREE         (S_TREE + S_KILL  )
#define S_WAKEUP_TREE       (S_TREE + S_WAKEUP)
#define S_SLEEP_TREE        (S_TREE + S_SLEEP )
#define S_FREEZE_TREE       (S_TREE + S_FREEZE)

#define S_KILL_FORCE        (S_FORCE + S_KILL  )
#define S_WAKEUP_FORCE      (S_FORCE + S_WAKEUP)
#define S_SLEEP_FORCE       (S_FORCE + S_SLEEP )
#define S_FREEZE_FORCE      (S_FORCE + S_FREEZE)
#define S_KILL_TREE_FORCE   (S_FORCE + S_KILL_TREE  )
#define S_WAKEUP_TREE_FORCE (S_FORCE + S_WAKEUP_TREE)
#define S_SLEEP_TREE_FORCE  (S_FORCE + S_SLEEP_TREE )
#define S_FREEZE_TREE_FORCE (S_FORCE + S_FREEZE_TREE)

#define S_DFL               0
#define S_IGN               1

#define SMASK_KILL          0x0001
#define SMASK_WAKEUP        0x0002
#define SMASK_SLEEP         0x0004
#define SMASK_FREEZE        0x0008
#define SMASK_KILL_TREE     0x0100
#define SMASK_WAKEUP_TREE   0x0200
#define SMASK_SLEEP_TREE    0x0400
#define SMASK_FREEZE_TREE   0x0800

/* ----------------------------------------------------------------- */

extern int64_t libmod_misc_proc_exit_0( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_exit_1( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_exit( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_running( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_signal( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_signal_action( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_signal_action3( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_let_me_alone( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_get_id( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_get_status( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_pause0( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_pause1( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_resume0( INSTANCE * my, int64_t * params );
extern int64_t libmod_misc_proc_resume1( INSTANCE * my, int64_t * params );

/* ----------------------------------------------------------------- */

#endif

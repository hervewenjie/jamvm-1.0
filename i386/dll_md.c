/*
 * Copyright (C) 2003 Robert Lougher <rob@lougher.demon.co.uk>.
 *
 * This file is part of JamVM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include "../jam.h"
#include "../sig.h"

int extraArgSpace(MethodBlock *mb) {
    return (mb->access_flags & ACC_STATIC ? mb->args_count+1 : mb->args_count) + 1;
}

u8 *callJNIMethod(void *env, Class *class, char *sig, int extra, u8 *ostack, unsigned char *f) {
    u4 args[extra];
    u8 *opntr = ostack;
    u8 *apntr = &args[2];
   
    args[0] = (u8)env;
    args[1] = class ? (u8)class : *opntr++;

    u8 apntr_tmp = *((u8*)apntr);
    u8 opntr_tmp = *((u8*)opntr);
    SCAN_SIG(sig, apntr_tmp = opntr_tmp, *apntr++ = *opntr++);
    apntr_tmp++;
    opntr_tmp++;

    switch(*sig) {
        case 'V':
            (*(void (*)())f)();
            break;

//        case 'D':
//            *((double*)ostack)++ = (*(double (*)())f)();
//            break;
//
//        case 'F':
//            *((float*)ostack)++ = (*(float (*)())f)();
//            break;
//
//        case 'J':
//            *((long long*)ostack)++ = (*(long long (*)())f)();
//            break;

        default:
            *ostack++ = (*(u8 (*)())f)();
            break;
    }

    return ostack;
}

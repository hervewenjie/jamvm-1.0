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
#include <stdlib.h>
#include "jam.h"
#include "sig.h"
#include "frame.h"
#include "lock.h"

#define VA_DOUBLE(args, sp)  *(u8*)sp = va_arg(args, u8); sp+=2
#define VA_SINGLE(args, sp)  *sp++ = va_arg(args, u4)

#define JA_DOUBLE(args, sp)  (u8*)sp++; *((u8*)sp) = *args++
#define JA_SINGLE(args, sp)  *sp++ = *(u4*)args; args++

// execute non-static & static method
void *executeMethodArgs(Object *ob, Class *class, MethodBlock *mb, ...) {
    va_list jargs;
    void *ret;

    va_start(jargs, mb);
    ret = executeMethodVaList(ob, class, mb, jargs);
    va_end(jargs);

    return ret;
}

//void create_top_frame_tmp(ExecEnv *ee, Class *class, MethodBlock *mb, u4 *sp, void *ret) {
//    Frame *last = ee->last_frame;
//    Frame *dummy = (Frame *)(last->ostack+last->mb->max_stack);
//    Frame *new_frame;
//
//    uintptr_t *new_ostack;
//
//    ret = (void*) (sp = (uintptr_t*)(dummy+1));
//    new_frame = (Frame *)(sp + mb->max_locals);
//    new_ostack = ALIGN_OSTACK(new_frame + 1);
//
//    if((char*)(new_ostack + mb->max_stack) > ee->stack_end) {
//        printf("Fatal stack overflow!  Aborting VM.\n");
//
//    }
//
//    dummy->mb = NULL;
//    dummy->ostack = sp;
//    dummy->prev = last;
//
//    new_frame->mb = mb;
//    new_frame->lvars = sp;
//    new_frame->ostack = new_ostack;
//
//    new_frame->prev = dummy;
//    ee->last_frame = new_frame;
//}

void *executeMethodVaList(Object *ob, Class *class, MethodBlock *mb, va_list jargs) {
    ClassBlock *cb = CLASS_CB(class);
    char *sig = mb->type;

    ExecEnv *ee = getExecEnv();
    void *ret;
    u4 *sp;

    // create frame
    CREATE_TOP_FRAME(ee, class, mb, sp, ret);

    /* copy args onto stack */

    // index 0 of local variable table is always "this"
    if(ob)
        *sp++ = (u4) ob; /* push receiver first */

    SCAN_SIG(sig, VA_DOUBLE(jargs, sp), VA_SINGLE(jargs, sp))

    // synchronized lock
    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectLock(ob ? ob : (Object*)mb->class);

    // native
    if(mb->access_flags & ACC_NATIVE)
        (*(u4 *(*)(Class*, MethodBlock*, u4*))mb->native_invoker)(class, mb, ret);
    // java
    else
        executeJava();

    // synchronized unlock
    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectUnlock(ob ? ob : (Object*)mb->class);

    // pop frame
    POP_TOP_FRAME(ee);
    return ret;
}

void *executeMethodList(Object *ob, Class *class, MethodBlock *mb, u8 *jargs) {
    ClassBlock *cb = CLASS_CB(class);
    char *sig = mb->type;

    ExecEnv *ee = getExecEnv();
    void *ret;
    u4 *sp;

    CREATE_TOP_FRAME(ee, class, mb, sp, ret);

    /* copy args onto stack */

    if(ob)
        *sp++ = (u4) ob; /* push receiver first */

    SCAN_SIG(sig, JA_DOUBLE(jargs, sp), JA_SINGLE(jargs, sp))

    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectLock(ob ? ob : (Object*)mb->class);

    if(mb->access_flags & ACC_NATIVE)
        (*(u4 *(*)(Class*, MethodBlock*, u4*))mb->native_invoker)(class, mb, ret);
    else
        executeJava();

    if(mb->access_flags & ACC_SYNCHRONIZED)
        objectUnlock(ob ? ob : (Object*)mb->class);

    POP_TOP_FRAME(ee);
    return ret;
}

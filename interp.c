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
#include <arpa/inet.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>


#include "jam.h"
#include "thread.h"
#include "lock.h"

#define CP_SINDEX(p)  p[1]
#define CP_DINDEX(p)  (p[1]<<8)|p[2]
#define BRANCH(p)     (((signed char)p[1])<<8)|p[2]
#define BRANCH_W(p)   (((signed char)p[1])<<24)|(p[2]<<16)|(p[3]<<8)|p[4]
#define DSIGNED(p)    (((signed char)p[1])<<8)|p[2]

#define THROW_EXCEPTION(excep_name, message)                          \
{                                                                     \
    frame->last_pc = (unsigned char*)pc;                              \
    signalException(excep_name, message);                             \
    goto throwException;                                              \
}

#define ZERO_DIVISOR_CHECK(TYPE, ostack)                              \
    if(((TYPE*)ostack)[-1] == 0)                                      \
        THROW_EXCEPTION("java/lang/ArithmeticException",              \
                        "division by zero");

#define NULL_POINTER_CHECK(ref)                                       \
    if(!ref)                                                          \
        THROW_EXCEPTION("java/lang/NullPointerException", NULL);

#define ARRAY_BOUNDS_CHECK(array, i)                                  \
    if(i >= *INST_DATA(array))                                        \
        THROW_OUT_OF_BOUNDS_EXCEPTION(i);

#define THROW_OUT_OF_BOUNDS_EXCEPTION(index)                          \
{                                                                     \
    char buff[256];                                                   \
    sprintf(buff, "%d", index);                                       \
    THROW_EXCEPTION("java/lang/ArrayIndexOutOfBoundsException",       \
                                                           buff);     \
}

#define UNARY_MINUS(TYPE, ostack, pc)                                 \
    ((TYPE*)ostack)[-1] = -((TYPE*)ostack)[-1];                       \
    pc += 1;                                                          \
    DISPATCH(pc)

#define BINARY_OP(TYPE, OP, ostack, pc)                               \
    ((TYPE*)ostack)[-2] = ((TYPE*)ostack)[-2] OP ((TYPE*)ostack)[-1]; \
    ostack -= sizeof(TYPE)/4;                                         \
    pc += 1;                                                          \
    DISPATCH(pc)

#define SHIFT_OP(TYPE, OP, ostack, pc)                                \
{                                                                     \
    int s = *--ostack;                                                \
    ((TYPE*)ostack)[-1] = ((TYPE*)ostack)[-1] OP s;                   \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define OPC_X2Y(SRC_TYPE, DEST_TYPE)                                  \
{                                                                     \
    ostack = (SRC_TYPE *)ostack - 1;                                  \
    SRC_TYPE v = *(SRC_TYPE *)ostack;                                 \
    *((DEST_TYPE *)ostack) = (DEST_TYPE)v;                          \
    (DEST_TYPE *)ostack++;                                   \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define ARRAY_DATA(arrayRef, type)   ((type*)(((uintptr_t*)(arrayRef+1))+1))

#define ARRAY_LOAD(TYPE, ostack, pc)                                  \
{                                                                     \
    TYPE *element;                                                    \
    int i = ostack[-1];						                          \
    printf("i=%d\n", i); \
    Object *array = (Object *)ostack[-2];			                  \
    printf("array=%p\n", array); \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    element = (TYPE *)(((char *)INST_DATA(array)) + (i * sizeof(TYPE)) + 8); \
    printf("element=%p\n", element);              \
    ostack[-2] = *element;					      \
    printf("*element=%p\n", *element);              \
    ostack -= 1;					              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_LOAD_LONG(TYPE, ostack, pc)                             \
{                                                                     \
    u8 *element;                                                      \
    int i = ostack[-1];						      \
    Object *array = (Object *)ostack[-2];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    element = (u8 *)(((char *)INST_DATA(array)) + (i << 3) + 4);      \
    ((u8*)ostack)[-1] = *element;                                     \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_STORE(TYPE, ostack, pc)                                 \
{                                                                     \
    int v = ostack[-1];						      \
    int i = ostack[-2];						      \
    Object *array = (Object *)ostack[-3];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    *(TYPE *)(((char *)INST_DATA(array))+(i * sizeof(TYPE)) + 4) = v; \
    ostack -= 3;		 			              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define ARRAY_STORE_LONG(TYPE, ostack, pc)                            \
{                                                                     \
    u8 v = ((u8*)ostack)[-1];					      \
    int i = ostack[-3];						      \
    Object *array = (Object *)ostack[-4];			      \
    NULL_POINTER_CHECK(array);                                        \
    ARRAY_BOUNDS_CHECK(array, i);                                     \
    *(u8 *)(((char *)INST_DATA(array)) + (i << 3) + 4) = v;           \
    ostack -= 4;					              \
    pc += 1;						              \
    DISPATCH(pc)						      \
}

#define IF_ICMP(COND, ostack, pc)	                              \
{					                              \
    int v1 = ostack[-2];		                              \
    int v2 = ostack[-1];		                              \
    if(v1 COND v2) {			                              \
        pc += BRANCH(pc);		                              \
    } else 				                              \
        pc += 3;			                              \
    ostack -= 2;			                              \
    DISPATCH(pc)				                      \
}

#define IF(COND, ostack, pc)		                              \
{					                              \
    int v = *--ostack;			                              \
    if(v COND 0) {			                              \
        pc += BRANCH(pc);		                              \
    } else 				                              \
        pc += 3;			                              \
    DISPATCH(pc) 		                                      \
}

#define CMP(TYPE, ostack, pc)                                         \
{                                                                     \
ostack = (TYPE *)ostack - 1; \
    TYPE v2 = *((TYPE *)ostack);                                    \
    ostack = (TYPE *)ostack - 1; \
    TYPE v1 = *((TYPE *)ostack);                                    \
    if(v1 == v2)                                                      \
        *ostack++ = 0;                                                \
    else if(v1 < v2)                                                  \
            *ostack++ = -1;                                           \
        else                                                          \
            *ostack++ = 1;                                            \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define FCMP(TYPE, ostack, pc, isNan)                                 \
{                                                                     \
    ostack = (TYPE *)ostack - 1;   \
    TYPE v2 = *((TYPE *)ostack);                                    \
    ostack = (TYPE *)ostack - 1;   \
    TYPE v1 = *((TYPE *)ostack);                                    \
    if(v1 == v2)                                                      \
        *ostack++ = 0;                                                \
    else if(v1 < v2)                                                  \
        *ostack++ = -1;                                               \
    else if(v1 > v2)                                                  \
         *ostack++ = 1;                                               \
    else                                                              \
         *ostack++ = isNan;                                           \
    pc += 1;                                                          \
    DISPATCH(pc)                                                      \
}

#define WITH_OPCODE_CHANGE_CP_DINDEX(pc, opcode, index)               \
    index = CP_DINDEX(pc);                                            \
    if(pc[0] != opcode)                                               \
        DISPATCH(pc)

#define OPCODE_REWRITE(pc, opcode)                                    \
    pc[0] = opcode 

#define OPCODE_REWRITE_OPERAND1(pc, opcode, operand1)                 \
{                                                                     \
    pc[0] = OPC_LOCK;                                                 \
    pc[1] = operand1;                                                 \
    pc[0] = opcode;                                                   \
}

#define OPCODE_REWRITE_OPERAND2(pc, opcode, operand1, operand2)       \
{                                                                     \
    pc[0] = OPC_LOCK;                                                 \
    pc[1] = operand1;                                                 \
    pc[2] = operand2;                                                 \
    pc[0] = opcode;                                                   \
}

#ifdef THREADED
/* Two levels of macros are needed to correctly produce the label
 * from the OPC_xxx macro passed into DEF_OPC as cpp doesn't 
 * prescan when concatenating with ##...
 *
 * On gcc <= 2.95, we also get a space inserted before the :
 * e.g DEF_OPC(OPC_NULL) -> opc0 : - the ##: is a hack to fix
 * this, but this generates warnings on >= 2.96...
 */
#if (__GNUC__ == 2) && (__GNUC_MINOR__ <= 95)
#define label(x)         \
opc##x##:
#else
#define label(x)         \
opc##x:
#endif

#define DEF_OPC(opcode)  \
label(opcode)

#define DISPATCH(pc)     \
    goto *handlers[*pc];
#else
#define DEF_OPC(opcode)  \
    case opcode:

#define DISPATCH(pc)     \
    break;
#endif

u8 *executeJava() {
    ExecEnv *ee = getExecEnv();              // execute env
    Frame *frame = ee->last_frame;           // frame
    MethodBlock *mb = frame->mb;             // method
    u8 *lvars = frame->lvars;                // local vars
    u8 *ostack = frame->ostack;              // stack
    volatile unsigned char *pc = mb->code;   // code -> pc
    ClassBlock* class_tmp = CLASS_CB(mb->class);
    ConstantPool *cp = &(CLASS_CB(mb->class)->constant_pool);

    if (strcmp("java/lang/Runtime", class_tmp->name)==0 && strcmp("<clinit>", mb->name)==0) {
        int herve = 1;
    }

    Object *this = (Object*)lvars[0];        // method local index 0 is pointer to this object
    Class *new_class;                        //
    MethodBlock *new_mb;                     //
    u8 *arg1;                                //

#ifdef THREADED
    static void *handlers[] = {
        &&opc0, &&opc1, &&opc2, &&opc3, &&opc4, &&opc5, &&opc6, &&opc7, &&opc8, &&opc9, &&opc10,
        &&opc11, &&opc12, &&opc13, &&opc14, &&opc15, &&opc16, &&opc17, &&opc18, &&opc19, &&opc20,
        &&opc21, &&opc22, &&opc23, &&opc24, &&opc25, &&opc26, &&opc27, &&opc28, &&opc29, &&opc30,
        &&opc31, &&opc32, &&opc33, &&opc34, &&opc35, &&opc36, &&opc37, &&opc38, &&opc39, &&opc40,
        &&opc41, &&opc42, &&opc43, &&opc44, &&opc45, &&opc46, &&opc47, &&opc48, &&opc49, &&opc50,
        &&opc51, &&opc52, &&opc53, &&opc54, &&opc55, &&opc56, &&opc57, &&opc58, &&opc59, &&opc60,
        &&opc61, &&opc62, &&opc63, &&opc64, &&opc65, &&opc66, &&opc67, &&opc68, &&opc69, &&opc70,
        &&opc71, &&opc72, &&opc73, &&opc74, &&opc75, &&opc76, &&opc77, &&opc78, &&opc79, &&opc80,
        &&opc81, &&opc82, &&opc83, &&opc84, &&opc85, &&opc86, &&opc87, &&opc88, &&opc89, &&opc90,
        &&opc91, &&opc92, &&opc93, &&opc94, &&opc95, &&opc96, &&opc97, &&opc98, &&opc99, &&opc100,
        &&opc101, &&opc102, &&opc103, &&opc104, &&opc105, &&opc106, &&opc107, &&opc108, &&opc109,
        &&opc110, &&opc111, &&opc112, &&opc113, &&opc114, &&opc115, &&opc116, &&opc117, &&opc118,
        &&opc119, &&opc120, &&opc121, &&opc122, &&opc123, &&opc124, &&opc125, &&opc126, &&opc127,
        &&opc128, &&opc129, &&opc130, &&opc131, &&opc132, &&opc133, &&opc134, &&opc135, &&opc136,
        &&opc137, &&opc138, &&opc139, &&opc140, &&opc141, &&opc142, &&opc143, &&opc144, &&opc145,
        &&opc146, &&opc147, &&opc148, &&opc149, &&opc150, &&opc151, &&opc152, &&opc153, &&opc154,
        &&opc155, &&opc156, &&opc157, &&opc158, &&opc159, &&opc160, &&opc161, &&opc162, &&opc163,
        &&opc164, &&opc165, &&opc166, &&opc167, &&opc168, &&opc169, &&opc170, &&opc171, &&opc172,
        &&opc173, &&opc174, &&opc175, &&opc176, &&opc177, &&opc178, &&opc179, &&opc180, &&opc181,
        &&opc182, &&opc183, &&opc184, &&opc185, &&unused, &&opc187, &&opc188, &&opc189, &&opc190,
        &&opc191, &&opc192, &&opc193, &&opc194, &&opc195, &&opc196, &&opc197, &&opc198, &&opc199,
        &&opc200, &&opc201, &&unused, &&opc203, &&opc204, &&unused, &&opc206, &&opc207, &&opc208,
        &&opc209, &&opc210, &&opc211, &&opc212, &&opc213, &&opc214, &&opc215, &&opc216, &&unused,
        &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&opc226,
        &&opc227, &&opc228, &&opc229, &&opc230, &&opc231, &&opc232, &&unused, &&unused, &&unused,
	&&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, 
	&&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, &&unused, 
	&&unused, &&unused};

    DISPATCH(pc)

#else
    while(TRUE) {
        switch(*pc) {
            default:
#endif

unused:
    printf("Unrecognised opcode %d in: %s.%s\n", *pc, CLASS_CB(mb->class)->name, mb->name);
    exit(0);

    DEF_OPC(OPC_NOP)          // NOP
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ACONST_NULL)  // ACONST_NULL
        *ostack++ = 0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_M1)    // ICONST_M1
        *ostack++ = -1;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_0)     // ICONST_0
    DEF_OPC(OPC_FCONST_0)     // FCONST_0
        *ostack++ = 0;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_1)     // ICONST_1
        *ostack++ = 1;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_2)     // ICONST_2
        *ostack++ = 2;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_3)     // ICONST_3
        *ostack++ = 3;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_4)     // ICONST_4
        *ostack++ = 4;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ICONST_5)     // ICONST_5
        *ostack++ = 5;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LCONST_0)     // LCONST_0
    DEF_OPC(OPC_DCONST_0)     // DCONST_0
        *((u8*)ostack) = 0;
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_FCONST_1)     // FCONST_1
        *((float*)ostack) = (float) 1.0;
        (float*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_FCONST_2)     // FCONST_2
        *((float*)ostack) = (float) 2.0;
        (float*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DCONST_1)     // DCONST_1
        *((double*)ostack) = (double) 1.0;
        (double*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LCONST_1)     // LCONST_1
        *((u8*)ostack) = 1;
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_SIPUSH)       // SIPUSH
        *ostack++ = (signed short)((pc[1]<<8)+pc[2]);
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_BIPUSH)       // BIPUSH
        *ostack++ = (signed char)pc[1];
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC)          // LDC
        printf("ldc %d\n", CP_SINDEX(pc));
        *ostack++ = resolveSingleConstant(mb->class, CP_SINDEX(pc));
        OPCODE_REWRITE(pc, OPC_LDC_QUICK); // write for next time?
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_QUICK)    // LDC_QUICK
        *ostack++ = CP_INFO(cp, CP_SINDEX(pc));
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_W)        // LDC_W
        *ostack++ = resolveSingleConstant(mb->class, CP_DINDEX(pc));
        OPCODE_REWRITE(pc, OPC_LDC_W_QUICK);
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC_W_QUICK)  // LDC_W_QUICK
        *ostack++ = CP_INFO(cp, CP_DINDEX(pc));
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_LDC2_W)       // LDC2_W
        *((u8*)ostack) = CP_LONG(cp, CP_DINDEX(pc));
        (u8*)ostack++;
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD)        // ILOAD
    DEF_OPC(OPC_FLOAD)        // FLOAD
    DEF_OPC(OPC_ALOAD)        // ALOAD
        *ostack++ = lvars[CP_SINDEX(pc)];
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD)        // LLOAD
    DEF_OPC(OPC_DLOAD)        // DLOAD
	*((u8*)ostack) = *(u8*)(&lvars[CP_SINDEX(pc)]);
        (u8*)ostack++;
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ALOAD_0)      // ALOAD_0
	if(mb->access_flags & ACC_STATIC)
            OPCODE_REWRITE(pc, OPC_ILOAD_0);
	else
            OPCODE_REWRITE(pc, OPC_ALOAD_THIS);
        DISPATCH(pc)

    DEF_OPC(OPC_ALOAD_THIS)   // ALOAD_THIS
        if(pc[1] == OPC_GETFIELD_QUICK) {
            OPCODE_REWRITE(pc, OPC_GETFIELD_THIS);
	    DISPATCH(pc)
	}

    DEF_OPC(OPC_ILOAD_0)      // ILOAD_0
    DEF_OPC(OPC_FLOAD_0)      // FLOAD_0
        *ostack++ = lvars[0];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_1)      // ILOAD_1
    DEF_OPC(OPC_FLOAD_1)      // FLOAD_1
    DEF_OPC(OPC_ALOAD_1)      // ALOAD_1
        *ostack++ = lvars[1];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_2)      // ILOAD_2
    DEF_OPC(OPC_FLOAD_2)      // FLOAD_2
    DEF_OPC(OPC_ALOAD_2)      // ALOAD_2
        *ostack++ = lvars[2];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ILOAD_3)      // ILOAD_3
    DEF_OPC(OPC_FLOAD_3)      // FLOAD_3
    DEF_OPC(OPC_ALOAD_3)      // ALOAD_3
        *ostack++ = lvars[3];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_0)      // LLOAD_0
    DEF_OPC(OPC_DLOAD_0)      // DLOAD_0
	    *((u8*)ostack) = *(u8*)(&lvars[0]);
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_1)      // LLOAD_1
    DEF_OPC(OPC_DLOAD_1)      // DLOAD_1
	    *((u8*)ostack) = *(u8*)(&lvars[1]);
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_2)      // LLOAD_2
    DEF_OPC(OPC_DLOAD_2)      // DLOAD_2
	    *((u8*)ostack) = *(u8*)(&lvars[2]);
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LLOAD_3)      // LLOAD_3
    DEF_OPC(OPC_DLOAD_3)      // DLOAD_3
	    *((u8*)ostack) = *(u8*)(&lvars[3]);
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_IALOAD)       // IALOAD
    DEF_OPC(OPC_AALOAD)       // AALOAD
    DEF_OPC(OPC_FALOAD)       // FALOAD
        ARRAY_LOAD(long, ostack, pc);

    DEF_OPC(OPC_LALOAD)       // LALOAD
        ARRAY_LOAD_LONG(long long, ostack, pc);

    DEF_OPC(OPC_DALOAD)       // DALOAD
        ARRAY_LOAD_LONG(long long, ostack, pc);

    DEF_OPC(OPC_BALOAD)       // BALOAD
        ARRAY_LOAD(signed char, ostack, pc);

    DEF_OPC(OPC_CALOAD)       // CALOAD
    DEF_OPC(OPC_SALOAD)       // SALOAD
        ARRAY_LOAD(short, ostack, pc);

    DEF_OPC(OPC_LSTORE)       // LSTORE
    DEF_OPC(OPC_DSTORE)       // DSTORE
        ostack = (u8*)ostack - 1;
	    *(u8*)(&lvars[CP_SINDEX(pc)]) = *((u8*)ostack);
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE)       // ISTORE
    DEF_OPC(OPC_FSTORE)       // FSTORE
    DEF_OPC(OPC_ASTORE)       // ASTORE
        lvars[CP_SINDEX(pc)] = *--ostack;
        pc += 2;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_0)     // ISTORE_0
    DEF_OPC(OPC_ASTORE_0)     // ASTORE_0
    DEF_OPC(OPC_FSTORE_0)     // FSTORE_0
        lvars[0] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_1)     // ISTORE_1
    DEF_OPC(OPC_ASTORE_1)     // ASTORE_1
    DEF_OPC(OPC_FSTORE_1)     // FSTORE_1
        lvars[1] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_2)     // ISTORE_2
    DEF_OPC(OPC_ASTORE_2)     // ASTORE_2
    DEF_OPC(OPC_FSTORE_2)     // FSTORE_2
        lvars[2] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_ISTORE_3)     // ISTORE_3
    DEF_OPC(OPC_ASTORE_3)     // ASTORE_3
    DEF_OPC(OPC_FSTORE_3)     // FSTORE_3
        lvars[3] = *--ostack;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_0)     // LSTORE_0
    DEF_OPC(OPC_DSTORE_0)     // DSTORE_0
        ostack = (u8*)ostack - 1;
        *(u8*)(&lvars[0]) = *((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_1)     // LSTORE_1
    DEF_OPC(OPC_DSTORE_1)     // DSTORE_1
        ostack = (u8*)ostack - 1;
        *(u8*)(&lvars[1]) = *((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_2)     // LSTORE_2
    DEF_OPC(OPC_DSTORE_2)     // DSTORE_2
        ostack = (u8*)ostack - 1;
        *(u8*)(&lvars[2]) = *((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_LSTORE_3)     // LSTORE_3
    DEF_OPC(OPC_DSTORE_3)     // DSTORE_3
        ostack = (u8*)ostack - 1;
        *(u8*)(&lvars[3]) = *((u8*)ostack);
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_IASTORE)     // IASTORE
    DEF_OPC(OPC_AASTORE)     // AASTORE
    DEF_OPC(OPC_FASTORE)     // FASTORE
        ARRAY_STORE(int, ostack, pc);

    DEF_OPC(OPC_LASTORE)     // LASTORE
    DEF_OPC(OPC_DASTORE)     // DASTORE
        ARRAY_STORE_LONG(double, ostack, pc);

    DEF_OPC(OPC_BASTORE)     // BASTORE
        ARRAY_STORE(char, ostack, pc);

    DEF_OPC(OPC_CASTORE)     // CASTORE
    DEF_OPC(OPC_SASTORE)     // SASTORE
        ARRAY_STORE(short, ostack, pc);

    DEF_OPC(OPC_POP)         // POP
        ostack--;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_POP2)        // POP2
        ostack -= 2;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP)         // DUP
        *ostack++ = ostack[-1];
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP_X1) {    // DUP_X1
        u8 word1 = ostack[-1];
        u8 word2 = ostack[-2];
        ostack[-2] = word1;
        ostack[-1] = word2;
        *ostack++ = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP_X2) {    // DUP_X2
        u8 word1 = ostack[-1];
        u8 word2 = ostack[-2];
        u8 word3 = ostack[-3];
        ostack[-3] = word1;
        ostack[-2] = word3;
        ostack[-1] = word2;
        *ostack++ = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP2)        // DUP2
        *((u8*)ostack) = ((u8*)ostack)[-1];
        (u8*)ostack++;
        pc += 1;
        DISPATCH(pc)

    DEF_OPC(OPC_DUP2_X1) {  // DUP2_X1
        u8 word1 = ostack[-1];
        u8 word2 = ostack[-2];
        u8 word3 = ostack[-3];
        ostack[-3] = word2;
        ostack[-2] = word1;
        ostack[-1] = word3;
        ostack[0]  = word2;
        ostack[1]  = word1;
        ostack += 2;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DUP2_X2) {  // DUP2_X2
        u8 word1 = ostack[-1];
        u8 word2 = ostack[-2];
        u8 word3 = ostack[-3];
        u8 word4 = ostack[-4];
        ostack[-4] = word2;
        ostack[-3] = word1;
        ostack[-2] = word4;
        ostack[-1] = word3;
        ostack[0]  = word2;
        ostack[1]  = word1;
        ostack += 2;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_SWAP) {     // SWAP
        u8 word1 = ostack[-1];
        ostack[-1] = ostack[-2];
        ostack[-2] = word1;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IADD)       // ARITHMATICH
        BINARY_OP(int, +, ostack, pc);

    DEF_OPC(OPC_LADD)
        BINARY_OP(long long, +, ostack, pc);

    DEF_OPC(OPC_FADD)
        BINARY_OP(float, +, ostack, pc);

    DEF_OPC(OPC_DADD)
        BINARY_OP(double, +, ostack, pc);

    DEF_OPC(OPC_ISUB)
        BINARY_OP(int, -, ostack, pc);

    DEF_OPC(OPC_LSUB)
        BINARY_OP(long long, -, ostack, pc);

    DEF_OPC(OPC_FSUB)
        BINARY_OP(float, -, ostack, pc);

    DEF_OPC(OPC_DSUB)
        BINARY_OP(double, -, ostack, pc);

    DEF_OPC(OPC_IMUL)
        BINARY_OP(int, *, ostack, pc);

    DEF_OPC(OPC_LMUL)
        BINARY_OP(long long, *, ostack, pc);

    DEF_OPC(OPC_FMUL)
        BINARY_OP(float, *, ostack, pc);

    DEF_OPC(OPC_DMUL)
        BINARY_OP(double, *, ostack, pc);

    DEF_OPC(OPC_IDIV)
	    ZERO_DIVISOR_CHECK(int, ostack);
        BINARY_OP(int, /, ostack, pc);

    DEF_OPC(OPC_LDIV)
	    ZERO_DIVISOR_CHECK(long long, ostack);
        BINARY_OP(long long, /, ostack, pc);

    DEF_OPC(OPC_FDIV)
        BINARY_OP(float, /, ostack, pc);

    DEF_OPC(OPC_DDIV)
        BINARY_OP(double, /, ostack, pc);

    DEF_OPC(OPC_IREM)
	    ZERO_DIVISOR_CHECK(int, ostack);
        BINARY_OP(int, %, ostack, pc);

    DEF_OPC(OPC_LREM)
	    ZERO_DIVISOR_CHECK(long long, ostack);
        BINARY_OP(long long, %, ostack, pc);

    DEF_OPC(OPC_FREM) {
        ostack = (float *)ostack - 1;
        float v2 = *((float *)ostack);
        ostack = (float *)ostack - 1;
        float v1 = *((float *)ostack);

        *((float *)ostack) = fmod(v1, v2);
        (float *)ostack++;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_DREM) {
        ostack = (double *)ostack - 1;
        double v2 = *((double *)ostack);
        ostack = (double *)ostack - 1;
        double v1 = *((double *)ostack);

        *((double *)ostack) = fmod(v1, v2);
        (double *)ostack++;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INEG)
        UNARY_MINUS(int, ostack, pc);

    DEF_OPC(OPC_LNEG)
        UNARY_MINUS(long long, ostack, pc);

    DEF_OPC(OPC_FNEG)
        UNARY_MINUS(float, ostack, pc);

    DEF_OPC(OPC_DNEG)
        UNARY_MINUS(double, ostack, pc);

    DEF_OPC(OPC_ISHL)
        BINARY_OP(int, <<, ostack, pc);

    DEF_OPC(OPC_LSHL)
        SHIFT_OP(long long, <<, ostack, pc);

    DEF_OPC(OPC_ISHR)
        BINARY_OP(int, >>, ostack, pc);

    DEF_OPC(OPC_LSHR)
        SHIFT_OP(long long, >>, ostack, pc);

    DEF_OPC(OPC_IUSHR)
        SHIFT_OP(unsigned int, >>, ostack, pc);

    DEF_OPC(OPC_LUSHR)
        SHIFT_OP(unsigned long long, >>, ostack, pc);

    DEF_OPC(OPC_IAND)
        BINARY_OP(int, &, ostack, pc);

    DEF_OPC(OPC_LAND)
        BINARY_OP(long long, &, ostack, pc);

    DEF_OPC(OPC_IOR)
        BINARY_OP(int, |, ostack, pc);

    DEF_OPC(OPC_LOR)
        BINARY_OP(long long, |, ostack, pc);

    DEF_OPC(OPC_IXOR)
        BINARY_OP(int, ^, ostack, pc);

    DEF_OPC(OPC_LXOR)
        BINARY_OP(long long, ^, ostack, pc);

    DEF_OPC(OPC_IINC)
        lvars[CP_SINDEX(pc)] += (signed char)pc[2];
        pc += 3;
        DISPATCH(pc)

    DEF_OPC(OPC_I2L)
        OPC_X2Y(int, long long);

    DEF_OPC(OPC_I2F)
        OPC_X2Y(int, float);

    DEF_OPC(OPC_I2D)
        OPC_X2Y(int, double);

    DEF_OPC(OPC_L2I)
        OPC_X2Y(long long, int);

    DEF_OPC(OPC_L2F)
        OPC_X2Y(long long, float);

    DEF_OPC(OPC_L2D)
        OPC_X2Y(long long, double);

    DEF_OPC(OPC_F2I)
        OPC_X2Y(float, int);

    DEF_OPC(OPC_F2L)
        OPC_X2Y(float, long long);

    DEF_OPC(OPC_F2D)
        OPC_X2Y(float, double);

    DEF_OPC(OPC_D2I)
        OPC_X2Y(double, int);

    DEF_OPC(OPC_D2L)
        OPC_X2Y(double, long long);

    DEF_OPC(OPC_D2F)
        OPC_X2Y(double, float);

    DEF_OPC(OPC_I2B)
    {
        signed char v = ostack[-1] & 0xff;
        ostack[-1] = (int) v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_I2C)
    {
        int v = ostack[-1] & 0xffff;
        ostack[-1] = v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_I2S)
    {
        signed short v = ostack[-1] & 0xffff;
        ostack[-1] = (int) v;
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_LCMP)
        CMP(long long, ostack, pc);

    DEF_OPC(OPC_DCMPG)
    DEF_OPC(OPC_DCMPL)
        FCMP(double, ostack, pc, (*pc == OPC_DCMPG ? 1 : -1));

    DEF_OPC(OPC_FCMPG)
    DEF_OPC(OPC_FCMPL)
        FCMP(float, ostack, pc, (*pc == OPC_FCMPG ? 1 : -1));

    DEF_OPC(OPC_IFEQ)
        IF(==, ostack, pc);

    DEF_OPC(OPC_IFNE)
        IF(!=, ostack, pc);

    DEF_OPC(OPC_IFLT)
        IF(<, ostack, pc);

    DEF_OPC(OPC_IFGE)
        IF(>=, ostack, pc);

    DEF_OPC(OPC_IFGT)
        IF(>, ostack, pc);

    DEF_OPC(OPC_IFLE)
        IF(<=, ostack, pc);

    DEF_OPC(OPC_IF_ACMPEQ)
    DEF_OPC(OPC_IF_ICMPEQ)
	IF_ICMP(==, ostack, pc);

    DEF_OPC(OPC_IF_ACMPNE)
    DEF_OPC(OPC_IF_ICMPNE)
	IF_ICMP(!=, ostack, pc);

    DEF_OPC(OPC_IF_ICMPLT)
	IF_ICMP(<, ostack, pc);

    DEF_OPC(OPC_IF_ICMPGE)
	IF_ICMP(>=, ostack, pc);

    DEF_OPC(OPC_IF_ICMPGT)
	IF_ICMP(>, ostack, pc);

    DEF_OPC(OPC_IF_ICMPLE)
	IF_ICMP(<=, ostack, pc);

    DEF_OPC(OPC_GOTO)
        pc += BRANCH(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_JSR)
        *ostack++ = (u8)pc+3;
        pc += BRANCH(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_RET)         // RET
        pc = (unsigned char*)lvars[CP_SINDEX(pc)];
        DISPATCH(pc)

    DEF_OPC(OPC_TABLESWITCH)
    {
        int *aligned_pc = (int*)((int)(pc + 4) & ~0x3);
        int deflt = ntohl(aligned_pc[0]);
        int low   = ntohl(aligned_pc[1]);
        int high  = ntohl(aligned_pc[2]);
        int index = *--ostack;

        if(index < low || index > high)
            pc += deflt;
        else
            pc += ntohl(aligned_pc[index - low + 3]);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_LOOKUPSWITCH)
    {
        int *aligned_pc = (int*)((int)(pc + 4) & ~0x3);
        int deflt  = ntohl(aligned_pc[0]);
        int npairs = ntohl(aligned_pc[1]);
        int key    = *--ostack;
        int i;

        for(i = 2; (i < npairs*2+2) && (key != ntohl(aligned_pc[i])); i += 2);

        if(i == npairs*2+2)
            pc += deflt;
        else
            pc += ntohl(aligned_pc[i+1]);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_IRETURN)
    DEF_OPC(OPC_ARETURN)
    DEF_OPC(OPC_FRETURN)
        *lvars++ = *--ostack;
        goto methodReturn;

    DEF_OPC(OPC_LRETURN)
    DEF_OPC(OPC_DRETURN)
        ostack = (u8*)ostack - 1;
	    *((u8*)lvars) = *((u8*)ostack);
	    (u8*)lvars++;
        goto methodReturn;

    DEF_OPC(OPC_RETURN)      // RETURN
        goto methodReturn;

    DEF_OPC(OPC_GETSTATIC)   // GETSTATIC
    {
        FieldBlock *fb;
	       
        frame->last_pc = (unsigned char*)pc;
	    fb = resolveField(mb->class, CP_DINDEX(pc));

        if(exceptionOccured0(ee))
            goto throwException;

        if((*fb->type == 'J') || (*fb->type == 'D'))
            OPCODE_REWRITE(pc, OPC_GETSTATIC2_QUICK);
        else
            OPCODE_REWRITE(pc, OPC_GETSTATIC_QUICK);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC)        // PUTSTATIC
    {
        FieldBlock *fb;
	       
        frame->last_pc = (unsigned char*)pc;
	    fb = resolveField(mb->class, CP_DINDEX(pc));

	    if (CP_DINDEX(pc)==2 && strcmp(fb->name, "current")==0) {
	        int herve = 1;
	    }
        if(exceptionOccured0(ee))
            goto throwException;

        if((*fb->type == 'J') || (*fb->type == 'D'))
            OPCODE_REWRITE(pc, OPC_PUTSTATIC2_QUICK);
        else
            OPCODE_REWRITE(pc, OPC_PUTSTATIC_QUICK);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETSTATIC_QUICK)   // GETSTATIC_QUICK
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));

        *ostack++ = fb->static_value;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETSTATIC2_QUICK)  // GETSTATIC2_QUICK
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        *(u8*)ostack = *(u8*)&fb->static_value;
        ostack += 2;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC_QUICK)    // PUTSTATIC_QUICK
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        fb->static_value = *--ostack;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTSTATIC2_QUICK)    // PUTSTATIC2_QUICK
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        ostack -= 2;
        *(u8*)&fb->static_value = *(u8*)ostack;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD)            // GETFIELD
    {
        int idx;
        FieldBlock *fb;

        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_GETFIELD, idx);

        frame->last_pc = (unsigned char*)pc;
        fb = resolveField(mb->class, idx);

        if(exceptionOccured0(ee))
            goto throwException;

        if(fb->offset > 255)
            OPCODE_REWRITE(pc, OPC_GETFIELD_QUICK_W);
        else
            OPCODE_REWRITE_OPERAND1(pc, ((*fb->type == 'J') || (*fb->type == 'D') ? 
                 OPC_GETFIELD2_QUICK : OPC_GETFIELD_QUICK), fb->offset);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD)            // PUTFIELD
    {
        int idx;
        FieldBlock *fb;

	    WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_PUTFIELD, idx);

        frame->last_pc = (unsigned char*)pc;
        fb = resolveField(mb->class, idx);

        if(exceptionOccured0(ee))
            goto throwException;

        if(fb->offset > 255)
            OPCODE_REWRITE(pc, OPC_PUTFIELD_QUICK_W);
        else
            OPCODE_REWRITE_OPERAND1(pc, ((*fb->type == 'J') || (*fb->type == 'D') ? 
                 OPC_PUTFIELD2_QUICK : OPC_PUTFIELD_QUICK), fb->offset);

        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD_QUICK)      // GETFIELD_QUICK
    {
        Object *o = (Object *)ostack[-1];
	    NULL_POINTER_CHECK(o);
		
        ostack[-1] = INST_DATA(o)[pc[1]];
        pc += 3;
        DISPATCH(pc)
    }

        DEF_OPC(OPC_GETFIELD_THIS)  // GETFIELD_THIS
        *ostack++ = INST_DATA(this)[pc[2]];
        pc += 4;
        DISPATCH(pc)

    DEF_OPC(OPC_GETFIELD2_QUICK)    // GETFIELD2_QUICK
    {
        Object *o = (Object *)*--ostack;
	    NULL_POINTER_CHECK(o);
		
        *((u8*)ostack) = *(u8*)(&(INST_DATA(o)[pc[1]]));
        (u8*)ostack++;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GETFIELD_QUICK_W)     // GETFIELD_QUICK_W)
    {
        FieldBlock *fb = (FieldBlock*)CP_INFO(cp, CP_DINDEX(pc));
        Object *o = (Object *)*--ostack;
        u8 *addr;

	    NULL_POINTER_CHECK(o);
		
        addr = &(INST_DATA(o)[fb->offset]);

        if((*fb->type == 'J') || (*fb->type == 'D')) {
            u8 v = *(u8*)addr;
            *(u8*)ostack = v;
            ostack += 2;
        } else {
            u8 v = *addr;
            *ostack++ = v; 
        }

        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD_QUICK_W)     // PUTFIELD_QUICK_W
    {
        FieldBlock *fb = (FieldBlock *)CP_INFO(cp, CP_DINDEX(pc));
        Object *o;
 
        if((*fb->type == 'J') || (*fb->type == 'D')) {
            u8 v, *addr;
            ostack -= 2;
            v = *(u8*)ostack;
            o = (Object *)*--ostack;
	        NULL_POINTER_CHECK(o);

            addr = (u8*)&(INST_DATA(o)[fb->offset]);
            *addr = v;
        } else {
            u8 *addr, v = *--ostack;
            o = (Object *)*--ostack;
	        NULL_POINTER_CHECK(o);

            addr = &(INST_DATA(o)[fb->offset]);
            *addr = v;
        }

        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD_QUICK)        // PUTFIELD_QUICK
    {
        Object *o = (Object *)ostack[-2];
	    NULL_POINTER_CHECK(o);
		
        INST_DATA(o)[pc[1]] = ostack[-1];
        ostack -= 2;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_PUTFIELD2_QUICK)       // PUTFIELD2_QUICK
    {
        Object *o = (Object *)ostack[-3];
	    NULL_POINTER_CHECK(o);
		
        *(u8*)(&(INST_DATA(o)[pc[1]])) = *(u8*)(&ostack[-2]);
        ostack -= 3;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKEVIRTUAL)         // INVOKEVIRTUAL
    {
        int idx;
        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_INVOKEVIRTUAL, idx);
        if (idx==64) {
            int herve = 1;
        }

        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, idx);
 
        if(exceptionOccured0(ee))
            goto throwException;

        if((new_mb->args_count < 256) && (new_mb->method_table_index < 256)) {
            OPCODE_REWRITE_OPERAND2(pc, OPC_INVOKEVIRTUAL_QUICK, new_mb->method_table_index, new_mb->args_count);
	    } else
            OPCODE_REWRITE(pc, OPC_INVOKEVIRTUAL_QUICK_W);
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKEVIRTUAL_QUICK_W)  // INVOKEVIRTUAL_QUICK_W
        new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
        arg1 = ostack - (new_mb->args_count);
	    NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
        new_mb = CLASS_CB(new_class)->method_table[new_mb->method_table_index];

        goto invokeMethod;

    DEF_OPC(OPC_INVOKESPECIAL)          // INVOKESPECIAL
    {
        int idx;
        WITH_OPCODE_CHANGE_CP_DINDEX(pc, OPC_INVOKESPECIAL, idx);

        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, idx);
        if (idx==101 && strcmp("<init>", new_mb->name)==0) {
            int herve = 1;
        }
        if(exceptionOccured0(ee))
            goto throwException;

        /* Check if invoking a super method... */
        if((CLASS_CB(mb->class)->access_flags & ACC_SUPER) && ((new_mb->access_flags & ACC_PRIVATE) == 0) && (new_mb->name[0] != '<')) {
                OPCODE_REWRITE_OPERAND2(pc, OPC_INVOKESUPER_QUICK,
                        new_mb->method_table_index >> 8,
                        new_mb->method_table_index & 0xff);
        } else
                OPCODE_REWRITE(pc, OPC_INVOKENONVIRTUAL_QUICK);

        // pc not change here, so while loop goes OPC_INVOKESUPER_QUICK next time
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INVOKESUPER_QUICK)        // INVOKESUPER_QUICK
        new_mb = CLASS_CB(CLASS_CB(mb->class)->super)->method_table[CP_DINDEX(pc)];
        arg1 = ostack - (new_mb->args_count);
        NULL_POINTER_CHECK(*arg1);
        goto invokeMethod;

    DEF_OPC(OPC_INVOKENONVIRTUAL_QUICK)   // INVOKENONVIRTUAL_QUICK
        new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
        arg1 = ostack - (new_mb->args_count);
        NULL_POINTER_CHECK(*arg1);
        goto invokeMethod;

    DEF_OPC(OPC_INVOKESTATIC)              // INVOKESTATIC
        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveMethod(mb->class, CP_DINDEX(pc));
        if (strcmp(new_mb->name, "getRuntime")==0) {
            int herve = 0;
        }
        if(exceptionOccured0(ee))
            goto throwException;
        OPCODE_REWRITE(pc, OPC_INVOKESTATIC_QUICK);
        DISPATCH(pc)

    DEF_OPC(OPC_INVOKESTATIC_QUICK)
        {
            new_mb = (MethodBlock *)CP_INFO(cp, CP_DINDEX(pc));
            arg1 = ostack - new_mb->args_count;
            goto invokeMethod;
        }

    DEF_OPC(OPC_INVOKEINTERFACE)
        frame->last_pc = (unsigned char*)pc;
        new_mb = resolveInterfaceMethod(mb->class, CP_DINDEX(pc));
 
        if(exceptionOccured0(ee))
            goto throwException;

        arg1 = ostack - (new_mb->args_count);
	    NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
	    new_mb = lookupMethod(new_class, new_mb->name, new_mb->type);

        goto invokeMethod;

    DEF_OPC(OPC_ARRAYLENGTH)
    {
        Object *array = (Object *)ostack[-1];
	    NULL_POINTER_CHECK(array);

//        ostack[-1] = *INST_DATA(array);
        int i_tmp = ARRAY_LEN_READ(array);
        ostack[-1] = ARRAY_LEN_READ(array);
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_ATHROW)
    {
        Object *ob = (Object *)ostack[-1];
        frame->last_pc = (unsigned char*)pc;
	    NULL_POINTER_CHECK(ob);
		
        ee->exception = ob;
        goto throwException;
    }

    DEF_OPC(OPC_NEW)                         // NEW
    {
        Class *class;
        Object *ob;
 
        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), TRUE);

        if(exceptionOccured0(ee))
            goto throwException;
        
        if((ob = allocObject(class)) == NULL)
            goto throwException;

        *ostack++ = (u8)ob;
        pc += 3;
        DISPATCH(pc)
    }
 
    DEF_OPC(OPC_NEWARRAY)
    {
        int type = pc[1];
        int count = *--ostack;
        Object *ob;

        frame->last_pc = (unsigned char*)pc;
        if((ob = allocTypeArray(type, count)) == NULL)
            goto throwException;

        *ostack++ = (u8)ob;
        pc += 2;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_ANEWARRAY)
    {
        Class *array_class;
        char ac_name[256];
        int count = ostack[-1];
        Class *class;
        Object *ob;
 
        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        if(CLASS_CB(class)->name[0] == '[')
            strcat(strcpy(ac_name, "["), CLASS_CB(class)->name);
        else
            strcat(strcat(strcpy(ac_name, "[L"), CLASS_CB(class)->name), ";");

        array_class = findArrayClass(ac_name);

        if(exceptionOccured0(ee))
            goto throwException;

        if((ob = allocArray(array_class, count, 4)) == NULL)
            goto throwException;

        ostack[-1] = (u8)ob;
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_CHECKCAST)
    {
        Object *obj = (Object*)ostack[-1]; 
        Class *class;
	       
        frame->last_pc = (unsigned char*)pc;
	    class = resolveClass(mb->class, CP_DINDEX(pc), TRUE);
 
        if(exceptionOccured0(ee))
            goto throwException;

        if((obj != NULL) && !isInstanceOf(class, obj->class))
            THROW_EXCEPTION("java/lang/ClassCastException", CLASS_CB(obj->class)->name);
    
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_INSTANCEOF)
    {
        Object *obj = (Object*)ostack[-1]; 
        Class *class;
	       
        frame->last_pc = (unsigned char*)pc;
	    class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        if(obj != NULL)
            ostack[-1] = isInstanceOf(class, obj->class); 
        pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_MONITORENTER)
    {
        Object *ob = (Object *)*--ostack;
        NULL_POINTER_CHECK(ob);
        objectLock(ob);
        pc += 1;
	    DISPATCH(pc)
    }

    DEF_OPC(OPC_MONITOREXIT)
    {
        Object *ob = (Object *)*--ostack;
        NULL_POINTER_CHECK(ob);
        objectUnlock(ob);
        pc += 1;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_WIDE)
    {
       int opcode = pc[1];
        switch(opcode) {
            case OPC_ILOAD:
            case OPC_FLOAD:
            case OPC_ALOAD:
                *ostack++ = lvars[CP_DINDEX((pc+1))];
                pc += 4;
		break;

            case OPC_LLOAD:
            case OPC_DLOAD:
                *((u8*)ostack) = *(u8*)(&lvars[CP_DINDEX((pc+1))]);
                (u8*)ostack++;
                pc += 4;
		break;

            case OPC_ISTORE:
            case OPC_FSTORE:
            case OPC_ASTORE:
                lvars[CP_DINDEX((pc+1))] = *--ostack;
                pc += 4;
		break;

            case OPC_LSTORE:
            case OPC_DSTORE:
                ostack = (u8*)ostack - 1;
                *(u8*)(&lvars[CP_DINDEX((pc+1))]) = *((u8*)ostack);
                pc += 4;
		break;

            case OPC_RET:
                pc = (unsigned char*)lvars[CP_DINDEX((pc+1))];
		break;

            case OPC_IINC:
                lvars[CP_DINDEX((pc+1))] += DSIGNED((pc+3));
                pc += 6;
		break;
        }
        DISPATCH(pc)
   }

    DEF_OPC(OPC_MULTIANEWARRAY)
    {
        Class *class;
        int dim = pc[3];
	    Object *ob;

        frame->last_pc = (unsigned char*)pc;
        class = resolveClass(mb->class, CP_DINDEX(pc), FALSE);

        if(exceptionOccured0(ee))
            goto throwException;

        ostack -= dim;

        if((ob = allocMultiArray(class, dim, ostack)) == NULL)
            goto throwException;

        *ostack++ = (u8)ob;
        pc += 4;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IFNULL)
    {
        int v = *--ostack;
        if(v == 0) {
           pc += BRANCH(pc);
        } else 
           pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_IFNONNULL)
    {
        int v = *--ostack;
        if(v != 0) {
           pc += BRANCH(pc);
        } else 
           pc += 3;
        DISPATCH(pc)
    }

    DEF_OPC(OPC_GOTO_W)
        pc += BRANCH_W(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_JSR_W)
        *ostack++ = (u8)pc+3;
        pc += BRANCH_W(pc);
        DISPATCH(pc)

    DEF_OPC(OPC_LOCK)
        DISPATCH(pc)

    DEF_OPC(OPC_INVOKEVIRTUAL_QUICK)
    // pc[0] opcode
    // pc[1] method table index
    // pc[2] args count
        arg1 = ostack - pc[2];

        unsigned char char_tmp = pc[2];
        u8 tmp = *arg1;
	    NULL_POINTER_CHECK(*arg1);

        new_class = (*(Object **)arg1)->class;
        new_mb = CLASS_CB(new_class)->method_table[pc[1]];
        int herve = 1;
invokeMethod:
{
    // Create new frame first.
    // This is also created for natives so that they appear correctly in the stack trace
    Frame *new_frame = (Frame *)(arg1 + new_mb->max_locals);
    Object *sync_ob = NULL;

    if((char*)new_frame > ee->stack_end) {
        ee->stack_end += 1024;
        THROW_EXCEPTION("java/lang/StackOverflowError", NULL);
    }

    new_frame->mb = new_mb;   // new mb supposed to be just after this frame?
    new_frame->lvars = arg1;
    new_frame->ostack = (u8*)(new_frame+1);
    new_frame->prev = frame;
    frame->last_pc = (unsigned char*)pc;

    ee->last_frame = new_frame;

    if(new_mb->access_flags & ACC_SYNCHRONIZED) {
        sync_ob = new_mb->access_flags & ACC_STATIC ? (Object*)new_mb->class : (Object*)*arg1;
	    objectLock(sync_ob);
    }

    if(new_mb->access_flags & ACC_NATIVE) {
        ostack = (*(u8 *(*)(Class*, MethodBlock*, u8*))new_mb->native_invoker)(new_mb->class, new_mb, arg1);

        if(sync_ob)
            objectUnlock(sync_ob);

        ee->last_frame = frame;

        if(exceptionOccured0(ee))
                goto throwException;
	    pc += *pc == OPC_INVOKEINTERFACE ? 5 : 3;
    }
    // ready to execute
    else {
        frame = new_frame;
        mb = new_mb;
        lvars = new_frame->lvars;
        this = (Object*)lvars[0];
        ostack = new_frame->ostack;
        pc = mb->code;
        cp = &(CLASS_CB(mb->class)->constant_pool);
    }
    DISPATCH(pc)
}

methodReturn:
    /* Set interpreter state to previous frame */

    frame = frame->prev;
    if(frame->mb == NULL) {
        /* The previous frame is a dummy frame - this indicates
           top of this Java invocation. */
        return ostack;
    }

    if(mb->access_flags & ACC_SYNCHRONIZED) {
        Object *sync_ob = mb->access_flags & ACC_STATIC ? (Object*)mb->class : this;
	    objectUnlock(sync_ob);
    }

    mb = frame->mb;
    ostack = lvars;
    lvars = frame->lvars;
    this = (Object*)lvars[0];
    pc = frame->last_pc;
    pc += *pc == OPC_INVOKEINTERFACE ? 5 : 3;
    cp = &(CLASS_CB(mb->class)->constant_pool);

    /* Pop frame */ 
    ee->last_frame = frame;
    DISPATCH(pc)

throwException:
    {
        Object *excep = ee->exception;
        clearException();

        pc = findCatchBlock(excep->class);

        if(pc == NULL) {
            ee->exception = excep;
            return NULL;
        }

        frame = ee->last_frame;
        mb = frame->mb;
        ostack = frame->ostack;
        lvars = frame->lvars;
        this = (Object*)lvars[0];
        cp = &(CLASS_CB(mb->class)->constant_pool);

        *ostack++ = (u8)excep;
        DISPATCH(pc)
    }
#ifndef THREADED
  }}
#endif
}

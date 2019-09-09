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

#ifdef NO_JNI
#error to use classpath, Jam must be compiled with JNI!
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "jam.h"
#include "thread.h"
#include "lock.h"

/* java.lang.VMObject */

u8 *getClass(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *ob = (Object*)*ostack;
    *ostack++ = (u8)ob->class;
    return ostack;
}

u8 *jamClone(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *ob = (Object*)*ostack;
    *ostack++ = (u8)cloneObject(ob);
    return ostack;
}

/* static method wait(Ljava/lang/Object;JI)V */
u8 *wait_(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *obj = (Object *)ostack[0];
    long long ms = *((long long *)&ostack[1]);
    int ns = ostack[3];

    objectWait(obj, ms, ns);
    return ostack;
}

/* static method notify(Ljava/lang/Object;)V */
u8 *notify(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *obj = (Object *)*ostack;
    objectNotify(obj);
    return ostack;
}

/* static method notifyAll(Ljava/lang/Object;)V */
u8 *notifyAll(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *obj = (Object *)*ostack;
    objectNotifyAll(obj);
    return ostack;
}

/* java.lang.VMSystem */

/* arraycopy(Ljava/lang/Object;ILjava/lang/Object;II)V */
u8 *arraycopy(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *src = (Object *)ostack[0];
    int start1 = ostack[1];
    Object *dest = (Object *)ostack[2];
    int start2 = ostack[3];
    int length = ostack[4];


    if((src == NULL) || (dest == NULL))
	signalException("java/lang/NullPointerException", NULL);
    else {
        ClassBlock *scb = CLASS_CB(src->class);
        ClassBlock *dcb = CLASS_CB(dest->class);
        int *sdata = INST_DATA(src);            
        int *ddata = INST_DATA(dest);            

        if((scb->name[0] != '[') || (dcb->name[0] != '['))
            goto storeExcep; 

        if((start1 < 0) || (start2 < 0) || (length < 0)
                        || ((start1 + length) > sdata[0]) || ((start2 + length) > ddata[0])) {
            signalException("java/lang/ArrayIndexOutOfBoundsException", NULL);
            return ostack;
	}

        if(isInstanceOf(dest->class, src->class)) {
            int size;

            switch(scb->name[1]) {
                case 'B':
                    size = 1;
                    break;
                case 'C':
                case 'S':
                    size = 2;
                    break;
                case 'I':
                case 'F':
                case 'L':
                case '[':
                    size = 4;
                    break;
                case 'J':
                case 'D':
                    size = 8;
                    break;
            } 

            memmove(((char *)&ddata[1]) + start2*size,
                    ((char *)&sdata[1]) + start1*size,
                    length*size);
	} else {
	    Object **sob, **dob;
	    int i;

   	    if(!(((scb->name[1] == 'L') || (scb->name[1] == '[')) &&
   	                  ((dcb->name[1] == 'L') || (dcb->name[1] == '['))))
                goto storeExcep; 

	    /* Not compatible array types, but elements may be compatible...
	       e.g. src = [Ljava/lang/Object, dest = [Ljava/lang/String, but all src = Strings -
	       check one by one...
	     */
	    
	    if(scb->dim != dcb->dim)
                goto storeExcep;

	    sob = (Object**)&sdata[start1+1];
	    dob = (Object**)&ddata[start2+1];

	    if(scb->dim == 1)
	        for(i = 0; i < length; i++) {
                    if(*sob && !isInstanceOf(dcb->element_class, (*sob)->class))
                        goto storeExcep;
                    *dob++ = *sob++;
	        }
	    else
	        for(i = 0; i < length; i++) {
                    if(*sob && !isInstanceOf(dcb->element_class, CLASS_CB((*sob)->class)->element_class))
                        goto storeExcep;
                    *dob++ = *sob++;
	        }
	}
    }
    return ostack;

storeExcep:
    signalException("java/lang/ArrayStoreException", NULL);
    return ostack;
}

u8 *identityHashCode(Class *class, MethodBlock *mb, u8 *ostack) {
    return ++ostack;
}

/* java.lang.Runtime */

u8 *freeMemory(Class *class, MethodBlock *mb, u8 *ostack) {
    *((u8*)ostack) = (u8) freeHeapMem();
    (u8*)ostack++;
    return ostack;
}

u8 *totalMemory(Class *class, MethodBlock *mb, u8 *ostack) {
    *((u8*)ostack) = (u8) totalHeapMem();
    (u8*)ostack++;
    return ostack;
}

u8 *maxMemory(Class *class, MethodBlock *mb, u8 *ostack) {
    *((u8*)ostack) = (u8) maxHeapMem();
    (u8*)ostack++;
    return ostack;
}

u8 *gc(Class *class, MethodBlock *mb, u8 *ostack) {
    gc1();
    return ostack;
}

u8 *runFinalization(Class *class, MethodBlock *mb, u8 *ostack) {
    return ostack;
}

u8 *exitInternal(Class *class, MethodBlock *mb, u8 *ostack) {
    exit(0);
}

u8 *nativeLoad(Class *class, MethodBlock *mb, u8 *ostack) {
    char *name = String2Cstr((Object*)ostack[1]);

    ostack[0] = resolveDll(name);
    free(name);

    return ostack+1;
}

u8 *nativeGetLibname(Class *class, MethodBlock *mb, u8 *ostack) {
    char *path = String2Cstr((Object*)ostack[0]);
    char *name = String2Cstr((Object*)ostack[1]);
    *ostack++ = (u8)Cstr2String(getDllName(path, name));
    return ostack;
}

void setProperty(Object *this, char *key, char *value) {
    Object *k = Cstr2String(key);
    Object *v = Cstr2String(value);

    MethodBlock *mb = lookupMethod(this->class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    executeMethod(this, mb, k, v);
}

u8 *insertSystemProperties(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;

    setProperty(this, "java.version", "");
    setProperty(this, "java.vendor", "");
    setProperty(this, "java.vendor.url", "");
    setProperty(this, "java.home", "");
    setProperty(this, "java.vm.specification.version", "2");
    setProperty(this, "java.vm.specification.vendor", "");
    setProperty(this, "java.vm.specification.name", "");
    setProperty(this, "java.class.version", "");
    setProperty(this, "java.class.path", getClassPath());
    setProperty(this, "java.library.path", getDllPath());
    setProperty(this, "java.io.tmpdir", "");
    setProperty(this, "java.compiler", "");
    setProperty(this, "java.ext.dirs", "");
    setProperty(this, "os.name", "");
    setProperty(this, "os.arch", "");
    setProperty(this, "os.version", "");
    setProperty(this, "file.separator", "/");
    setProperty(this, "path.separator", ":");
    setProperty(this, "line.separator", "\n");
    setProperty(this, "user.name", getenv("USER"));
    setProperty(this, "user.home", getenv("HOME"));
    setProperty(this, "user.dir", getenv("HOME"));

    return ostack;
}

/* java.lang.Class */

/* forName0(Ljava/lang/String;IL/java/lang/ClassLoader)Ljava/lang/Class; */
u8 *forName0(Class *clazz, MethodBlock *mb, u8 *ostack) {
    Object *string = (Object *)ostack[0];
    int resolve = ostack[1];
    Object *loader = (Object *)ostack[2];
    char *cstr = String2Cstr(string);
    Class *class;
    int i;
    
    for(i = 0; i < strlen(cstr); i++)
        if(cstr[i]=='.') cstr[i]='/';

    class = findClassFromClassLoader(cstr, loader);
    free(cstr);

    if(class == NULL) {
        clearException();
	signalException("java/lang/ClassNotFoundException", NULL);
    } else
        if(resolve)
            initClass(class);

    *ostack++ = (u8)class;
    return ostack;
}

u8 *isInstance(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *clazz = (Class*)ostack[0];
    Object *ob = (Object*)ostack[1];

    *ostack++ = ob == NULL ? FALSE : (u8)isInstanceOf(clazz, ob->class);
    return ostack;
}

u8 *isAssignableFrom(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *clazz = (Class*)ostack[0];
    Class *clazz2 = (Class*)ostack[1];

    if(clazz2 == NULL)
        signalException("java/lang/NullPointerException", NULL);
    else
        *ostack++ = (u8)isInstanceOf(clazz, clazz2);

    return ostack;
}

u8 *isInterface(Class *class, MethodBlock *mb, u8 *ostack) {
    ClassBlock *cb = CLASS_CB((Class*)ostack[0]);
    *ostack++ = IS_INTERFACE(cb) ? TRUE : FALSE;
    return ostack;
}

u8 *isPrimitive(Class *class, MethodBlock *mb, u8 *ostack) {
    ClassBlock *cb = CLASS_CB((Class*)ostack[0]);
    *ostack++ = IS_PRIMITIVE(cb) ? TRUE : FALSE;
    return ostack;
}

u8 *getSuperclass(Class *class, MethodBlock *mb, u8 *ostack) {
    ClassBlock *cb = CLASS_CB((Class*)ostack[0]);
    *ostack++ = (u8) (IS_PRIMITIVE(cb) || IS_INTERFACE(cb) ? NULL : cb->super);
    return ostack;
}

u8 *newInstance(Class *clazz, MethodBlock *mb, u8 *ostack) {
    Class *class = (Class*)*ostack;
    Object *ob = allocObject(class);
    MethodBlock *init = findMethod(class, "<init>", "()V");

    executeMethod(ob, init);
    *ostack++ = (u8)ob;
    return ostack;
}

u8 *getName(Class *class, MethodBlock *mb, u8 *ostack) {
    unsigned char *dot_name = slash2dots(CLASS_CB(((Class*)*ostack))->name);
    Object *string = createString(dot_name);
    *ostack++ = (u8)string;
    free(dot_name);
    return ostack;
}

u8 *getConstructor(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *clazz = (Class*)ostack[0];
    Object *array = (Object*)ostack[1]; 

    *ostack++ = (u8) getClassConstructor(clazz, array);
    return ostack;
}

u8 *getClassLoader0(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *clazz = (Class*)*ostack;
    *ostack++ = (u8)CLASS_CB(clazz)->class_loader;
    return ostack;
}

uintptr_t *getClassLoader(Class *class, MethodBlock *mb, uintptr_t *ostack) {
    Class *clazz = (Class*)ostack[0];
    ClassBlock* cb_tmp = (uintptr_t)CLASS_CB(clazz);
    *ostack++ = (uintptr_t)CLASS_CB(clazz)->class_loader;
    return ostack;
}

/* java.lang.Throwable */

u8 *fillInStackTrace(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;

    setStackTrace(this);
    return ostack+1;
}

u8 *printStackTrace0(Class *class, MethodBlock *m, u8 *ostack) {
    Object *this = (Object *)*ostack;
    Object *writer = (Object *)ostack[1];

    printStackTrace(this, writer);
    return ostack;
}

/* java.lang.VMSecurityManager */

u8 *currentClassLoader(Class *class, MethodBlock *mb, u8 *ostack) {
    *ostack++ = (u8)NULL;
    return ostack;
}

u8 *getClassContext(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *class_class = findArrayClass("[Ljava/lang/Class;");
    Object *array;
    u8 *data;

    Frame *bottom, *last = getExecEnv()->last_frame;
    int depth = 0;
/*
    for(; last->mb != NULL && isInstanceOf(throw_class, last->mb->class);
          last = last->prev);
*/
    bottom = last;
    do {
        for(; last->mb != NULL; last = last->prev, depth++);
    } while((last = last->prev)->prev != NULL);
    
    array = allocArray(class_class, depth, 4);

    if(array != NULL) {
        data = INST_DATA(array);

        depth = 1;
        do {
            for(; bottom->mb != NULL; bottom = bottom->prev)
                data[depth++] = (long)bottom->mb->class;
        } while((bottom = bottom->prev)->prev != NULL);
    }
    int count_tmp = ARRAY_LEN(array);
    printf("array len=%d\n", count_tmp);
    u8* tmp = INST_DATA(array);
    for (int i = 0; i < count_tmp; i++) {
        printf("array[%d]=%p\n", i, data[i+1]);
    }
    *ostack++ = (u8)array;
    return ostack;
}

/* java.lang.VMClassLoader */

/* getPrimitiveClass(Ljava/lang/String;)Ljava/lang/Class; */
u8 *getPrimitiveClass(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *string = (Object *)*ostack;
    char *cstr = String2Cstr(string);
    *ostack++ = (u8)findPrimClass(cstr);

    free(cstr);
    return ostack;
}

u8 *defineClass0(Class *clazz, MethodBlock *mb, u8 *ostack) {
    Object *class_loader = (Object *)ostack[0];
    Object *string = (Object *)ostack[1];
    Object *array = (Object *)ostack[2];
    int offset = ostack[3];
    int len = ostack[4];
    char *data = ((char*)INST_DATA(array)) + 4;
    Class *class = defineClass(data, offset, len, class_loader);

    if(class != NULL)
        linkClass(class);

    *ostack++ = (int)class;
    return ostack;
}

u8 *resolveClass0(Class *class, MethodBlock *mb, u8 *ostack) {
    Class *clazz = (Class *)*ostack++;

    initClass(clazz);
    return ostack;
}

/* java.lang.reflect.Constructor */

u8 *constructNative(Class *class, MethodBlock *mb2, u8 *ostack) {
    Object *array = (Object*)ostack[1]; 
    Class *clazz = (Class*)ostack[2];
    MethodBlock *mb = (MethodBlock*)ostack[3]; 
    Object *ob = allocObject(clazz);

    *ostack++ = *(u8*)invoke(ob, clazz, mb, array);
    return ostack;
}

/* java.lang.VMString */

/* static method - intern(Ljava/lang/String;)Ljava/lang/String; */
u8 *intern(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *string = (Object*)ostack[0];
    ostack[0] = (u8)findInternedString(string);
    return ostack+1;
}

/* java.lang.Thread */

/* static method currentThread()Ljava/lang/Thread; */
u8 *currentThread(Class *class, MethodBlock *mb, u8 *ostack) {
    *ostack++ = (u8)getExecEnv()->thread;
    return ostack;
}

/* instance method nativeInit(J)V */
u8 *nativeInit(Class *class, MethodBlock *mb, u8 *ostack) {
    return ostack;
}

/* instance method start()V */
u8 *start(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;
    createJavaThread(this);
    return ostack;
}

/* static method sleep(JI)V */
u8 *jamSleep(Class *class, MethodBlock *mb, u8 *ostack) {
    long long ms = *((long long *)&ostack[0]);
    int ns = ostack[2];
    Thread *thread = threadSelf();

    threadSleep(thread, ms, ns);

    return ostack;
}

/* instance method interrupt()V */
u8 *interrupt(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;
    Thread *thread = threadSelf0(this);
    if(thread)
        threadInterrupt(thread);
    return ostack;
}

/* instance method isAlive()Z */
u8 *isAlive(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;
    Thread *thread = threadSelf0(this);
    *ostack++ = thread ? threadIsAlive(thread) : FALSE;
    return ostack;
}

/* static method yield()V */
u8 *yield(Class *class, MethodBlock *mb, u8 *ostack) {
    Thread *thread = threadSelf();
    threadYield(thread);
    return ostack;
}

/* instance method isInterrupted()Z */
u8 *isInterrupted(Class *class, MethodBlock *mb, u8 *ostack) {
    Object *this = (Object *)*ostack;
    Thread *thread = threadSelf0(this);
    *ostack++ = thread ? threadIsInterrupted(thread) : FALSE;
    return ostack;
}

/* static method interrupted()Z */
u8 *interrupted(Class *class, MethodBlock *mb, u8 *ostack) {
    Thread *thread = threadSelf();
    *ostack++ = threadInterrupted(thread);
    return ostack;
}

/* instance method nativeSetPriority()V */
u8 *nativeSetPriority(Class *class, MethodBlock *mb, u8 *ostack) {
    return ostack+1;
}

char *native_methods[][2] = {
                             "arraycopy",		(char*)arraycopy,
                             "insertSystemProperties",	(char*)insertSystemProperties,
                             "getPrimitiveClass",	(char*)getPrimitiveClass,
			     "defineClass",             (char*)defineClass0,
			     "resolveClass",            (char*)resolveClass0,
                             "intern",			(char*)intern,
                             "forName0",		(char*)forName0,
                             "newInstance",		(char*)newInstance,
			     "isInstance",		(char*)isInstance,
			     "isAssignableFrom",	(char*)isAssignableFrom,
			     "isInterface",		(char*)isInterface,
			     "isPrimitive",		(char*)isPrimitive,
                             "getSuperclass",		(char*)getSuperclass,
                             "getName",			(char*)getName,
                             "getClass",		(char*)getClass,
                             "identityHashCode",	(char*)identityHashCode,
			     "clone",			(char*)jamClone,
			     "wait",			(char*)wait_,
			     "notify",			(char*)notify,
			     "notifyAll",		(char*)notifyAll,
                             "gc",			(char*)gc,
                             "runFinalization",		(char*)runFinalization,
                             "exitInternal",		(char*)exitInternal,
                             "fillInStackTrace",	(char*)fillInStackTrace,
                             "printStackTrace0",	(char*)printStackTrace0,
			     "currentClassLoader",	(char*)currentClassLoader,
			     "getClassContext",		(char*)getClassContext,
			     "getConstructor",		(char*)getConstructor,
			     "getClassLoader0",		(char*)getClassLoader0,
                 "getClassLoader",		(char*)getClassLoader,
			     "constructNative",		(char*)constructNative,
                             "nativeLoad",		(char*)nativeLoad,
                             "nativeGetLibname",	(char*)nativeGetLibname,
			     "freeMemory",		(char*)freeMemory,
			     "totalMemory",		(char*)totalMemory,
			     "maxMemory",		(char*)maxMemory,
			     "currentThread",		(char*)currentThread,
			     "nativeInit",		(char*)nativeInit,
			     "start",			(char*)start,
			     "sleep",			(char*)jamSleep,
			     "nativeInterrupt",		(char*)interrupt,
			     "isAlive",			(char*)isAlive,
			     "yield",			(char*)yield,
			     "isInterrupted",		(char*)isInterrupted,
			     "interrupted",		(char*)interrupted,
			     "nativeSetPriority",	(char*)nativeSetPriority,
                             NULL,			NULL};

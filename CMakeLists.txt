cmake_minimum_required(VERSION 3.14)
project(src C)

set(CMAKE_C_STANDARD 11)

include_directories(.)
include_directories(i386)
include_directories(powerpc)

add_executable(src
        i386/dll_md.c
        i386/lock_md.h
        alloc.c
        alloc.h
        cast.c
        class.c
        dll.c
        excep.c
        execute.c
        frame.h
        hash.c
        hash.h
        interp.c
        jam.c
        jam.h
        jni.c
        jni.h
        lock.c
        lock.h
        natives.c
        reflect.c
        resolve.c
        sig.h
        string.c
        thread.c
        thread.h
        utf8.c)

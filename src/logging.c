#include "logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // exit

void die(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fputs("ERROR: ",stderr);
    vfprintf(stderr,fmt,args);
    va_end(args);
    fputc('\n',stderr);
    exit(3);
}

void warn(const char* fmt, ...) {
    va_list args;    
    va_start(args, fmt);
    fputs("WARNING: ",stderr);
    vfprintf(stderr,fmt,args);
    va_end(args);
    fputc('\n',stderr);
}

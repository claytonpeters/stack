// Includes:
#include "StackLog.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#ifndef gettid
#include <sys/syscall.h>
#endif

void stack_log(const char *format, ...)
{
	// Log time
	char buffer[80];
	time_t t;
	time(&t);
	struct tm *lt = localtime(&t);
	strftime(buffer, 80, "[%Y-%m-%d %H:%M:%S] ", lt);
#ifdef gettid
	pid_t tid = gettid();
#else
	long tid = syscall(SYS_gettid);
#endif
	fprintf(stderr, "%s(tid %ld) ", buffer, tid);

	// Log message
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

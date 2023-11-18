// Includes:
#include "StackLog.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>

void stack_log(const char *format, ...)
{
	// Log time
	char buffer[80];
	time_t t;
	time(&t);
	struct tm *lt = localtime(&t);
	strftime(buffer, 80, "[%Y-%m-%d %H:%M:%S] ", lt);
	fprintf(stderr, "%s", buffer);

	// Log message
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

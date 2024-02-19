// Includes:
#include "StackCue.h"
#include <cstring>
#include <time.h>
#include <cmath>

// Gets the current clocktime as a stack_time_t (in nanoseconds)
stack_time_t stack_get_clock_time()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((int64_t)ts.tv_sec * NANOSECS_PER_SEC) + (int64_t)ts.tv_nsec;
}

// Formats a cue id as a cue number string
void stack_cue_id_to_string(cue_id_t cue_id, char *buffer, size_t buffer_size)
{
	// Format in i.ddd format (will have trailing zeros)
	snprintf(buffer, buffer_size, "%d.%03d", cue_id / 1000, cue_id % 1000);

	// Get the length of our generated string
	size_t len = strlen(buffer);

	// Remove any extraneous zeros
	while (len > 1 && buffer[len - 1] == '0')
	{
		buffer[len - 1] = '\0';
		len--;
	}

	// Once we've removed any extraneous zeros, look for extraneous dots
	while (len > 1 && buffer[len - 1] == '.')
	{
		buffer[len - 1] = '\0';
		len--;
	}

	// Note we don't do the above as one single loop checking for either zeros
	// or dots other wise "10.00" becomes "1" rather than "10"
}

// Formats a cue id as a cue number string
cue_id_t stack_cue_string_to_id(const char *s)
{
	// Get the length of the string
	size_t len = strlen(s);

	// Find the point
	const char *dot_sep = strchr(s, '.');

	// Convert anything before the dot to an integer
	cue_id_t cue_id = ((cue_id_t)atoi(s) * 1000);

	// If we've found a dot...
	if (dot_sep != NULL)
	{
		// If we have a digit after the dot
		if ((size_t)(dot_sep - s) + 1 < len && dot_sep[1] >= '0' && dot_sep[1] <= '9')
		{
			// Add on the hundreds component
			cue_id += (dot_sep[1] - '0') * 100;

			// If we have a second digit after the dot
			if ((size_t)(dot_sep - s) + 2 < len && dot_sep[2] >= '0' && dot_sep[2] <= '9')
			{
				// Add on the tens component
				cue_id += (dot_sep[2] - '0') * 10;

				// If we have a third digit after the dot
				if ((size_t)(dot_sep - s) + 3 < len && dot_sep[3] >= '0' && dot_sep[3] <= '9')
				{
					// Add on the hundreds component
					cue_id += (dot_sep[3] - '0');

					// Ignore any extraneous digits
				}
			}
		}
	}

	return cue_id;
}
// Formats a time as a string
// @param time The time (in nanoseconds) to format
// @param str The buffer to write the string in to
// @param len The length of the buffer given by 'str'
void stack_format_time_as_string(stack_time_t time, char *str, size_t len)
{
	uint64_t time_seconds = time / NANOSECS_PER_SEC;
	snprintf(str, len, "%u:%02u.%03u", (uint32_t)(time_seconds / 60), uint32_t(time_seconds % 60), uint32_t((time % NANOSECS_PER_SEC) / NANOSECS_PER_MILLISEC));
}

// Converts a decibel value to a number that can be used as a coefficient to
// samples
// @param db The value in decibels to convert
// @returns A value between 0.0 and 1.0 if db <= 0 and a value greater than 1.0 if db >= 1
double stack_db_to_scalar(double db)
{
	return pow(10.0, db / 20.0); /* Implicit multiply by 1 */
}

// Converts a linear scalar to a number to an equivalent value in decibels
// (relative to 1 dB)
// @param db The value to convert
// @returns A value in decibels
double stack_scalar_to_db(double scalar)
{
	return 20.0 * log10(scalar); /* Implicit divide by 1 */
}

// Converts a string of the format mm:ss.ss into a stack_time_t. Also supports
// "ss.ss" (i.e. no minute part) and "ss" (i.e. just seconds)
stack_time_t stack_time_string_to_ns(const char *s)
{
	const char *min_sep = strchr(s, ':');
	const char *sec_sep = strchr(s, '.');

	// This is invalid, so return zero
	if (sec_sep == NULL && min_sep == NULL && sec_sep < min_sep)
	{
		return 0;
	}

	// No minute or second separator, assume just whole seconds
	if (min_sep == NULL && sec_sep == NULL)
	{
		return (stack_time_t)atoi(s) * NANOSECS_PER_SEC;
	}
	// No minute separator, but a second separator so assume seconds and fractional seconds
	else if (min_sep == NULL && sec_sep != NULL)
	{
		return (stack_time_t)((double)atof(s) * NANOSECS_PER_SEC_F);
	}
	// A minute separator, but no second separator so assume minutes and whole seconds
	else if (min_sep != NULL && sec_sep == NULL)
	{
		return ((stack_time_t)atoi(s) * NANOSECS_PER_SEC * 60) + ((stack_time_t)atoi(min_sep + 1) * NANOSECS_PER_SEC);
	}
	// We have both a minute separator and a second separator
	else
	{
		return ((stack_time_t)atoi(s) * NANOSECS_PER_SEC * 60) + (stack_time_t)((double)atof(min_sep + 1) * NANOSECS_PER_SEC_F);
	}
}

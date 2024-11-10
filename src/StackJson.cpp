// Includes:
#include "StackJson.h"
#include "StackLog.h"
#include <cstring>

bool stack_json_read_string(const char *json, Json::Value *value)
{
    static Json::CharReaderBuilder builder;
	static Json::CharReader *reader = NULL;
	if (reader == NULL)
	{
		reader = builder.newCharReader();
		if (reader == NULL)
		{
			stack_log("stack_json_string(): Failed to create new JSON reader\n");
			return false;
		}
	}

    return reader->parse(json, &json[strlen(json)], value, NULL);
}

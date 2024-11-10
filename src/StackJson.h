#ifndef _STACKJSON_H_INCLUDED
#define _STACKJSON_H_INCLUDED

// Includes:
#include <json/json.h>

bool stack_json_read_string(const char *json, Json::Value *value);

#endif

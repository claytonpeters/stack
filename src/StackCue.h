#ifndef _STACKCUE_H_INCLUDED
#define _STACKCUE_H_INCLUDED

// System Includes:
#include <cstdint>
#include <cstdlib>
#include <gtk/gtk.h>
#include <map>
#include <vector>
#include <string>

// Things defined in this file:
struct StackCue;
struct StackCueClass;

// Generic type for time - in nanoseconds
typedef int64_t stack_time_t;
typedef int32_t cue_id_t;
typedef uint64_t cue_uid_t;

// Defines: Constants
#define NANOSECS_PER_MINUTE ((int64_t)60000000000)
#define NANOSECS_PER_SEC ((int64_t)1000000000)
#define NANOSECS_PER_MILLISEC ((int64_t)1000000)
#define NANOSECS_PER_MICROSEC ((int64_t)1000)
#define MICROSECS_PER_SEC ((int64_t)1000000)
#define MILLISECS_PER_SEC ((int64_t)1000)
#define CENTISECS_PER_SEC ((int64_t)100)
#define NANOSECS_PER_SEC_F ((double)NANOSECS_PER_SEC)
#define NANOSECS_PER_MILLISEC_F ((double)NANOSECS_PER_MILLISEC)
#define NANOSECS_PER_MICROSEC_F ((double)NANOSECS_PER_MICROSEC)
#define MICROSECS_PER_SEC_F ((double)MICROSECS_PER_SEC)
#define MILLISECS_PER_SEC_F ((double)MILLISECS_PER_SEC)
#define CENTISECS_PER_SEC_F ((double)CENTISECS_PER_SEC)
#define STACK_CUE_UID_NONE ((cue_uid_t)0)
#define STACK_TIME_INFINITE ((stack_time_t)0x7FFFFFFFFFFFFFFF)

// Includes:
#include "StackError.h"
#include "StackRingBuffer.h"
#include "StackProperty.h"
#include "StackCueList.h"
#include "StackTrigger.h"

typedef std::map<std::string, StackProperty*> StackPropertyMap;
typedef std::vector<StackTrigger*> StackTriggerVector;
typedef std::vector<StackTrigger*>::iterator *StackTriggerVectorIter;
typedef std::map<std::string, const StackCueClass*> StackCueClassMap;

// Cue state
enum StackCueState
{
	STACK_CUE_STATE_ERROR = -1,
	STACK_CUE_STATE_STOPPED = 0,
	STACK_CUE_STATE_PAUSED = 1,
	STACK_CUE_STATE_PREPARED = 2,
	STACK_CUE_STATE_PLAYING_PRE = 3,
	STACK_CUE_STATE_PLAYING_ACTION = 4,
	STACK_CUE_STATE_PLAYING_POST = 5,
};

// Cue post-wait trigger
enum StackCueWaitTrigger
{
	STACK_CUE_WAIT_TRIGGER_NONE = 0,
	STACK_CUE_WAIT_TRIGGER_IMMEDIATE = 1,
	STACK_CUE_WAIT_TRIGGER_AFTERPRE = 2,
	STACK_CUE_WAIT_TRIGGER_AFTERACTION = 3,
};

// Base class for cues
struct StackCue
{
	// Class name
	const char *_class_name;

	// Parent cue list
	StackCueList *parent;

	// Parent cue
	StackCue *parent_cue;

	// Determines if the cue can have child cues
	bool can_have_children;

	// Cue ID (number)
	cue_id_t id;

	// Cue UID - unique id - this should be used to reference a cue
	cue_uid_t uid;

	// Cue name as displayed
	char *rendered_name;

	// The current state of the cue
	StackCueState state;

	// Runtime data: The 'clock' time of when the cue was started
	stack_time_t start_time;

	// Runtime data: The 'clock' time of when the cue was most recently paused
	stack_time_t pause_time;

	// Runtime data: The amount of time the cue has been paused for
	stack_time_t paused_time;

	// Runtime data: The amount of time the cue had been paused for when it was
	// paused (will be zero the first time the queue is paused)
	stack_time_t pause_paused_time;

	// Runtime data: Determines if the post-wait trigger has run or not
	bool post_has_run;

	// The properties for the cue (this is a std::map internally). Any
	// properties stored in here will automatically be written to JSON
	StackPropertyMap *properties;

	// The additional triggers for the cue (this is a std::vector internally)
	StackTriggerVector *triggers;
};

// Typedefs for create/delete functions
typedef StackCue*(*stack_create_cue_t)(StackCueList*);
typedef void(*stack_destroy_cue_t)(StackCue*);
typedef bool(*stack_play_cue_t)(StackCue*);
typedef void(*stack_pause_cue_t)(StackCue*);
typedef void(*stack_stop_cue_t)(StackCue*);
typedef void(*stack_pulse_cue_t)(StackCue*, stack_time_t);
typedef void(*stack_set_tabs_t)(StackCue*, GtkNotebook*);
typedef void(*stack_unset_tabs_t)(StackCue*, GtkNotebook*);
typedef char*(*stack_to_json_t)(StackCue*);
typedef void(*stack_free_json_t)(StackCue*, char*);
typedef void(*stack_from_json_t)(StackCue*, const char*);
typedef bool(*stack_cue_get_error_t)(StackCue*, char*, size_t);
typedef size_t(*stack_cue_get_active_channels_t)(StackCue*, bool*, bool);
typedef size_t(*stack_cue_get_audio_t)(StackCue*, float*, size_t);
typedef const char*(*stack_cue_get_field_t)(StackCue*, const char *);
typedef GdkPixbuf*(*stack_cue_get_icon_t)(StackCue*);
typedef StackCueStdList*(*stack_cue_get_children_t)(StackCue*);
typedef StackCue*(*stack_cue_get_next_cue_t)(StackCue*);

// Defines information about a class
struct StackCueClass
{
	const char *class_name;
	const char *super_class_name;
	const char *friendly_name;
	stack_create_cue_t create_func;
	stack_destroy_cue_t destroy_func;
	stack_play_cue_t play_func;
	stack_pause_cue_t pause_func;
	stack_stop_cue_t stop_func;
	stack_pulse_cue_t pulse_func;
	stack_set_tabs_t set_tabs_func;
	stack_unset_tabs_t unset_tabs_func;
	stack_to_json_t to_json_func;
	stack_free_json_t free_json_func;
	stack_from_json_t from_json_func;
	stack_cue_get_error_t get_error_func;
	stack_cue_get_active_channels_t get_active_channels_func;
	stack_cue_get_audio_t get_audio_func;
	stack_cue_get_field_t get_field_func;
	stack_cue_get_icon_t get_icon_func;
	stack_cue_get_children_t get_children_func;
	stack_cue_get_next_cue_t get_next_cue_func;
};

// Functions: Helpers
stack_time_t stack_get_clock_time();
void stack_format_time_as_string(stack_time_t time, char *str, size_t len)
	__attribute__((access (write_only, 2, 3)));
double stack_db_to_scalar(double db);
double stack_scalar_to_db(double db);
stack_time_t stack_time_string_to_ns(const char *s);
void stack_cue_id_to_string(cue_id_t cue_id, char *buffer, size_t buffer_size)
	__attribute__((access (write_only, 2, 3)));
cue_id_t stack_cue_string_to_id(const char *s);

// Functions: Cue Type Registration
int stack_register_cue_class(const StackCueClass *cue_class);

// Functions: Arbitrary cue creation/deletion
StackCue* stack_cue_new(const char *type, StackCueList *cue_list);
void stack_cue_destroy(StackCue *cue);

// Functions: Base cue functions
void stack_cue_initsystem();
StackCue *stack_cue_get_by_uid(cue_uid_t uid);
void stack_cue_init(StackCue *cue, StackCueList *cue_list);
void stack_cue_add_property(StackCue *cue, StackProperty *property);
void stack_cue_remove_property(StackCue *cue, const char *property);
StackProperty *stack_cue_get_property(StackCue *cue, const char *property);
void stack_cue_set_id(StackCue *cue, cue_id_t id);
void stack_cue_set_name(StackCue *cue, const char *name);
void stack_cue_set_script_ref(StackCue *cue, const char *script_ref);
void stack_cue_set_notes(StackCue *cue, const char *notes);
void stack_cue_set_pre_time(StackCue *cue, stack_time_t pre_time);
void stack_cue_set_action_time(StackCue *cue, stack_time_t action_time);
void stack_cue_set_post_time(StackCue *cue, stack_time_t post_time);
void stack_cue_set_state(StackCue *cue, StackCueState state);
void stack_cue_set_color(StackCue *cue, uint8_t r, uint8_t g, uint8_t b);
void stack_cue_set_post_trigger(StackCue *cue, StackCueWaitTrigger post_trigger);
bool stack_cue_play(StackCue *cue);
void stack_cue_pause(StackCue *cue);
void stack_cue_stop(StackCue *cue);
void stack_cue_pulse(StackCue *cue, stack_time_t clocktime);
void stack_cue_set_tabs(StackCue *cue, GtkNotebook *notebook);
void stack_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook);
void stack_cue_get_running_times(StackCue *cue, stack_time_t clocktime, stack_time_t *pre, stack_time_t *action, stack_time_t *post, stack_time_t *paused, stack_time_t *real, stack_time_t *total);
char *stack_cue_to_json(StackCue *cue);
void stack_cue_free_json(StackCue *cue, char *json_data);
void stack_cue_from_json(StackCue *cue, const char *json_data);
bool stack_cue_get_error(StackCue *cue, char *message, size_t size)
	__attribute__((access (write_only, 2, 3)));
const char* stack_cue_get_rendered_name(StackCue *cue);
size_t stack_cue_get_active_channels(StackCue *cue, bool *active, bool live);
size_t stack_cue_get_audio(StackCue *cue, float *buffer, size_t samples)
	__attribute__((access (write_only, 2, 3)));
GdkPixbuf* stack_cue_get_icon(StackCue *cue);
void stack_cue_add_trigger(StackCue *cue, StackTrigger *trigger);
void stack_cue_remove_trigger(StackCue *cue, StackTrigger *trigger);
void stack_cue_clear_triggers(StackCue *cue);
const char* stack_cue_get_field(StackCue *cue, const char *field);
StackCueStdList *stack_cue_get_children(StackCue *cue);
StackCue *stack_cue_get_next_cue(StackCue *cue);

// Base stack cue operations. These should not be called directly except from
// within subclasses of StackCue
StackCue *stack_cue_create_base(StackCueList *cue_list);
void stack_cue_destroy_base(StackCue *cue);
bool stack_cue_play_base(StackCue *cue);
void stack_cue_pause_base(StackCue *cue);
void stack_cue_stop_base(StackCue *cue);
void stack_cue_pulse_base(StackCue *cue, stack_time_t clocktime);
void stack_cue_set_tabs_base(StackCue *cue, GtkNotebook *notebook);
void stack_cue_unset_tabs_base(StackCue *cue, GtkNotebook *notebook);
char *stack_cue_to_json_base(StackCue *cue);
void stack_cue_free_json_base(StackCue *cue, char *json_data);
void stack_cue_from_json_base(StackCue *cue, const char *json_data);
bool stack_cue_get_error_base(StackCue *cue, char *message, size_t size)
	__attribute__((access (write_only, 2, 3)));
size_t stack_cue_get_active_channels_base(StackCue *cue, bool *active, bool live);
size_t stack_cue_get_audio_base(StackCue *cue, float *buffer, size_t samples)
	__attribute__((access (write_only, 2, 3)));
const char* stack_cue_get_field_base(StackCue *cue, const char *field);
GdkPixbuf* stack_cue_get_icon_base(StackCue *cue);
StackCueStdList *stack_cue_get_children_base(StackCue *cue);
StackCue *stack_cue_get_next_cue_base(StackCue *cue);

// Class functions
const StackCueClassMap *stack_cue_class_map_get();

// Defines:
#define STACK_CUE(_c) ((StackCue*)(_c))

#endif

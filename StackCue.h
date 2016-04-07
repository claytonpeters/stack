#ifndef _STACKCUE_H_INCLUDED
#define _STACKCUE_H_INCLUDED

// Includes:
#include "StackError.h"
#include "StackAudioDevice.h"
#include <gtk/gtk.h>
#include <cstdint>
#include <cstdlib>
#include <mutex>

// Generic type for time - in nanoseconds
typedef int64_t stack_time_t;
typedef int32_t cue_id_t;
typedef uint64_t cue_uid_t;

// Defines: Constants
#define NANOSECS_PER_SEC ((int64_t)1000000000)
#define CENTISECS_PER_SEC ((int64_t)10000000)
#define MILLISECS_PER_SEC ((int64_t)1000000)
#define NANOSECS_PER_SEC_F ((double)NANOSECS_PER_SEC)
#define CENTISECS_PER_SEC_F ((double)CENTISECS_PER_SEC)
#define MILLISECS_PER_SEC_F ((double)MILLISECS_PER_SEC)
#define STACK_CUE_UID_NONE ((cue_uid_t)0)

// Cue state
typedef enum StackCueState
{
	STACK_CUE_STATE_ERROR = -1,
	STACK_CUE_STATE_STOPPED = 0,
	STACK_CUE_STATE_PAUSED = 1,
	STACK_CUE_STATE_PREPARED = 2,
	STACK_CUE_STATE_PLAYING_PRE = 3,
	STACK_CUE_STATE_PLAYING_ACTION = 4,
	STACK_CUE_STATE_PLAYING_POST = 5,
} StackCueState;

// Cue post-wait trigger
typedef enum StackCueWaitTrigger
{
	STACK_CUE_WAIT_TRIGGER_NONE = 0,
	STACK_CUE_WAIT_TRIGGER_IMMEDIATE = 1,
	STACK_CUE_WAIT_TRIGGER_AFTERPRE = 2,
	STACK_CUE_WAIT_TRIGGER_AFTERACTION = 3,
} StackCuePostWaitTrigger;

// Cue list
typedef struct StackCueList
{
	// The array of cues (this is a std::list internally)
	void *cues;
		
	// Channels - the number of channels configured for playback
	uint16_t channels;

	// The audio device to play out of (in the future we'd have many of these 
	// and a map between virtual channels and device channels)
	StackAudioDevice *audio_device;
	
	// Buffered audio data (ring-buffer)
	float* buffer;

	// Buffer size - the maximum number of samples to keep in the buffer (per channel)
	size_t buffer_len;

	// Index of the current start of our audio_data ring buffer
	size_t buffer_idx;
	
	// The time at the start of the data
	stack_time_t buffer_time;
	
	// Mutex lock
	std::mutex lock;
	
	// Cue UID remapping (this is a std::map internally)
	void *uid_remap;
} StackCueList;

// Base class for cues
typedef struct StackCue
{
	// Class name
	const char *_class_name;
	
	// Parent cue list
	StackCueList *parent;
	
	// Determines if the cue can have child cues
	bool can_have_children;
	
	// Cue ID (number)
	cue_id_t id;
	
	// Cue UID - unique id - this should be used to reference a cue
	cue_uid_t uid;
	
	// Cue name - displayed in the list
	char *name;
	
	// Notes - displayed to the user when waiting to start the cue
	char *notes;
	
	// Cue pre-wait time. Action will be performed this much time after cue is started
	stack_time_t pre_time;
	
	// Cue in-action time. Amount of time the cue actually spends doing
	// something
	stack_time_t action_time;
	
	// Cure post-wait time. Auto follows will be performed after this much 
	// time after the cue action finished
	stack_time_t post_time;
	
	// The current state of the cue
	StackCueState state;
	
	// The color of the cue
	uint8_t r, g, b;
	
	// The postwait trigger
	StackCueWaitTrigger post_trigger;
	
	// Runtime data: The 'clock' time of when the cue was started
	stack_time_t start_time;
	
	// Runtime data: The 'clock' time of when the cue was most recently paused
	stack_time_t pause_time;
	
	// Runtime data: The amount of time the cue has been paused for
	stack_time_t paused_time;
	
	// Runtime data: The amount of time the cue had been paused for when it was
	// paused (will be zero the first time the queue is paused)
	stack_time_t pause_paused_time;
} StackCue;

typedef struct StackGroupCue
{
	// Super class
	StackCue super;
	
	// The array of cues (this is a std::list internally)
	void *cues;
} StackGroupCue;

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
typedef void(*stack_free_json_t)(char*);
typedef void(*stack_from_json_t)(StackCue*, const char*);

// Defines information about a class
typedef struct StackCueClass
{
	const char *class_name;
	const char *super_class_name;
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
} StackCueClass;

// Functions: Helpers
stack_time_t stack_get_clock_time();
void stack_format_time_as_string(stack_time_t time, char *str, size_t len);
double stack_db_to_scalar(double db);
double stack_scalar_to_db(double db);
stack_time_t stack_time_string_to_ns(const char *s);
void stack_cue_id_to_string(cue_id_t cue_id, char *buffer, size_t buffer_size);
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
void stack_cue_set_id(StackCue *cue, cue_id_t id);
void stack_cue_set_name(StackCue *cue, const char *name);
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
void stack_cue_free_json(char *json_data);
void stack_cue_from_json(StackCue *cue, const char *json_data);

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
void stack_cue_free_json_base(char *json_data);
void stack_cue_from_json_base(StackCue *cue, const char *json_data);

// Functions: Cue list count
StackCueList *stack_cue_list_new(uint16_t channels);
StackCueList *stack_cue_list_new_from_file(const char *uri);
bool stack_cue_list_save(StackCueList *cue_list, const char *uri);
StackCueList *stack_cue_list_new_from_file(const char *uri);
void stack_cue_list_destroy(StackCueList *cue_list);
size_t stack_cue_list_count(StackCueList *cue_list);
void stack_cue_list_append(StackCueList *cue_list, StackCue *cue);
void *stack_cue_list_iter_front(StackCueList *cue_list);
void *stack_cue_list_iter_next(void *iter);
StackCue *stack_cue_list_iter_get(void *iter);
void stack_cue_list_iter_free(void *iter);
bool stack_cue_list_iter_at_end(StackCueList *cue_list, void *iter);
void stack_cue_list_pulse(StackCueList *cue_list);
size_t stack_cue_list_write_audio(StackCueList *cue_list, size_t write_ptr, float *data, uint16_t channels, size_t samples, bool interleaved);
void stack_cue_list_lock(StackCueList *cue_list);
void stack_cue_list_unlock(StackCueList *cue_list);
cue_uid_t stack_cue_list_remap(StackCueList *cue_list, cue_uid_t old_uid);

// Defines:
#define STACK_CUE(_c) ((StackCue*)(_c))
#define STACK_GROUP_CUE(_c) ((StackGroupCue*)(_c))
#define STACK_CUE_LIST(_c) ((StackCueList*)(_c))

#endif


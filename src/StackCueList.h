#ifndef _STACKCUELIST_H_INCLUDED
#define _STACKCUELIST_H_INCLUDED

// Things defined in this fine
struct StackCueList;

// Typedefs:
typedef void(*state_changed_t)(StackCueList*, StackCue*, void*);
typedef void(*stack_cue_list_load_callback_t)(StackCueList*, double, const char*, void*);

// Includes
#include "StackCue.h"
#include "StackAudioDevice.h"
#include "StackRPCSocket.h"
#include <mutex>
#include <thread>
#include <cstdint>

struct StackChannelRMSData
{
	float current_level;
	float peak_level;
	stack_time_t peak_time;
	bool clipped;
};

// Cue list
struct StackCueList
{
	// The array of cues (this is a std::list internally)
	void *cues;

	// Channels - the number of channels configured for playback
	uint16_t channels;

	// The audio device to play out of (in the future we'd have many of these
	// and a map between virtual channels and device channels)
	StackAudioDevice *audio_device;

	// The thread which handles pulsing the cue list
	std::thread pulse_thread;
	bool kill_thread;

	// Mutex lock
	std::mutex lock;

	// Cue UID remapping (this is a std::map internally). Used during loading.
	void *uid_remap;

	// Changed since we were initialised?
	bool changed;

	// Ring buffers for audio
	StackRingBuffer **buffers;

	// The URI of the currently loaded cue list (may be NULL)
	char* uri;

	// The show name
	char* show_name;

	// The show designer
	char* show_designer;

	// The show revision
	char* show_revision;

	// Function to call on state change
	state_changed_t state_change_func;
	void* state_change_func_data;

	// Audio RMS data (this a std::map internally)
	void *rms_data;
	StackChannelRMSData *master_rms_data;

	// Cache
	bool *active_channels_cache;
	float *rms_cache;

#if HAVE_LIBPROTOBUF_C == 1
	// Remote control for the cue list
	StackRPCSocket *rpc_socket;
#endif
};

// Functions: Cue list count
StackCueList *stack_cue_list_new(uint16_t channels);
StackCueList *stack_cue_list_new_from_file(const char *uri, stack_cue_list_load_callback_t callback = NULL, void *callback_user_data = NULL);
bool stack_cue_list_save(StackCueList *cue_list, const char *uri);
void stack_cue_list_destroy(StackCueList *cue_list);
void stack_cue_list_set_audio_device(StackCueList *cue_list, StackAudioDevice *audio_device);
size_t stack_cue_list_count(StackCueList *cue_list);
void stack_cue_list_append(StackCueList *cue_list, StackCue *cue);
void *stack_cue_list_iter_front(StackCueList *cue_list);
void *stack_cue_list_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index);
void *stack_cue_list_iter_next(void *iter);
void *stack_cue_list_iter_prev(void *iter);
StackCue *stack_cue_list_iter_get(void *iter);
void stack_cue_list_iter_free(void *iter);
bool stack_cue_list_iter_at_end(StackCueList *cue_list, void *iter);
void stack_cue_list_pulse(StackCueList *cue_list);
void stack_cue_list_lock(StackCueList *cue_list);
void stack_cue_list_unlock(StackCueList *cue_list);
void stack_cue_list_stop_all(StackCueList *cue_list);
cue_uid_t stack_cue_list_remap(StackCueList *cue_list, cue_uid_t old_uid);
void stack_cue_list_changed(StackCueList *cue_list, StackCue *cue, StackProperty *property);
void stack_cue_list_state_changed(StackCueList *cue_list, StackCue *cue);
void stack_cue_list_remove(StackCueList *cue_list, StackCue *cue);
void stack_cue_list_move(StackCueList *cue_list, StackCue *cue, size_t index);
StackCue *stack_cue_list_get_cue_after(StackCueList *cue_list, StackCue *cue);
StackCue *stack_cue_list_get_cue_by_uid(StackCueList *cue_list, cue_uid_t uid);
StackCue *stack_cue_list_get_cue_by_index(StackCueList *cue_list, size_t index);
cue_id_t stack_cue_list_get_next_cue_number(StackCueList *cue_list);
const char *stack_cue_list_get_show_name(StackCueList *cue_list);
const char *stack_cue_list_get_show_designer(StackCueList *cue_list);
const char *stack_cue_list_get_show_revision(StackCueList *cue_list);
bool stack_cue_list_set_show_name(StackCueList *cue_list, const char *show_name);
bool stack_cue_list_set_show_designer(StackCueList *cue_list, const char *show_designer);
bool stack_cue_list_set_show_revision(StackCueList *cue_list, const char *show_revision);
void stack_cue_list_get_audio(StackCueList *cue_list, float *buffer, size_t samples, size_t channel_count, size_t *channels);
StackChannelRMSData *stack_cue_list_get_rms_data(StackCueList *cue_list, cue_uid_t uid);

// Defines:
#define STACK_CUE_LIST(_c) ((StackCueList*)(_c))

#endif

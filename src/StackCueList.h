#ifndef _STACKCUELIST_H_INCLUDED
#define _STACKCUELIST_H_INCLUDED

// System includes:
#include <list>
#include <map>

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

// Define StackCueStdList as a custom std::list<StackCue*> that has a custom
// recursive_iterator that descends in to child cues
class StackCueStdList : public std::list<StackCue*>
{
	public:
		class recursive_iterator
		{
			private:
				StackCueStdList &cue_list;
				StackCueStdList::iterator main_iter;
				StackCueStdList::iterator child_iter;
				bool in_child;
			public:
				recursive_iterator(const recursive_iterator& other) = default;
				recursive_iterator(StackCueStdList &cl) : cue_list(cl), main_iter(cl.begin()), child_iter(cl.end()), in_child(false) {}

				recursive_iterator begin() const
				{
					return recursive_iterator(cue_list);
				}

				recursive_iterator end() const
				{
					recursive_iterator r = recursive_iterator(cue_list);
					r.main_iter = r.cue_list.end();
					r.child_iter = r.cue_list.end();
					return r;
				}

				bool is_child() const
				{
					return in_child;
				}

				recursive_iterator& operator=(const recursive_iterator& other)
				{
					cue_list = other.cue_list;
					main_iter = other.main_iter;
					child_iter = other.child_iter;
					in_child = other.in_child;

					return *this;
				}

				bool operator==(recursive_iterator other) const
				{
					return main_iter == other.main_iter && child_iter == other.child_iter && in_child == other.in_child;
				}

				bool operator!=(recursive_iterator other) const
				{
					return main_iter != other.main_iter || child_iter != other.child_iter || in_child != other.in_child;
				}

				StackCue *operator*() const
				{
					return in_child ? *child_iter : *main_iter;
				}

				recursive_iterator& leave_child(bool forward);

				// Prefix increment/decrement operators (defined in StackCueList.cpp)
				recursive_iterator& operator++();
				recursive_iterator& operator--();

				// Postfix increment/decretment operators that call the prefixed ones
				recursive_iterator operator++(int)
				{
					recursive_iterator retval = *this;
					++(*this);
					return retval;
				}

				recursive_iterator operator--(int)
				{
					recursive_iterator retval = *this;
					--(*this);
					return retval;
				}

				// Get copy of the main cue list iterator
				StackCueStdList::iterator main_iterator()
				{
					return main_iter;
				}

				// Get copy of child iterator - note that this is only valid if
				// is_child() returns true
				StackCueStdList::iterator child_iterator()
				{
					return child_iter;
				}
		};

		recursive_iterator recursive_begin()
		{
			return recursive_iterator(*this).begin();
		}

		recursive_iterator recursive_end()
		{
			return recursive_iterator(*this).end();
		}
};

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
	StackCueStdList *cues;

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

	// Cue UID remapping. Used during loading.
	std::map<cue_uid_t, cue_uid_t> *uid_remap;

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

	// Audio RMS data
	std::map<cue_uid_t, StackChannelRMSData*> *rms_data;
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
StackCue *stack_cue_list_create_cue_from_json(StackCueList *cue_list, Json::Value &json, bool construct);
StackCue *stack_cue_list_create_cue_from_json_string(StackCueList *cue_list, const char* json, bool construct);
bool stack_cue_list_save(StackCueList *cue_list, const char *uri);
void stack_cue_list_destroy(StackCueList *cue_list);
void stack_cue_list_set_audio_device(StackCueList *cue_list, StackAudioDevice *audio_device);
size_t stack_cue_list_count(StackCueList *cue_list);
void stack_cue_list_append(StackCueList *cue_list, StackCue *cue);
StackCueStdList::iterator stack_cue_list_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index);
StackCueStdList::recursive_iterator stack_cue_list_recursive_iter_at(StackCueList *cue_list, cue_uid_t cue_uid, size_t *index);
void stack_cue_list_pulse(StackCueList *cue_list);
void stack_cue_list_lock(StackCueList *cue_list);
void stack_cue_list_unlock(StackCueList *cue_list);
void stack_cue_list_stop_all(StackCueList *cue_list);
cue_uid_t stack_cue_list_remap(StackCueList *cue_list, cue_uid_t old_uid);
void stack_cue_list_changed(StackCueList *cue_list, StackCue *cue, StackProperty *property);
void stack_cue_list_state_changed(StackCueList *cue_list, StackCue *cue);
void stack_cue_list_remove(StackCueList *cue_list, StackCue *cue);
void stack_cue_list_move(StackCueList *cue_list, StackCue *cue, StackCue *dest, bool before, bool dest_in_child);
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

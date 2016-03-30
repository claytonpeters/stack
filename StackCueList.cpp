// Includes:
#include "StackCue.h"
#include <list>
#include <cstring>
using namespace std;

typedef list<StackCue*> stackcue_list_t;
typedef list<StackCue*>::iterator stackcue_list_iterator_t;

#define SCL_GET_LIST(_scl) ((stackcue_list_t*)(((StackCueList*)(_scl))->cues))

void stack_cue_list_init(StackCueList *cue_list)
{
	// Default to a two-channel set up
	cue_list->channels = 2;
	
	// Initialise a list
	cue_list->cues = (void*)new stackcue_list_t();
}

size_t stack_cue_list_count(StackCueList *cue_list)
{
	return SCL_GET_LIST(cue_list)->size();
}

void stack_cue_list_append(StackCueList *cue_list, StackCue *cue)
{
	SCL_GET_LIST(cue_list)->push_back(cue);
}

void *stack_cue_list_iter_front(StackCueList *cue_list)
{
	stackcue_list_iterator_t* result = new stackcue_list_iterator_t;
	*result	= (SCL_GET_LIST(cue_list)->begin());
	return result;
}

void *stack_cue_list_iter_next(void *iter)
{
	return (void*)&(++(*(stackcue_list_iterator_t*)(iter)));
}

StackCue *stack_cue_list_iter_get(void *iter)
{
	return *(*(stackcue_list_iterator_t*)(iter));
}

void stack_cue_list_iter_free(void *iter)
{
	delete (stackcue_list_iterator_t*)iter;
}

bool stack_cue_list_iter_at_end(StackCueList *cue_list, void *iter)
{
	return (*(stackcue_list_iterator_t*)(iter)) == SCL_GET_LIST(cue_list)->end();
}


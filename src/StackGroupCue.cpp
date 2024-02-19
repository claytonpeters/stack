// Includes:
#include "StackApp.h"
#include "StackGroupCue.h"
#include "StackLog.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cmath>
#include <json/json.h>
#include <list>

// Global: A single instance of our builder so we don't have to keep reloading
// it every time we change the selected cue
static GtkBuilder *sgc_builder = NULL;

////////////////////////////////////////////////////////////////////////////////
// CREATION AND DESTRUCTION

/// Creates a action cue
static StackCue* stack_group_cue_create(StackCueList *cue_list)
{
	// Allocate the cue
	StackGroupCue* cue = new StackGroupCue();

	// Initialise the superclass
	stack_cue_init(&cue->super, cue_list);

	// Make this class a StackGroupCue
	cue->super._class_name = "StackGroupCue";

	// Initialise our variables
	STACK_CUE(cue)->can_have_children = true;
	cue->group_tab = NULL;
	cue->cues = new StackCueStdList;

	// Initialise superclass variables
	stack_cue_set_name(STACK_CUE(cue), "Group");

	return STACK_CUE(cue);
}

/// Destroys a action cue
static void stack_group_cue_destroy(StackCue *cue)
{
	for (auto cue : *STACK_GROUP_CUE(cue)->cues)
	{
		stack_cue_destroy(cue);
	}

	delete STACK_GROUP_CUE(cue)->cues;

	// Call parent destructor
	stack_cue_destroy_base(cue);
}

////////////////////////////////////////////////////////////////////////////////
// PROPERTY SETTERS

////////////////////////////////////////////////////////////////////////////////
// UI CALLBACKS

////////////////////////////////////////////////////////////////////////////////
// BASE CUE OPERATIONS

/// Start the cue playing
static bool stack_group_cue_play(StackCue *cue)
{
	// Call the superclass
	if (!stack_cue_play_base(cue))
	{
		return false;
	}

	return true;
}

/// Update the cue based on time
static void stack_group_cue_pulse(StackCue *cue, stack_time_t clocktime)
{
	// Get the cue state before the base class potentially updates it
	StackCueState pre_pulse_state = cue->state;

	// Call superclass
	stack_cue_pulse_base(cue, clocktime);
}

/// Sets up the tabs for the action cue
static void stack_group_cue_set_tabs(StackCue *cue, GtkNotebook *notebook)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);

	// Create the tab
	GtkWidget *label = gtk_label_new("Group");

	// Load the UI
	if (sgc_builder == NULL)
	{
		sgc_builder = gtk_builder_new_from_resource("/org/stack/ui/StackGroupCue.ui");

		// Connect the signals
		gtk_builder_connect_signals(sgc_builder, NULL);
	}
	gcue->group_tab = GTK_WIDGET(gtk_builder_get_object(sgc_builder, "gcpGrid"));

	// Add an extra reference to the action tab - we're about to remove it's
	// parent and we don't want it to get garbage collected
	g_object_ref(gcue->group_tab);

	// The tab has a parent window in the UI file - unparent the tab container from it
	gtk_widget_unparent(gcue->group_tab);

	// Append the tab (and show it, because it starts off hidden...)
	gtk_notebook_append_page(notebook, gcue->group_tab, label);
	gtk_widget_show(gcue->group_tab);
}

/// Removes the properties tabs for a action cue
static void stack_group_cue_unset_tabs(StackCue *cue, GtkNotebook *notebook)
{
	// Find our media page
	gint page = gtk_notebook_page_num(notebook, STACK_GROUP_CUE(cue)->group_tab);

	// If we've found the page, remove it
	if (page >= 0)
	{
		gtk_notebook_remove_page(notebook, page);
	}

	// Remove our reference to the action tab
	g_object_unref(STACK_GROUP_CUE(cue)->group_tab);

	// Be tidy
	STACK_GROUP_CUE(cue)->group_tab = NULL;
}

/// Saves the details of this cue as JSON
static char *stack_group_cue_to_json(StackCue *cue)
{
	StackGroupCue *gcue = STACK_GROUP_CUE(cue);

	// Build JSON
	Json::Value root;

	root["cues"] = Json::Value(Json::ValueType::arrayValue);

	// Iterate over all the cues
	for (auto cue : *gcue->cues)
	{
		// Get the JSON representation of the cue
		Json::Value cue_root;
		Json::Reader reader;
		char *cue_json_data = stack_cue_to_json(cue);
		reader.parse(cue_json_data, cue_root);
		stack_cue_free_json(cue, cue_json_data);

		// Add it to the cues entry
		root["cues"].append(cue_root);
	}

	// Write out JSON string and return (to be free'd by
	// stack_fade_cue_free_json)
	Json::FastWriter writer;
	return strdup(writer.write(root).c_str());
}

/// Frees JSON strings as returned by stack_group_cue_to_json
static void stack_group_cue_free_json(StackCue *cue, char *json_data)
{
	free(json_data);
}

/// Re-initialises this cue from JSON Data
void stack_group_cue_from_json(StackCue *cue, const char *json_data)
{
	Json::Value cue_root;
	Json::Reader reader;

	// Parse JSON data
	reader.parse(json_data, json_data + strlen(json_data), cue_root, false);

	// Get the data that's pertinent to us
	if (!cue_root.isMember("StackGroupCue"))
	{
		stack_log("stack_group_cue_from_json(): Missing StackGroupCue class\n");
		return;
	}

	Json::Value& cue_data = cue_root["StackGroupCue"];

	// If we have some cues...
	if (cue_data.isMember("cues"))
	{
		Json::Value& cues_root = cue_data["cues"];

		if (cues_root.isArray())
		{
			// Iterate over the cues, creating their instances, and populating
			// just their base classes (we need to have built a UID map)
			int cue_count = 0;
			for (auto iter = cues_root.begin(); iter != cues_root.end(); ++iter)
			{
				Json::Value& cue_json = *iter;

				// Make sure we have a class parameter
				if (!cue_json.isMember("class"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_group_cue_from_json(): Cue missing 'class' parameter, skipping\n");
					continue;
				}

				// Make sure we have a base class
				if (!cue_json.isMember("StackCue"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_group_cue_from_json(): Cue missing 'StackCue' class, skipping\n");
					continue;
				}

				// Make sure we have a UID
				if (!cue_json["StackCue"].isMember("uid"))
				{
					cue_json["_skip"] = 1;
					stack_log("stack_group_cue_from_json(): Cue missing UID, skipping\n");
					continue;
				}

				// Create a new cue of the correct type
				const char *class_name = cue_json["class"].asString().c_str();
				StackCue *child_cue = stack_cue_new(class_name, cue->parent);
				if (child_cue == NULL)
				{
					stack_log("stack_group_cue_from_json(): Failed to create cue of type '%s', skipping\n", class_name);
					cue_json["_skip"] = 1;

					// TODO: It would be nice if we have some sort of "error cue" which
					// contained the JSON for the cue, so we didn't just drop cues from
					// the stack

					continue;
				}

				// Get the UID of the newly created cue and put a mapping from
				// the old UID to the new UID. Also store it in the JSON object
				// so that we can re-use it on the second loop
				(*cue->parent->uid_remap)[cue_json["StackCue"]["uid"].asUInt64()] = child_cue->uid;
				cue_json["_new_uid"] = (Json::UInt64)child_cue->uid;

				// Call base constructor
				stack_cue_from_json_base(child_cue, cue_json.toStyledString().c_str());

				// Append the cue to the child list
				child_cue->parent_cue = cue;
				STACK_GROUP_CUE(cue)->cues->push_back(child_cue);
			}

			// Iterate over the cues again calling their actual constructor
			int prepared_cues = 0;
			for (auto iter = cues_root.begin(); iter != cues_root.end(); ++iter)
			{
				Json::Value& cue_json = *iter;

				// Skip over cues we skipped because of errors last time
				if (cue_json.isMember("_skip"))
				{
					continue;
				}

				// Call the appropriate overloaded function
				stack_cue_from_json(stack_cue_get_by_uid(cue_json["_new_uid"].asUInt64()), cue_json.toStyledString().c_str());
				prepared_cues++;
			}
		}
		else
		{
			stack_log("stack_group_cue_from_json(): 'cues' is not an array\n");
		}
	}
	else
	{
		stack_log("stack_group_cue_from_json(): Missing 'cues' option\n");
	}
}

/// Gets the error message for the cue
void stack_group_cue_get_error(StackCue *cue, char *message, size_t size)
{
	strncpy(message, "", size);
}

/// Returns the icon for a cue
/// @param cue The cue to get the icon of
GdkPixbuf *stack_group_cue_get_icon(StackCue *cue)
{
	return NULL;
}

/// Returns the list of child cues for the cue
/// @param cue The cue to get the children of
StackCueStdList *stack_group_cue_get_children(StackCue *cue)
{
	return STACK_GROUP_CUE(cue)->cues;
}

////////////////////////////////////////////////////////////////////////////////
// CLASS REGISTRATION

// Registers StackGroupCue with the application
void stack_group_cue_register()
{
	// Register built in cue types
	StackCueClass* action_cue_class = new StackCueClass{ "StackGroupCue", "StackCue", stack_group_cue_create, stack_group_cue_destroy, stack_group_cue_play, NULL, NULL, stack_group_cue_pulse, stack_group_cue_set_tabs, stack_group_cue_unset_tabs, stack_group_cue_to_json, stack_group_cue_free_json, stack_group_cue_from_json, stack_group_cue_get_error, NULL, NULL, NULL, stack_group_cue_get_icon, stack_group_cue_get_children };
	stack_register_cue_class(action_cue_class);
}

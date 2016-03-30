// Includes:
#include "StackApp.h"

// Entry point for the application
int main(int argc, char **argv)
{
	// Start the application	
	return g_application_run(G_APPLICATION(stack_app_new()), argc, argv);
}


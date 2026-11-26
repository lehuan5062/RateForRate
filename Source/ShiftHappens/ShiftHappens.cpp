// Rate for Rate - Game Off 2026

#include "ShiftHappens.h"

#include "Logging/LogCategory.h"
#include "MetasoundFrontendModuleRegistrationMacros.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogShift);

// The MetaSound nodes of this module (Audio/Nodes/): the registration list the nodes add themselves to, filled in at module start.
METASOUND_IMPLEMENT_MODULE_REGISTRATION_LIST

class FShiftHappensModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		METASOUND_REGISTER_ITEMS_IN_MODULE
	}

	virtual void ShutdownModule() override
	{
		METASOUND_UNREGISTER_ITEMS_IN_MODULE
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FShiftHappensModule, ShiftHappens, "RateForRate");   // the module keeps its code name; the third argument is the game (project) name

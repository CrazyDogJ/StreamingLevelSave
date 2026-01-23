#include "StreamingLevelSaveEditor.h"

#include "ISettingsModule.h"
#include "StreamingLevelSaveSettings.h"

#define LOCTEXT_NAMESPACE "FStreamingLevelSaveEditorModule"

void FStreamingLevelSaveEditorModule::StartupModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Streaming Level Save Game",
			LOCTEXT("RuntimeSettingsName", "Streaming Level Save Game"),
			LOCTEXT("RuntimeSettingsDescription", "Configure Streaming Level Save Game settings"),
			GetMutableDefault<UStreamingLevelSaveSettings>());
	}
}

void FStreamingLevelSaveEditorModule::ShutdownModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Streaming Level Save Game");
	}
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FStreamingLevelSaveEditorModule, StreamingLevelSaveEditor)
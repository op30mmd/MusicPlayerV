#include "..\..\ScriptHookV_SDK\inc\main.h"
#include "keyboard.h"
#include "script.h"
#include "log.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		LogMessage(L"DllMain: DLL_PROCESS_ATTACH module=%p", hInstance);
		scriptRegister(hInstance, ScriptMain);
		keyboardHandlerRegister(OnKeyboardMessage);
		LogMessage(L"DllMain: script + keyboard registered");
		break;
	case DLL_PROCESS_DETACH:
		scriptUnregister(hInstance);
		keyboardHandlerUnregister(OnKeyboardMessage);
		LogMessage(L"DllMain: DLL_PROCESS_DETACH");
		break;
	}
	return TRUE;
}

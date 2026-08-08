#include "ui.h"
#include "log.h"

// All native hashes verified against ScriptHookV_SDK\inc\natives.h

static bool g_drawTextLogged = false;
static bool g_drawRectLogged = false;

void UI::DrawText(float x, float y, const wchar_t* text, float scale, int r, int g, int b, int a)
{
	if (!g_drawTextLogged)
	{
		g_drawTextLogged = true;
		LogMessage(L"DrawText: first call x=%f y=%f scale=%f text=%s", x, y, scale, text);
	}

	nativeInit(0x66E0276CC5F6B9DA); // SET_TEXT_FONT
	nativePush64(0);
	nativeCall();

	float zero = 0.0f;
	nativeInit(0x07C837F9A01C34C9); // SET_TEXT_SCALE
	nativePush64(*(UINT64*)&zero);
	nativePush64(*(UINT64*)&scale);
	nativeCall();

	nativeInit(0xBE6B23FFA53FB442); // SET_TEXT_COLOUR
	nativePush64((UINT64)r);
	nativePush64((UINT64)g);
	nativePush64((UINT64)b);
	nativePush64((UINT64)a);
	nativeCall();

	nativeInit(0x1CA3E9EAC9D93E5E); // SET_TEXT_DROP_SHADOW
	nativeCall();

	nativeInit(0xC02F4DBFB51D988B); // SET_TEXT_CENTRE
	nativePush64(0);
	nativeCall();

	nativeInit(0x25FBB336DF1804CB); // _SET_TEXT_ENTRY
	nativePush64((UINT64)"STRING");
	nativeCall();

	char narrowText[512];
	int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, narrowText, 511, nullptr, nullptr);
	if (len < 0) len = 0;
	narrowText[len] = '\0';

	nativeInit(0x6C188BE134E074AA); // _ADD_TEXT_COMPONENT_STRING
	nativePush64((UINT64)narrowText);
	nativeCall();

	nativeInit(0xCD015E5BB0D96A57); // _DRAW_TEXT
	nativePush64(*(UINT64*)&x);
	nativePush64(*(UINT64*)&y);
	nativeCall();
}

void UI::DrawNotification(const wchar_t* text)
{
	nativeInit(0x202709F4C58A0424); // _SET_NOTIFICATION_TEXT_ENTRY
	nativePush64((UINT64)"STRING");
	nativeCall();

	char narrowText[512];
	int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, narrowText, 511, nullptr, nullptr);
	if (len < 0) len = 0;
	narrowText[len] = '\0';

	nativeInit(0x6C188BE134E074AA); // _ADD_TEXT_COMPONENT_STRING
	nativePush64((UINT64)narrowText);
	nativeCall();

	nativeInit(0x2ED7843F8F801023); // _DRAW_NOTIFICATION
	nativePush64(1);
	nativePush64(1);
	nativeCall();
}

void UI::DrawRect(float x, float y, float width, float height, int r, int g, int b, int a)
{
	if (!g_drawRectLogged)
	{
		g_drawRectLogged = true;
		LogMessage(L"DrawRect: first call x=%f y=%f w=%f h=%f", x, y, width, height);
	}

	// DRAW_RECT takes CENTER coords; x/y here are top-left
	float cx = x + width * 0.5f;
	float cy = y + height * 0.5f;

	nativeInit(0x3A618A217E5154F0); // DRAW_RECT
	nativePush64(*(UINT64*)&cx);
	nativePush64(*(UINT64*)&cy);
	nativePush64(*(UINT64*)&width);
	nativePush64(*(UINT64*)&height);
	nativePush64((UINT64)r);
	nativePush64((UINT64)g);
	nativePush64((UINT64)b);
	nativePush64((UINT64)a);
	nativeCall();
}

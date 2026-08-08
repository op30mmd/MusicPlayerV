#pragma once

#include "..\..\ScriptHookV_SDK\inc\main.h"
#include <string>

namespace UI
{
	void DrawText(float x, float y, const wchar_t* text, float scale = 0.3f, int r = 255, int g = 255, int b = 255, int a = 255);
	void DrawNotification(const wchar_t* text);
	void DrawRect(float x, float y, float width, float height, int r, int g, int b, int a);
}

#pragma once

#include <windows.h>

#include <string>

#include "core/types.h"

namespace ui {

const wchar_t* MainWindowClassName();
bool CreateMainWindow(HINSTANCE instance, int showCommand, const rack::State* initialState);
int RunMessageLoop();

}  // namespace ui

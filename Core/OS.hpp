#pragma once

// Common OS includes
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>

#if defined(__EDG__)
#define XOR_INTELLISENSE
#endif

using Microsoft::WRL::ComPtr;

#pragma warning(disable: 4180) // warning C4180: qualifier applied to function type has no meaning; ignored)

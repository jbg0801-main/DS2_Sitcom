#include "dinput8_proxy.h"

#include "log.h"

#include <string>

DirectInput8Create_t g_original_direct_input8_create = nullptr;

bool LoadRealDinput8(HMODULE* out_module, DirectInput8Create_t* out_create) {
  wchar_t sys[MAX_PATH];
  if (!GetSystemDirectoryW(sys, MAX_PATH)) {
    return false;
  }
  std::wstring path(sys);
  path += L"\\dinput8.dll";

  HMODULE mod = LoadLibraryW(path.c_str());
  if (!mod) {
    sitcom::LogWrite("proxy: failed to load system dinput8.dll");
    return false;
  }

  auto fn = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(mod, "DirectInput8Create"));
  if (!fn) {
    sitcom::LogWrite("proxy: DirectInput8Create missing");
    FreeLibrary(mod);
    return false;
  }

  *out_module = mod;
  *out_create = fn;
  g_original_direct_input8_create = fn;
  sitcom::LogWrite("proxy: system dinput8 loaded");
  return true;
}

void UnloadRealDinput8(HMODULE module) {
  g_original_direct_input8_create = nullptr;
  if (module) {
    FreeLibrary(module);
  }
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion,
                                                                   REFIID riidltf, LPVOID* ppvOut,
                                                                   LPUNKNOWN punkOuter) {
  if (g_original_direct_input8_create) {
    return g_original_direct_input8_create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
  }
  return E_FAIL;
}

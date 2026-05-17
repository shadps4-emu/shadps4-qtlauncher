// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bitlocker.h"

#include <format>
#include <string>

static HMODULE hFveApi = nullptr;

bool FveInit() {
    hFveApi = LoadLibraryW(L"fveapi.dll");
    if (!hFveApi) {
        return false;
    }

    pOpenVolume = (_FveOpenVolumeW)GetProcAddress(hFveApi, "FveOpenVolumeW");
    pCloseVolume = (_FveCloseVolume)GetProcAddress(hFveApi, "FveCloseVolume");
    pAuthFromPass =
        (_FveAuthElementFromPassPhraseW)GetProcAddress(hFveApi, "FveAuthElementFromPassPhraseW");
    pUnlock = (_FveUnlockVolume)GetProcAddress(hFveApi, "FveUnlockVolume");
    pGetStatus = (_FveGetStatus)GetProcAddress(hFveApi, "FveGetStatus");

    return pOpenVolume && pCloseVolume && pAuthFromPass && pUnlock && pGetStatus;
}

void FveCleanup() {
    if (hFveApi) {
        FreeLibrary(hFveApi);
    };
}

static HANDLE OpenVolume(PCWSTR drive) {
    std::wstring path = std::format(LR"(\\.\{}:)", drive);
    HANDLE handle = INVALID_HANDLE_VALUE;
    HRESULT hr = pOpenVolume(path.c_str(), FALSE, &handle);
    return handle;
}

bool FveIsLocked(PCWSTR drive) {
    HANDLE handle = OpenVolume(drive);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    BYTE buffer[0x78] = {};
    ULONG* pStatus = reinterpret_cast<ULONG*>(buffer);
    pStatus[0] = 0x78;
    pStatus[1] = 8;

    HRESULT hr = pGetStatus(handle, reinterpret_cast<FVE_STATUS*>(buffer));
    if (FAILED(hr)) {
        return false;
    }

    pCloseVolume(handle);
    return pStatus[3] >> 0xb & 1;
}

HRESULT FveUnlock(PCWSTR drive, PCWSTR passphrase) {
    HANDLE handle = OpenVolume(drive);
    if (handle == INVALID_HANDLE_VALUE) {
        return E_HANDLE;
    }

    FVE_AUTH_ELEMENT element = {};
    element.StructureSize = sizeof(FVE_AUTH_ELEMENT);
    element.StructureVersion = 1;

    HRESULT hr = pAuthFromPass(passphrase, &element);
    if (SUCCEEDED(hr)) {
        PFVE_AUTH_ELEMENT pElement = &element;
        FVE_AUTH_INFORMATION info = {};
        info.StructureSize = sizeof(FVE_AUTH_INFORMATION);
        info.StructureVersion = 1;
        info.ElementsCount = 1;
        info.Elements = &pElement;
        hr = pUnlock(handle, &info);
    }

    pCloseVolume(handle);
    return hr;
}
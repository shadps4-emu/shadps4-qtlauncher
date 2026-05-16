// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

typedef struct {
    WCHAR ClearPassPhrase[257];
    BYTE HashedPassPhrase[32];
    BYTE Salt[16];
} FVE_AUTH_PASSPHRASE;

typedef struct {
    ULONG StructureSize;
    ULONG StructureVersion;
    ULONG ElementFlags;
    ULONG ElementType;
    union {
        BYTE Nothing[1];
        FVE_AUTH_PASSPHRASE PassPhrase;
    } Data;
} FVE_AUTH_ELEMENT, *PFVE_AUTH_ELEMENT;

typedef struct {
    ULONG StructureSize;
    ULONG StructureVersion;
    ULONG AuthFlags;
    ULONG ElementsCount;
    PFVE_AUTH_ELEMENT* Elements;
    PCWSTR Description;
    FILETIME CreationTime;
    GUID Identifier;
} FVE_AUTH_INFORMATION;

typedef struct _FVE_STATUS_V8 {
    ULONG StructureSize;
    ULONG StructureVersion;
    USHORT FveVersion;
    ULONG Flags;
} FVE_STATUS_V8;
typedef FVE_STATUS_V8 FVE_STATUS;

typedef HRESULT(NTAPI* _FveOpenVolumeW)(PCWSTR, BOOL, HANDLE*);
typedef HRESULT(NTAPI* _FveCloseVolume)(HANDLE);
typedef HRESULT(NTAPI* _FveAuthElementFromPassPhraseW)(PCWSTR, PFVE_AUTH_ELEMENT);
typedef HRESULT(NTAPI* _FveUnlockVolume)(HANDLE, const FVE_AUTH_INFORMATION*);
typedef HRESULT(NTAPI* _FveGetStatus)(HANDLE, FVE_STATUS*);

static _FveOpenVolumeW pOpenVolume;
static _FveCloseVolume pCloseVolume;
static _FveAuthElementFromPassPhraseW pAuthFromPass;
static _FveUnlockVolume pUnlock;
static _FveGetStatus pGetStatus;

bool FveInit();
void FveCleanup();
bool FveIsLocked(PCWSTR drive);
HRESULT FveUnlock(PCWSTR drive, PCWSTR passphrase);
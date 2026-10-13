// SPDX-License-Identifier: MIT
// Missing Vista+ print-property declarations for the trusted MinGW SDK.
// ABI: Microsoft win32metadata generation/WinSDK/RecompiledIdlHeaders/um/winspool.h
// Included only after a compiler probe confirms the SDK lacks these types.
#pragma once
#include <windows.h>
#include <winspool.h>
typedef enum {
    kPropertyTypeString=1,kPropertyTypeInt32,kPropertyTypeInt64,kPropertyTypeByte,
    kPropertyTypeTime,kPropertyTypeDevMode,kPropertyTypeSD,kPropertyTypeNotificationReply,
    kPropertyTypeNotificationOptions,kPropertyTypeBuffer
} EPrintPropertyType;
typedef struct {
    EPrintPropertyType ePropertyType;
    union {
        BYTE propertyByte;
        PWSTR propertyString;
        LONG propertyInt32;
        LONGLONG propertyInt64;
        struct { DWORD cbBuf; LPVOID pBuf; } propertyBlob;
    } value;
} PrintPropertyValue;
typedef struct { WCHAR* propertyName; PrintPropertyValue propertyValue; } PrintNamedProperty;
typedef struct { ULONG numberOfProperties; PrintNamedProperty* propertiesCollection; } PrintPropertiesCollection;

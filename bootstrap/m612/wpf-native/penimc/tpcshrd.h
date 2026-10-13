/*
 * Wrapper around mingw-w64's <tpcshrd.h>: older mingw-w64 headers lack the
 * Tablet PC packet description types that WPF's PenImc uses.  The
 * definitions follow the documented Windows SDK layout.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include_next <tpcshrd.h>

#ifdef WPF_MINGW_TPCSHRD_INCOMPLETE
typedef enum _PROPERTY_UNITS
{
    PROPERTY_UNITS_DEFAULT     = 0,
    PROPERTY_UNITS_INCHES      = 1,
    PROPERTY_UNITS_CENTIMETERS = 2,
    PROPERTY_UNITS_DEGREES     = 3,
    PROPERTY_UNITS_RADIANS     = 4,
    PROPERTY_UNITS_SECONDS     = 5,
    PROPERTY_UNITS_POUNDS      = 6,
    PROPERTY_UNITS_GRAMS       = 7,
    PROPERTY_UNITS_SILINEAR    = 8,
    PROPERTY_UNITS_SIROTATION  = 9,
    PROPERTY_UNITS_ENGLINEAR   = 10,
    PROPERTY_UNITS_ENGROTATION = 11,
    PROPERTY_UNITS_SLUGS       = 12,
    PROPERTY_UNITS_KELVIN      = 13,
    PROPERTY_UNITS_FAHRENHEIT  = 14,
    PROPERTY_UNITS_AMPERE      = 15,
    PROPERTY_UNITS_CANDELA     = 16
} PROPERTY_UNITS;

typedef struct _PROPERTY_METRICS
{
    LONG nLogicalMin;
    LONG nLogicalMax;
    PROPERTY_UNITS Units;
    FLOAT fResolution;
} PROPERTY_METRICS, *PPROPERTY_METRICS;

typedef struct _PACKET_PROPERTY
{
    GUID guid;
    PROPERTY_METRICS PropertyMetrics;
} PACKET_PROPERTY, *PPACKET_PROPERTY;

typedef struct _PACKET_DESCRIPTION
{
    ULONG cbPacketSize;
    ULONG cPacketProperties;
    PACKET_PROPERTY *pPacketProperties;
    ULONG cButtons;
    GUID *pguidButtons;
} PACKET_DESCRIPTION, *PPACKET_DESCRIPTION;

typedef struct tagSYSTEM_EVENT_DATA
{
    BYTE bModifier;
    WCHAR wKey;
    LONG xPos;
    LONG yPos;
    BYTE bCursorMode;
    DWORD dwButtonState;
} SYSTEM_EVENT_DATA;
#endif /* WPF_MINGW_TPCSHRD_INCOMPLETE */

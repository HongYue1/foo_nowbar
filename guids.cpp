#include "pch.h"
#include "guids.h"

// Authoritative definitions — exactly one TU defines these.
// All other TUs see the extern declarations in guids.h.
//
// NOTE: These GUIDs must never change once the component has been distributed.
// foobar2000 and Columns UI store panel layouts by GUID; changing them causes
// existing saved layouts to lose the panel silently.

// {F8E2B456-1234-5678-9ABC-DEF012345678}
const GUID g_component_guid =
    { 0xf8e2b456, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };

// {A1B2C3D4-5E6F-7890-ABCD-EF1234567890}
const GUID g_cui_panel_guid =
    { 0xa1b2c3d4, 0x5e6f, 0x7890, { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90 } };

// {B2C3D4E5-6F78-90AB-CDEF-234567890ABC}
const GUID g_dui_element_guid =
    { 0xb2c3d4e5, 0x6f78, 0x90ab, { 0xcd, 0xef, 0x23, 0x45, 0x67, 0x89, 0x0a, 0xbc } };

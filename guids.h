#pragma once

// GUIDs are defined once in guids.cpp to avoid multiple-definition issues.
// All translation units share the same definition via these extern declarations.
//
// IMPORTANT: These GUIDs are fixed for the lifetime of the component.
// foobar2000 and Columns UI store panel layouts by GUID — changing them
// silently breaks any existing saved layout that references this panel.

// {F8E2B456-1234-5678-9ABC-DEF012345678}  foo_nowbar component GUID
extern const GUID g_component_guid;

// {A1B2C3D4-5E6F-7890-ABCD-EF1234567890}  CUI panel GUID
extern const GUID g_cui_panel_guid;

// {B2C3D4E5-6F78-90AB-CDEF-234567890ABC}  DUI element GUID
extern const GUID g_dui_element_guid;

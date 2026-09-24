#pragma once

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

// One macro pair per artifact in this repository.  CommonLibF4 owns the plugin
// metadata translation unit, so both guards export the same two entry points
// under names the loader looks up by string.
#if defined(FALLOUTVR)
#define GUARD_PLUGIN_QUERY(...) extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(__VA_ARGS__)
#define GUARD_PLUGIN_LOAD(...) extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(__VA_ARGS__)
#else
#if defined(F4SE_PLUGIN_QUERY)
#define GUARD_PLUGIN_QUERY(...) F4SE_PLUGIN_QUERY(__VA_ARGS__)
#else
#define GUARD_PLUGIN_QUERY(...) extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(__VA_ARGS__)
#endif
#define GUARD_PLUGIN_LOAD(...) F4SE_PLUGIN_LOAD(__VA_ARGS__)
#endif

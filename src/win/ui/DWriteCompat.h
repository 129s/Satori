#pragma once

#include <dwrite.h>

#ifndef SATORI_HAS_DWRITE2
#define SATORI_HAS_DWRITE2 0
#endif

#ifndef SATORI_HAS_DWRITE3
#define SATORI_HAS_DWRITE3 0
#endif

#if SATORI_HAS_DWRITE2
#include <dwrite_2.h>
#endif

#if SATORI_HAS_DWRITE3
#include <dwrite_3.h>
#endif

#if !defined(SATORI_DWRITE_FONT_COLLECTION_TYPE)
#if SATORI_HAS_DWRITE2
#define SATORI_DWRITE_FONT_COLLECTION_TYPE IDWriteFontCollection1
#else
#define SATORI_DWRITE_FONT_COLLECTION_TYPE IDWriteFontCollection
#endif
#endif

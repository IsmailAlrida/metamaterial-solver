#pragma once

// GLVis only reaches Fontconfig when no explicit font path was supplied. The
// embedded panel always supplies one, so these inline declarations keep that
// optional desktop dependency out of the application build.

using FcChar8 = unsigned char;
using FcBool = int;

enum FcResult {
    FcResultMatch
};

struct FcPattern {};
struct FcObjectSet {};
struct FcFontSet {
    int nfont = 0;
    FcPattern** fonts = nullptr;
};

#define FC_FAMILY "family"
#define FC_STYLE "style"
#define FC_FILE "file"
#define FC_SCALABLE "scalable"
#define FC_INDEX "index"
#define FC_WEIGHT "weight"

inline FcBool FcInit() { return 0; }
inline FcObjectSet* FcObjectSetBuild(...) { return nullptr; }
inline FcPattern* FcNameParse(const FcChar8*) { return nullptr; }
inline FcFontSet* FcFontList(void*, FcPattern*, FcObjectSet*) { return nullptr; }
inline void FcPatternDestroy(FcPattern*) {}
inline FcResult FcPatternGetBool(FcPattern*, const char*, int, FcBool*) { return FcResultMatch; }
inline FcResult FcPatternGetInteger(FcPattern*, const char*, int, int*) { return FcResultMatch; }
inline FcResult FcPatternGetString(FcPattern*, const char*, int, FcChar8**) { return FcResultMatch; }
inline FcChar8* FcNameUnparse(FcPattern*) { return nullptr; }
inline void FcFontSetDestroy(FcFontSet*) {}
inline void FcObjectSetDestroy(FcObjectSet*) {}
inline void FcFini() {}

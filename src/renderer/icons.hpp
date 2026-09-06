#pragma once

#include <filesystem>

#include "imgui.h"

// Material Symbols Rounded icon strings for ImGui.
//
// The readable names and Unicode codepoints come from Google's official
// Material Symbols codepoint list:
// https://github.com/google/material-design-icons/tree/master/variablefont
//
// ImGui does not shape OpenType ligatures, so "play_arrow" cannot become an
// icon automatically. These UTF-8 strings are the direct glyph equivalents.
// UI code only needs Icons::Play, Icons::Add, Icons::button(...), etc.
namespace Icons {

inline constexpr const char* Error         = u8"\ue000"; // error
inline constexpr const char* Warning       = u8"\ue002"; // warning
inline constexpr const char* Pause         = u8"\ue034"; // pause
inline constexpr const char* Play          = u8"\ue037"; // play_arrow
inline constexpr const char* Stop          = u8"\ue047"; // stop
inline constexpr const char* Add           = u8"\ue145"; // add
inline constexpr const char* Remove        = u8"\ue15b"; // remove
inline constexpr const char* Redo          = u8"\ue15a"; // redo
inline constexpr const char* Save          = u8"\ue161"; // save
inline constexpr const char* Undo          = u8"\ue166"; // undo
inline constexpr const char* File          = u8"\ue24d"; // insert_drive_file
inline constexpr const char* Download      = u8"\ue2c4"; // file_download
inline constexpr const char* Upload        = u8"\ue2c6"; // file_upload
inline constexpr const char* Folder        = u8"\ue2c7"; // folder
inline constexpr const char* FolderOpen    = u8"\ue2c8"; // folder_open
inline constexpr const char* Edit          = u8"\ue3c9"; // edit
inline constexpr const char* Tune          = u8"\ue429"; // tune
inline constexpr const char* ArrowBack     = u8"\ue5c4"; // arrow_back
inline constexpr const char* ArrowForward  = u8"\ue5c8"; // arrow_forward
inline constexpr const char* Check         = u8"\ue5ca"; // check
inline constexpr const char* ChevronLeft   = u8"\ue5cb"; // chevron_left
inline constexpr const char* ChevronRight  = u8"\ue5cc"; // chevron_right
inline constexpr const char* Close         = u8"\ue5cd"; // close
inline constexpr const char* ExpandLess    = u8"\ue5ce"; // expand_less
inline constexpr const char* ExpandMore    = u8"\ue5cf"; // expand_more
inline constexpr const char* Menu          = u8"\ue5d2"; // menu
inline constexpr const char* More          = u8"\ue5d4"; // more_vert
inline constexpr const char* Refresh       = u8"\ue5d5"; // refresh
inline constexpr const char* Chart         = u8"\ue6e1"; // show_chart
inline constexpr const char* Export        = u8"\ue6b8"; // ios_share
inline constexpr const char* View3D        = u8"\ue84d"; // 3d_rotation
inline constexpr const char* Success       = u8"\ue86c"; // check_circle
inline constexpr const char* Delete        = u8"\ue872"; // delete
inline constexpr const char* Help          = u8"\ue887"; // help
inline constexpr const char* Home          = u8"\ue88a"; // home
inline constexpr const char* Info          = u8"\ue88e"; // info
inline constexpr const char* Search        = u8"\ue8b6"; // search
inline constexpr const char* Settings      = u8"\ue8b8"; // settings
inline constexpr const char* Visibility    = u8"\ue8f4"; // visibility
inline constexpr const char* VisibilityOff = u8"\ue8f5"; // visibility_off

namespace Detail {

inline const ImWchar* glyphRanges()
{
    // Build the atlas range from the strings above instead of loading the
    // font's thousands of unused symbols.
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        static constexpr const char* glyphs[] = {
            Error, Warning, Pause, Play, Stop, Add, Remove, Redo, Save, Undo,
            File, Download, Upload, Folder, FolderOpen, Edit, Tune, ArrowBack,
            ArrowForward, Check, ChevronLeft, ChevronRight, Close, ExpandLess,
            ExpandMore, Menu, More, Refresh, Chart, Export, View3D, Success,
            Delete, Help, Home, Info, Search, Settings, Visibility, VisibilityOff
        };

        ImFontGlyphRangesBuilder builder;
        for (const char* glyph : glyphs) {
            builder.AddText(glyph);
        }
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

inline std::filesystem::path fontPath()
{
    // icons.hpp is in src; the checked-in font lives in the adjacent public folder.
    return std::filesystem::path(__FILE__).parent_path().parent_path()
        / "public"
        / "Material_Symbols_Rounded"
        / "static"
        / "MaterialSymbolsRounded_28pt-Medium.ttf";
}

inline ImWchar firstCodepoint(const char* text)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    if (bytes[0] < 0x80) {
        return bytes[0];
    }
    if ((bytes[0] & 0xe0) == 0xc0) {
        return static_cast<ImWchar>(
            ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f));
    }
    if ((bytes[0] & 0xf0) == 0xe0) {
        return static_cast<ImWchar>(
            ((bytes[0] & 0x0f) << 12)
            | ((bytes[1] & 0x3f) << 6)
            | (bytes[2] & 0x3f));
    }
    return 0;
}

} // namespace Detail

// Called once by Renderer::loadFonts(). UI code never needs to call this.
inline bool load(ImGuiIO& io, ImFont* targetFont, float pixelSize = 18.0f)
{
    const std::filesystem::path path = Detail::fontPath();
    if (targetFont == nullptr || !std::filesystem::exists(path)) {
        return false;
    }

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphOffset.y = 1.0f;
    config.GlyphMinAdvanceX = pixelSize;
    config.GlyphMaxAdvanceX = pixelSize;
    config.DstFont = targetFont;

    return io.Fonts->AddFontFromFileTTF(
        path.string().c_str(),
        pixelSize,
        &config,
        Detail::glyphRanges()) != nullptr;
}

inline bool button(const char* id,
                   const char* icon,
                   const char* tooltip = nullptr,
                   ImVec2 size = {})
{
    ImGui::PushID(id);
    if (size.x <= 0.0f) {
        size.x = ImGui::GetFrameHeight();
    }
    if (size.y <= 0.0f) {
        size.y = ImGui::GetFrameHeight();
    }

    const bool clicked = ImGui::Button("##icon", size);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImFontBaked* font = ImGui::GetFontBaked();
    const ImFontGlyph* glyph = font->FindGlyphNoFallback(
        Detail::firstCodepoint(icon));
    if (glyph != nullptr) {
        const ImVec2 origin(
            minimum.x + (maximum.x - minimum.x - (glyph->X1 - glyph->X0)) * 0.5f
                - glyph->X0,
            minimum.y + (maximum.y - minimum.y - (glyph->Y1 - glyph->Y0)) * 0.5f
                - glyph->Y0);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            origin,
            ImGui::GetColorU32(ImGuiCol_Text),
            icon);
    }
    if (tooltip != nullptr
        && ImGui::IsItemHovered(
            ImGuiHoveredFlags_ForTooltip
            | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return clicked;
}

} // namespace Icons

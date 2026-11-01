// SPDX-License-Identifier: Apache-2.0
// Included after TrueTypeFont. Face-tagged cache indices avoid collisions when
// a script-specific face uses the licensed Roboto face for Latin/path text.
#pragma once
namespace ure_font {
inline constexpr int fallback_tag=1<<24;
inline int character(TrueTypeFont* font,unsigned unicode) {
    const auto primary=FT_Get_Char_Index(font->face,unicode);
    if(primary || !font->fallback_face)return static_cast<int>(primary);
    const auto fallback=FT_Get_Char_Index(font->fallback_face,unicode);
    return fallback ? static_cast<int>(fallback)|fallback_tag : 0;
}
inline FT_Face face(TrueTypeFont* font,int index) {
    return index&fallback_tag ? font->fallback_face : font->face;
}
inline FT_UInt glyph(int index) { return static_cast<FT_UInt>(index&~fallback_tag); }
inline void kerning(TrueTypeFont* font,int previous,int current,FT_Vector& delta) {
    delta={0,0};
    if(!previous || !current || (previous&fallback_tag)!=(current&fallback_tag))return;
    const auto selected=face(font,current);
    if(selected && FT_HAS_KERNING(selected))FT_Get_Kerning(selected,glyph(previous),glyph(current),FT_KERNING_DEFAULT,&delta);
}
}

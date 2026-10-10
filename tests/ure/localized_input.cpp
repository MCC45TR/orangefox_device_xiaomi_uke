// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <iostream>
#include <linux/input.h>
#include <string>
#include "input.inc"
using std::string;
constexpr int KEYBOARD_BACKSPACE=8,KEYBOARD_ACTION=13,KEYBOARD_SWIPE_LEFT=21,TW_INPUT_NO_UPDATE=-1,TOUCH_RELEASE=0;
struct DataManager { static void SetValue(const string&,const string&) {} };
struct Action { int NotifyTouch(int,int,int) { return 0; } };
struct Font { void* GetResource() { return this; } };
namespace twrpTruetype {
int gr_ttf_measureEx(const char* text,void*) {
    int width=0;
    for(const auto* p=reinterpret_cast<const unsigned char*>(text);*p;++p)if((*p&0xc0)!=0x80)width+=10;
    return width;
}
}
class GUIInput {
public:
    bool HasInputFocus=true,mRendered=false,DrawCursor=true,isLocalChange=false,HasAllowed=false,HasDisabled=false;
    string mValue,displayValue,mVariable="input",AllowedList,DisabledList;
    int mCursorLocation=-1,cursorX=0,mRenderX=0,mRenderY=0,mRenderW=1000,scrollingX=0,textWidth=0;
    unsigned MinLen=0,MaxLen=128;
    Font font; Font* mFont=&font; Action* mAction=nullptr;
    void UpdateDisplayText() { displayValue=mValue; }
    void HandleTextLocation(int) {}
    void HandleCursorByText() {}
    void HandleCursorByTouch(int);
    int NotifyKey(int,bool);
    int NotifyCharInput(int);
};
#include "input-touch.inc"
#include "input-key.inc"
#include "input-edit.inc"

void text(GUIInput& input,const string& value,int cursor=-1) {
    input.mValue=value; input.displayValue=value; input.mCursorLocation=cursor;
}
bool boundary(const string& value,int cursor) {
    return cursor==-1 || (cursor>=0 && static_cast<std::size_t>(cursor)<=value.size() &&
        (static_cast<std::size_t>(cursor)==value.size() || (static_cast<unsigned char>(value[cursor])&0xc0)!=0x80));
}
int main() {
    assert(InputCharacter(0xe7)=="ç" && InputCharacter(0x131)=="ı" && InputCharacter(0xf6)=="ö" && InputCharacter(0xfc)=="ü");
    assert(InputCharacter(0x20ac)=="€" && InputCharacter(0x1f642)=="🙂");
    for(int key:{-1,0,31,127,0xd800,0xdfff,0x110000})assert(InputCharacter(key).empty());
    for(int key=32;key<127;++key)assert(InputCharacter(key)==std::string(1,static_cast<char>(key)));
    const std::string phrase="Varsayılana Döndür";
    std::string entered;
    const int letters[]{'V','a','r','s','a','y',0x131,'l','a','n','a',' ','D',0xf6,'n','d',0xfc,'r'};
    for(int key:letters)entered+=InputCharacter(key);
    assert(entered==phrase);
    const std::string mixed="aıöü€🙂"; std::string remaining=mixed;
    for(const auto& expected:{std::string("aıöü€"),std::string("aıöü"),std::string("aıö"),std::string("aı"),std::string("a"),std::string()}) {
        remaining.resize(PreviousInputCharacter(remaining,remaining.size())); assert(remaining==expected);
    }
    assert(PreviousInputCharacter("",0)==0 && PreviousInputCharacter("aı",999)==1);
    GUIInput input;
    for(int key:letters)input.NotifyCharInput(key);
    assert(input.mValue==phrase);
    text(input,"");
    const int setup_letters[]{'S','e',0xe7,'i','l','e','n',' ','k','u','l','l','a','n',0x131,'c',0x131,' ','a','l','a','n',0x131,'n',0x131,' ','s','i','l'};
    for(int key:setup_letters)input.NotifyCharInput(key);
    assert(input.mValue=="Seçilen kullanıcı alanını sil");
    text(input,"aıb"); input.NotifyKey(KEY_LEFT,true); input.NotifyKey(KEY_LEFT,true);
    assert(input.mCursorLocation==1); input.NotifyCharInput(KEYBOARD_BACKSPACE); assert(input.mValue=="ıb");
    text(input,"aıb",2); input.NotifyCharInput(KEYBOARD_BACKSPACE); assert(input.mValue=="ıb");
    text(input,"aıb",2); input.NotifyCharInput(KEYBOARD_SWIPE_LEFT); assert(input.mValue=="ıb");
    text(input,mixed);
    for(std::size_t step=0;step<6;++step) { input.NotifyKey(KEY_LEFT,true); assert(boundary(mixed,input.mCursorLocation)); }
    assert(input.mCursorLocation==0);
    for(std::size_t step=0;step<6;++step) { input.NotifyKey(KEY_RIGHT,true); assert(boundary(mixed,input.mCursorLocation)); }
    assert(input.mCursorLocation==-1);
    for(int x=0;x<=70;++x) {
        text(input,mixed); input.HandleCursorByTouch(x); assert(boundary(mixed,input.mCursorLocation));
        input.NotifyCharInput(KEYBOARD_BACKSPACE); assert(boundary(input.mValue,input.mCursorLocation));
    }
    text(input,"aı",1); input.NotifyCharInput(0xf6); assert(input.mValue=="aöı");
    input.MaxLen=4; text(input,"abc"); input.NotifyCharInput(0x131); assert(input.mValue=="abc");
    std::cout<<"Localized input: actual typing, touch/arrow cursors and deletion preserve UTF-8 boundaries; both Turkish consent phrases and length limits passed.\n";
}

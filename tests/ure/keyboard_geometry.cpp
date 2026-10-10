// Compile the production keyboard fit and hit-test functions with layout stand-ins.
#include <cassert>
#include <cstdint>
#include <iostream>
class GUIKeyboard {
public:
    enum { MAX_KEYBOARD_LAYOUTS=5, MAX_KEYBOARD_ROWS=9, MAX_KEYBOARD_KEYS=20 };
    struct Key { int key{},longpresskey{},end_x{},layout{}; };
    struct Layout { Key keys[MAX_KEYBOARD_ROWS][MAX_KEYBOARD_KEYS]{}; int row_end_y[MAX_KEYBOARD_ROWS]{}; };
    Layout layouts[MAX_KEYBOARD_LAYOUTS]{};
    int currentLayout=1,mRenderX=13,mRenderY=27,mRenderW{},mRenderH=644;
    bool IsInRegion(int x,int y) const { return x>=mRenderX && x<mRenderX+mRenderW && y>=mRenderY && y<mRenderY+mRenderH; }
    void FitToWidth(int);
    Key* HitTestKey(int,int);
};
#include "keyboard-fit.inc"
#include "keyboard-hit.inc"
int main() {
    for(int width:{540,918,1080,2136,3200})for(int scale=50;scale<=100;scale+=5) {
        GUIKeyboard keyboard; keyboard.mRenderW=width;
        for(auto& layout:keyboard.layouts)for(int row=0;row<4;++row) {
            layout.row_end_y[row]=(row+1)*160;
            for(int key=0;key<10;++key) {
                layout.keys[row][key].end_x=(key+1)*108*scale/100;
                layout.keys[row][key].key='a'+key;
                layout.keys[row][key].longpresskey='0'+key;
            }
        }
        keyboard.FitToWidth(width);
        for(int layout=0;layout<5;++layout) {
            keyboard.currentLayout=layout+1;
            for(int row=0;row<4;++row) {
                int start=0;
                for(auto& key:keyboard.layouts[layout].keys[row]) {
                    if(!key.end_x)break;
                    assert(key.end_x>start && key.end_x<=width);
                    assert(keyboard.HitTestKey(keyboard.mRenderX+start,keyboard.mRenderY+row*160)==&key);
                    assert(keyboard.HitTestKey(keyboard.mRenderX+key.end_x-1,keyboard.mRenderY+row*160+159)==&key);
                    start=key.end_x;
                }
                assert(start==width);
            }
        }
        assert(!keyboard.HitTestKey(keyboard.mRenderX-1,keyboard.mRenderY));
        assert(!keyboard.HitTestKey(keyboard.mRenderX+width,keyboard.mRenderY));
        assert(!keyboard.HitTestKey(keyboard.mRenderX,keyboard.mRenderY+640));
        const auto edge=keyboard.layouts[0].keys[0][0].end_x;
        keyboard.FitToWidth(width); assert(keyboard.layouts[0].keys[0][0].end_x==edge);
        keyboard.FitToWidth(0); assert(keyboard.layouts[0].keys[0][0].end_x==edge);
    }
    std::cout<<"Keyboard: actual fit/hit code covers every key edge in five layouts at 50–100% and tablet portrait/landscape widths.\n";
}

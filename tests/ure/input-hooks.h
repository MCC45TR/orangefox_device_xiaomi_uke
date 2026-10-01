// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <linux/input.h>
#include <set>
#include <vector>
#include <algorithm>
inline int mime=0;
#define KEYBOARD_ACTION 13
#define KEYBOARD_BACKSPACE 8
#define KEYBOARD_TAB 9
class Page { public: enum class Direction { Up=1,Down=-1 }; };
class PageManager {
public:
    inline static std::vector<int> characters,keys,moves;
    inline static int selections=0;
    static int NotifyCharInput(int c) { characters.push_back(c); return 0; }
    static int NotifyKey(int k,bool down) { keys.push_back(down ? k : -k); return 0; }
    static void MoveFocus(Page::Direction d) { moves.push_back(int(d)); }
    static void SelectFocusedElement(bool) { ++selections; }
};
class HardwareKeyboard {
public:
    HardwareKeyboard(); virtual ~HardwareKeyboard();
    int KeyDown(int); int KeyUp(int); int KeyRepeat();
    void ConsumeKeyRelease(int); void ResetPressedKeys();
    bool IsKeyDown(int) const; bool AreKeysPressed(int,int) const;
private:
    int mLastKey,mLastKeyChar;
    bool mNavigation=false;
    std::set<int> mPressedKeys,mConsumedKeys;
};

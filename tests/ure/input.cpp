// SPDX-License-Identifier: Apache-2.0
#include "input-hooks.h"
#include <cassert>
int main() {
    HardwareKeyboard keyboard;
    keyboard.KeyDown(KEY_LEFTSHIFT); keyboard.KeyDown(KEY_A); keyboard.KeyUp(KEY_A); keyboard.KeyUp(KEY_LEFTSHIFT);
    assert(PageManager::characters.back()=='A' && !keyboard.IsKeyDown(KEY_LEFTSHIFT));
    keyboard.KeyDown(KEY_A); keyboard.KeyUp(KEY_A); assert(PageManager::characters.back()=='a');
    keyboard.KeyDown(KEY_F6); keyboard.KeyDown(KEY_F6); keyboard.KeyUp(KEY_F6);
    keyboard.KeyDown(KEY_DOWN); keyboard.KeyDown(KEY_DOWN); keyboard.KeyUp(KEY_DOWN);
    assert(PageManager::moves.size()==1 && PageManager::moves.back()==-1);
    keyboard.KeyDown(KEY_LEFTSHIFT); keyboard.KeyDown(KEY_TAB); keyboard.KeyUp(KEY_TAB); keyboard.KeyUp(KEY_LEFTSHIFT);
    assert(PageManager::moves.back()==1);
    keyboard.KeyDown(KEY_ENTER); keyboard.KeyDown(KEY_ENTER); keyboard.KeyUp(KEY_ENTER);
    assert(PageManager::selections==1);
    assert(std::find(PageManager::keys.begin(),PageManager::keys.end(),-KEY_ENTER)==PageManager::keys.end());
    keyboard.KeyDown(KEY_F6); keyboard.KeyUp(KEY_F6);
    keyboard.KeyDown(KEY_TAB); keyboard.KeyUp(KEY_TAB); assert(PageManager::characters.back()==9);
    keyboard.KeyDown(KEY_ESC); keyboard.KeyUp(KEY_ESC); assert(PageManager::keys.back()==-KEY_BACK);
    keyboard.KeyDown(KEY_LEFTSHIFT); keyboard.KeyDown(KEY_LEFTCTRL); keyboard.KeyDown(KEY_A);
    const auto characters=PageManager::characters.size(),keys=PageManager::keys.size();
    keyboard.ResetPressedKeys(); keyboard.KeyRepeat();
    assert(!keyboard.IsKeyDown(KEY_LEFTSHIFT) && !keyboard.IsKeyDown(KEY_LEFTCTRL));
    assert(PageManager::characters.size()==characters && PageManager::keys.size()==keys);
    keyboard.KeyDown(KEY_B); assert(PageManager::characters.back()=='b');
}

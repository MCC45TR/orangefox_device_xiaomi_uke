// SPDX-License-Identifier: Apache-2.0
#include "events-hooks.cpp"
int main() {
    input_event event{};
    ev_init(); assert(ev_count==1 && ev_has_mouse()==1);
    queue.push_back({{},EV_REL,REL_X,123}); assert(ev_get(&event,0)==0 && event.type==EV_REL && event.value==123);
    queue.push_back({{},EV_KEY,KEY_A,1}); assert(ev_get(&event,0)==0 && event.code==KEY_A);
    devices=2; ++modified; ++now_seconds;
    assert(ev_get(&event,0)==0 && event.type==EV_SYN && event.code==SYN_DROPPED);
    assert(ev_count==2 && closes==1); // same second, changed nanoseconds
    devices=0; ++modified; ++now_seconds;
    assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED && ev_count==0 && ev_has_mouse()==0);
    const auto previous=closes; fail_stat=true; ++now_seconds;
    assert(ev_get(&event,0)==-2 && closes==previous); // failed stat never reads uninitialized timestamps
    fail_stat=false; devices=1; ++modified; ++now_seconds;
    assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED && ev_has_mouse()==1);
    hangup=true; assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED);
    assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED); // next call rescans
    queue.push_back({{},EV_SYN,SYN_DROPPED,0}); assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED);
    assert(ev_get(&event,0)==0 && event.code==SYN_DROPPED);
    devices=0; ++modified; ++now_seconds; assert(ev_get(&event,0)==0);
    devices=1; relative_mouse=false; ++modified; ++now_seconds;
    assert(ev_get(&event,0)==0 && ev_has_mouse()==0); // old mouse/ignored state not inherited on fd reuse
    devices=100; ++modified; ++now_seconds;
    assert(ev_get(&event,0)==0 && ev_count==MAX_DEVICES);
    ev_exit(); assert(ev_count==0);
}

// Isolated VM fixture only. Creates a property area; no property service or TEE.
extern "C" int __system_property_area_init();
extern "C" int __system_property_add(const char*,unsigned,const char*,unsigned);
extern "C" int printf(const char*,...);
int main() {
    const int initialized=__system_property_area_init();
    printf("URE_VM_PROPERTY_AREA_INIT_RESULT %d\n",initialized);
    if(initialized!=0)return 1;
    if(__system_property_add("ro.treble.enabled",17,"true",4)!=0) return 2;
    if(__system_property_add("sys.use_memfd",13,"true",4)!=0) return 3;
    printf("URE_VM_PROPERTY_AREA_READY\n");return 0;
}

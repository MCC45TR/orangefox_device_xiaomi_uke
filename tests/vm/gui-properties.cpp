// Isolated VM fixture only. Creates a property area; no property service or TEE.
extern "C" int __system_property_area_init();
extern "C" int __system_property_add(const char*,unsigned,const char*,unsigned);
extern "C" int __system_property_get(const char*,char*);
extern "C" int printf(const char*,...);
extern "C" int strcmp(const char*,const char*);
int main(int argc,char** argv) {
    // No argument preserves the original synthetic fixture. The graphical
    // harness supplies an explicit rotation for its generic virtual display.
    const char* rotation=argc==2?argv[1]:nullptr;
    if(argc>2 || (rotation && strcmp(rotation,"0")!=0 && strcmp(rotation,"90")!=0 &&
        strcmp(rotation,"180")!=0 && strcmp(rotation,"270")!=0)) {
        printf("Usage: uke-vm-properties [0|90|180|270]\n");return 64;
    }
    const int initialized=__system_property_area_init();
    printf("URE_VM_PROPERTY_AREA_INIT_RESULT %d\n",initialized);
    if(initialized!=0)return 1;
    if(__system_property_add("ro.treble.enabled",17,"true",4)!=0) return 2;
    if(__system_property_add("sys.use_memfd",13,"true",4)!=0) return 3;
    if(rotation) {
        const unsigned bytes=rotation[1]=='\0'?1:rotation[2]=='\0'?2:3;
        constexpr char name[]="persist.twrp.rotation";
        if(__system_property_add(name,sizeof(name)-1,rotation,bytes)!=0)return 4;
        char observed[92]{};
        if(__system_property_get(name,observed)!=static_cast<int>(bytes) || strcmp(observed,rotation)!=0)return 5;
        printf("URE_VM_ROTATION_PROPERTY %s\n",observed);
    } else {
        printf("URE_VM_ROTATION_PROPERTY absent\n");
    }
    printf("URE_VM_PROPERTY_AREA_READY\n");return 0;
}

#include <fstream>
#include <string>
#include <vector>
#include <property_info_serializer/property_info_serializer.h>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::string bytes,error;
    if(!android::properties::BuildTrie({},"u:object_r:default_prop:s0","string",&bytes,&error))return 3;
    std::ofstream output(argv[1],std::ios::binary);output.write(bytes.data(),bytes.size());return output?0:4;
}
// GNU-host adapter for the POSIX strerror ABI used by the pinned AOSP libbase.
extern "C" int __xpg_strerror_r(int,char*,unsigned long);
extern "C" int posix_strerror_r(int number,char* output,unsigned long bytes) { return __xpg_strerror_r(number,output,bytes); }

// VM-only bridge for the legacy ashmem API; never installed in recovery.
#if !defined(__aarch64__)
#error This VM adapter uses the AArch64 memfd_create syscall ABI
#endif
extern "C" long syscall(long,...);
extern "C" int ftruncate(int,long);
extern "C" int close(int);
extern "C" int __wrap_ashmem_create_region(const char* name,unsigned long bytes) {
    if(bytes==0 || bytes>64UL*1024UL*1024UL)return -1;
    const int fd=static_cast<int>(syscall(279,name?name:"ure-vm-code-cache",1U));
    if(fd<0)return fd;
    if(ftruncate(fd,static_cast<long>(bytes))!=0) {close(fd);return -1;}
    return fd;
}

// SPDX-License-Identifier: Apache-2.0
// VM-only entry point to the shipping AOSP metadata library. The disposable
// guest has no Android Binder/property service for the normal lpdump client.
int LpdumpMain(int argc,char* argv[]);
int main(int argc,char* argv[]) { return LpdumpMain(argc,argv); }

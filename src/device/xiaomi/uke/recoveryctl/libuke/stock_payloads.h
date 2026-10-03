// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

namespace ure::stock_source {
struct Payload { const char* filename; const char* label; unsigned lun; bool slotted;
    std::uint64_t source_bytes,expanded_bytes; const char* encoding; const char* sha256; };
// Reviewed archive contents, not evidence of installed firmware or SKU capacity.
inline constexpr Payload global[]{
    {"boot.img","boot",4,true,100663296,100663296,"raw","efdee1d4e1acd7f6e77615330dbcb045caeeccad8568d89fa6abd627f606e77f"},
    {"dtbo.img","dtbo",4,true,20971520,20971520,"raw","044aae9d9a144e9a05b91d2785a2ff4504c78caa11f8c22781839ba2f6c76490"},
    {"init_boot.img","init_boot",4,true,8388608,8388608,"raw","c4eb22f5c379678aead0cf7ab003f7534d923be16b0601a0b4da7d1705af10b2"},
    {"metadata.img","metadata",0,false,2236644,67108864,"android-sparse-v1","999f892b89a9b4dcbcdc54e4d1e5dd85f4edc1cefe2606dcb960824e97e81c18"},
    {"recovery.img","recovery",4,true,104857600,104857600,"raw","a22c93ccd0d439d610547a47ab4d8001f72ee769f791991f65d47d5724db049b"},
    {"super.img","super",0,false,8199567300ULL,11274289152ULL,"android-sparse-v1","9ca118bed2d3e80e3722e536c334d96be4a1d2a9b6221408f8807e598a9d0669"},
    {"userdata.img","userdata",0,false,1289147004,48318382080ULL,"android-sparse-v1","577d797c8cf6a37f7a5590078e4b2cae7139dacb375a1e9f40ca88a2020e301e"},
    {"vbmeta.img","vbmeta",4,true,8192,8192,"raw","77e3c2069304c562418ac2b9ac277d322a5edce71a560f6099510ecf37c17b81"},
    {"vbmeta_system.img","vbmeta_system",0,true,4096,4096,"raw","19cdebfe73e710f3e1ebc699e50768bdeb668ee05f60b5081354912312aa2f6c"},
    {"vendor_boot.img","vendor_boot",4,true,100663296,100663296,"raw","c2811677d6aa07753b615747c4f2dba110dd4519cf52ff3a89e41e00a5b02bcc"}
};
inline constexpr const char* archive_sha256="f811ae6255b7535d32f80548d800487a6494a87ddb4cca592799337fab24cd0d";
}

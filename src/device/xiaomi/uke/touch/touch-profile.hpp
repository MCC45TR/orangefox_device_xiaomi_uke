// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace uke::touch {
enum class FileKind { executable, library, data, linker, linker_config };
struct FilePin {
    std::string_view path, sha256;
    std::uint64_t bytes;
    FileKind kind;
};
// These cached installed files match the reviewed installed-device comparison.
// No proprietary byte is copied into a recovery by this profile.
inline constexpr FilePin kInstalledFiles[] = {
    {"/odm/bin/hw/vendor.xiaomi.hw.touchfeature-service",
     "341d3294e05a9bbf6986d59aabac621f00f256c389f6cefb634ef032a48b431e", 198216,
     FileKind::executable},
    {"/odm/lib64/libtouchreport.so",
     "33ab57d793f67983853b2119fed79acbf24a4862169fb757290ca8353ff988aa", 1354896,
     FileKind::library},
    {"/odm/lib64/libtouchsensor.so",
     "ef8118a20964638ddc1ada20f1b3dfcf3c01f1a7ca388f43cce845e8b43db1ba", 364960, FileKind::library},
    {"/odm/lib64/libtensorflowlite_touch_c.so",
     "22226ab82e34c88e69d808a63ec0952d1a83065a64173020b6f562d28a2dfb20", 2923544,
     FileKind::library},
    {"/odm/firmware/palm_check.tflite",
     "6cd7c418c0a271710f46fb8f1de1b2d9899a5f9e1abdf5a591b25b24e7c93966", 13436, FileKind::data},
    {"/odm/firmware/water_check.tflite",
     "2fd977535eda59e5d8b980d1724b12f205356a7f5a3e8114926307ed52ff5575", 12200, FileKind::data},
    {"/odm/firmware/film_model.tflite",
     "ebde241859f559c6fe994feb2fe73e659060863fa0533e01b41ac60c414953a0", 41472, FileKind::data},
    {"/odm/firmware/glove_model.tflite",
     "85a320b73f31b5650a48629ba75f986b3db2cce4f3ef2d1ae18f3c304a789de2", 10768, FileKind::data},
    {"/odm/firmware/novatek_nt36532_o82_fw_tm.bin",
     "12ce2693b965a35e3469984fd95afebb4c0a740ca908159b0a5e6bdad5968619", 245760, FileKind::data},
    {"/odm/firmware/novatek_nt36532_o82_mp_csot.bin",
     "b34db19d7021ccb053a2e1bfe414e86d06b16e63cdad05ca2fd8765745adb40f", 245760, FileKind::data},
    {"/odm/firmware/novatek_nt36532_o82_mp_tm.bin",
     "632fd02f279dc285c22f3a2795cd42c14dd131725cdbd863ab43a51c1a2b6410", 245760, FileKind::data},
    {"/odm/firmware/novatek_nt36532_o82_fw_csot.bin",
     "a4f949fb89c7eaad9da85a5885cd2dcc1fffbf08bc1e7ffe9f0bb97c1769b310", 245760, FileKind::data},
    {"/odm/etc/touch/stylus_game_config.json",
     "9cf15a28152cf89ade9c02417ec573fe5d9a8d2caf4545cb415c3e7fe2dbbbbf", 341, FileKind::data},
    {"/odm/etc/touch/Pencil_Posture.xml",
     "08ab045cd3010b82bee9367ed780122d66d7a7cc784553bfc11ad530c705b102", 1148, FileKind::data},
    {"/odm/etc/TouchPad_Upgrade_0x04.bin",
     "d40d9f0c60bf5b737cacdcce33a6144d7048274c785e14bf7e0e9cd2c0b097e9", 55280, FileKind::data},
    {"/vendor/lib64/libmisight.so",
     "f43f5c2090ea325440db6e891710755273999ffdd5c33383266316cffcebf447", 28112, FileKind::library},
    {"/vendor/lib64/vendor.xiaomi.hw.touchfeature-V1-ndk_platform.so",
     "e6ba44d6efc913679053ecb42beed4a180cfe545fb80530ffa681fb8b95d7f9b", 72032, FileKind::library},
    {"/vendor/lib64/android.frameworks.sensorservice@1.0.so",
     "41c50b41172d14b7b1fa5c8e63a77ebb74be8a3918fbbf23b7968aec82f6523a", 236024, FileKind::library},
    {"/vendor/lib64/android.hardware.sensors@1.0.so",
     "5e1fd913fb5b4c2040e75a7589fdaf447874c3e8b9e497266058f075707b4cf0", 119200, FileKind::library},
    {"/vendor/lib64/android.hardware.sensors@2.0.so",
     "594b35964147c077d21f2a6b917c66e8b2d48fdcdb5b50a32eaf45d0145c798c", 169256, FileKind::library},
    {"/vendor/lib64/libxml2.so", "99ca0ddf8685f4a5af16663c3cc3abebbce492e640e3cdd9df6e1257509f5428",
     1192304, FileKind::library},
};
inline constexpr std::array<std::string_view, 25> kExpectedElfPaths = {
    "/odm/bin/hw/vendor.xiaomi.hw.touchfeature-service",
    "/odm/lib64/libtouchreport.so",
    "/odm/lib64/libtouchsensor.so",
    "/odm/lib64/libtensorflowlite_touch_c.so",
    "/vendor/lib64/libxml2.so",
    "/vendor/lib64/libmisight.so",
    "/vendor/lib64/vendor.xiaomi.hw.touchfeature-V1-ndk_platform.so",
    "/vendor/lib64/android.frameworks.sensorservice@1.0.so",
    "/vendor/lib64/android.hardware.sensors@1.0.so",
    "/vendor/lib64/android.hardware.sensors@2.0.so",
    "/system/lib64/libapexsupport.so",
    "/system/lib64/libbase.so",
    "/system/lib64/libbinder.so",
    "/system/lib64/libbinder_ndk.so",
    "/system/lib64/libc++.so",
    "/system/lib64/libc.so",
    "/system/lib64/libcutils.so",
    "/system/lib64/libdl.so",
    "/system/lib64/libdl_android.so",
    "/system/lib64/libhidlbase.so",
    "/system/lib64/libjsoncpp.so",
    "/system/lib64/liblog.so",
    "/system/lib64/libm.so",
    "/system/lib64/libutils.so",
    "/system/lib64/libvndksupport.so"};
// Source-built providers are image-dependent. Packaging must compare these
// reviewed runtime hashes with the new sealed payload before publication.
inline constexpr std::array<FilePin, 18> kImageFiles = {{
    {"/system/lib64/libapexsupport.so", "3c9f6bf01356ecf4dc1d92519abd11a5317c316917c9d91ee85bb94ae7827f3a", 186048, FileKind::library},
    {"/system/lib64/libbase.so", "7301152a50cf3c8179a4fa26fd62981403975b6d61243cfd288182359989c8c6", 219784, FileKind::library},
    {"/system/lib64/libbinder.so", "12632d905d2e57b83e2770e5ba4fe6408b7f514db49874ed815580a66628b8f9", 759008, FileKind::library},
    {"/system/lib64/libbinder_ndk.so", "d238c5bfb2ffb56af8631fb9e23dcc343e00e9b7179e18231929f2d4f67175a3", 153328, FileKind::library},
    {"/system/lib64/libc++.so", "2267f93b8b3c9d1967f1833d5f71c7312213c43bb291250cf772800763037fb9", 1049704, FileKind::library},
    {"/system/lib64/libc.so", "5424093b26dc0cc2dcf4f52c515cb24ffc0c4603d5b3526cb44536d4c8a0f815", 1208680, FileKind::library},
    {"/system/lib64/libcutils.so", "1fad9efe9c86c43e4d5e65491098d491d90fba9e8b2b4b61c9b597bee5c52068", 118416, FileKind::library},
    {"/system/lib64/libdl.so", "5cba916ac95c941b99e82ca3091b1c604b910fafae8baa92586314964445552f", 50760, FileKind::library},
    {"/system/lib64/libdl_android.so", "739c5716653bcff6700501d98f576957fc299e4f2483c3ad2bd1c0f641e96422", 34672, FileKind::library},
    {"/system/lib64/libhidlbase.so", "09fbd3ef26cf15c30ae3ee7da464d71a98e2b20fe16f213e0b1ad2c6dfa736eb", 704752, FileKind::library},
    {"/system/lib64/libjsoncpp.so", "02adc1c1421a3291a89c7740273b58073c8fabca5b16b3e3b0b9b78dd1a9aeee", 200760, FileKind::library},
    {"/system/lib64/liblog.so", "fb4a8808cfece7ad128482cf7d83fb3985ed8578507fa265a3100e9684d5c8bd", 102096, FileKind::library},
    {"/system/lib64/libm.so", "44433149f62eeb8f09f38ecd3566683fac01a0f89de6ea6d15f6e4deca1ba4bc", 248968, FileKind::library},
    {"/system/lib64/libutils.so", "bdb5e2e02e0f2e2e92fb4e8364b9172996d15adb96071c7d31d9fb51856e3a33", 134776, FileKind::library},
    {"/system/lib64/libvndksupport.so", "448ee8d7be3a030bcc30a791a77b77628e8314b7b5aa28f70437cc3a1bd04a8a", 51352, FileKind::library},
    {"/system/bin/linker64", "279a489c2e60682d2e479cc85c9cf42e63655ee524cf147851f35a9bbd4daf8e", 2183528, FileKind::linker},
    {"/system/etc/ld.config.txt", "850e6c29e68eec2ca97850084052fe8b379f23406b96dec8187edef7dd595421", 356, FileKind::linker_config},
    {"/linkerconfig/ld.config.txt", "850e6c29e68eec2ca97850084052fe8b379f23406b96dec8187edef7dd595421", 356, FileKind::linker_config},
}};
inline constexpr bool kKnownDlopenAndAssetInventoryReviewed = true;
inline constexpr bool kImageRuntimeClosureReviewed = true;
// This input profile grants no storage, encryption or first-stage acceptance.
// Input startup is still subject to a fresh-image cold-boot owner test.
inline constexpr bool kInputStartupProfileReviewed = true;
inline constexpr std::string_view kKernelRelease =
    "6.1.175-android14-11-ga3b9c44908dd-ab13320413";
inline constexpr std::array<FilePin, 2> kInputModules = {{
    {"/vendor_dlkm/lib/modules/xiaomi_touch.ko",
     "4bc14f0464979e6a3df54abd943b0650c392f3b859483dacdb52900f422baa75", 308480, FileKind::data},
    {"/vendor_dlkm/lib/modules/nt36532_touch.ko",
     "5bbee229e58070b1b1da529a32a10d33999889db95790396e67794d57e5e649e", 1063192, FileKind::data},
}};
inline constexpr std::string_view kService = "/odm/bin/hw/vendor.xiaomi.hw.touchfeature-service";
inline constexpr std::string_view kProcessor = "/odm/lib64/libtouchreport.so";
inline constexpr std::string_view kLinker = "/system/bin/linker64";
inline constexpr std::array<std::string_view, 2> kRoots = {kService, kProcessor};
inline constexpr std::array<const char *, 4> kEnvironment = {
    "LD_LIBRARY_PATH=/odm/lib64", "PATH=/system/bin:/sbin", "TMPDIR=/tmp", nullptr};
} // namespace uke::touch

#pragma once

#include <cstdint>

namespace WeChatOffsets {

// Offsets observed from the currently opened IDA database. Keep these version gated.
constexpr const char* kWeixinModuleName = "Weixin.dll";
constexpr const char* kLibGlesModuleName = "libGLESv1.dll";

constexpr uintptr_t kOriginalLoginInitRva = 0x1A0410;
constexpr uintptr_t kOriginalProfileCacheRva = 0x19AA70;
constexpr uintptr_t kWeixinPrintPbRva = 0x5C3910;

constexpr uintptr_t kSendTextCall = 0x15AEF20;
constexpr uintptr_t kSendTextObjSeed = 0x8115AE8;
constexpr uintptr_t kInitTextMessage = 0x632B00;
constexpr uintptr_t kInitFileMessage = 0x20BC920;
constexpr uintptr_t kSendFileObjSeed = 0x20456BA;
constexpr uintptr_t kInitSceneContext = 0xD8F0;
constexpr uintptr_t kSceneVtable1 = 0x81B9D78;
constexpr uintptr_t kSceneVtable2 = 0x81B9CB8;
constexpr uintptr_t kSceneVtable3 = 0x81B9BF8;
constexpr uintptr_t kSceneGlobalPtr = 0x9BF3D40;
constexpr uintptr_t kTaskDispatchRva = 0x2D4D90;
constexpr uintptr_t kTaskConstructorRva = 0xE8FA40;
constexpr uintptr_t kTaskIdGlobalRva = 0x9CC8420;

}

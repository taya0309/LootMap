// ===========================================================================
//  OverlayHost —— 独立版自己充当"叠加层宿主"（v0.3.0）
//
//  背景（读主插件源码得出，2026-09-23）：
//    主插件的漂亮面板与地图星标，全部画在 **MapSense 那一层 Dear ImGui** 里。
//    它接入的方式只有一个接口（静态逆向 + 源码注释双重确认）：
//
//      导出函数  RuffnecKkMapSenseGetOverlayHostApi(abi=2, structSize>=0x20)
//        → 返回 32 字节的宿主接口块：
//            +0x00 u32 结构大小 = 0x20
//            +0x04 u32 版本     = 2
//            +0x08 u64 口令 magic = 0xF401021D19150002
//            +0x10 fn  registerClient
//            +0x18 fn  unregisterClient
//
//      registerClient(客户端描述块, 结构大小 >= 0x48)：
//            +0x00 u32 结构大小 / +0x04 u32 版本 = 2
//            +0x08 名字指针 / +0x10 u64 同一个 magic
//            +0x18~+0x38 五个回调函数指针（至少一个非空）
//            +0x40 用户数据指针
//
//      宿主回调客户端时给的上下文（栈上构造，只在回调期间有效）：
//            +0x00 u32 结构大小 = 0x20   +0x04 u32 版本 = 2
//            +0x08 void* 宿主的 ImGui 上下文
//            +0x10 void* 宿主单例对象
//            +0x18 f32 视口宽  +0x1C f32 视口高
//
//    所以：**独立版只要把这个接口和这套回调实现出来**，主插件（一个字节都不用改）
//    就会把面板与星标画进我们的叠加层 —— 界面与显示效果和装了 MapSense 时
//    **完全一致**（因为画的还是主插件自己那份代码，只是换了块画布）。
//
//  另一件必须做的事：主插件是按**模块名**找宿主的
//      GetModuleHandleW(L"d2rl-ruffneckk-mapsense.dll") → GetProcAddress(...)
//    我们的 DLL 不叫这个名字，所以这里给 GetModuleHandleW / GetModuleHandleExW
//    装一个很窄的钩子：**只有当真实的地图插件不存在、且问的正是这个名字时**，
//    才把"我们自己"的模块句柄回给它（等于告诉主插件"宿主在这儿"）。
//    其它任何模块名一律原样转发，不碰。
// ===========================================================================

#pragma once

#include <cstdint>

namespace HostApi {

using LogFn = void (*)(const char* text) noexcept;
auto SetLogger(LogFn logger) noexcept -> void;

// 客户端回调（编号 0..4，由注册方决定各自用途；我们每帧全叫一遍，
// 主插件内部按帧号去重，一帧只会真正画一次）。
using Callback = void(__fastcall*)(const void* ctx, void* userData) noexcept;

// 已注册客户端数量 / 有没有客户端
auto ClientCount() noexcept -> int;

// 把已注册客户端的回调叫一遍。
//   imguiContext = 我们自己的 ImGui 上下文（客户端会 SetCurrentContext 到它）
//   hostObject   = 宿主对象（客户端只是记一笔，不要求非空）
//   width/height = 视口尺寸（客户端用来排版）
auto InvokeClients(void* imguiContext, void* hostObject, float width, float height) noexcept -> void;

// 装/摘"模块名别名"钩子（让主插件按 MapSense 的名字能找到我们）
auto InstallModuleAliasHooks() noexcept -> bool;
auto RemoveModuleAliasHooks() noexcept -> void;

// 诊断：别名钩子状态 / 命中次数（写日志用）
auto AliasHitCount() noexcept -> std::uint32_t;

}   // namespace HostApi

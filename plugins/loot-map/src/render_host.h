// ===========================================================================
//  RenderHost —— 给 Loot Map 提供的"自绘面板"宿主
//
//  技术路线与地图插件 MapSense 完全一致（已拆它的 DLL 确认）：
//    1. 插件加载时（早于游戏图形初始化）把自己工厂的 CreateSwapChain
//       虚表项换掉（vtable patch，全局生效，不影响游戏自己的对象）
//    2. 游戏创建交换链时接住它 → 拿到设备、命令队列、窗口句柄
//    3. 换掉交换链的 Present 虚表项 → 每帧在游戏画面之后画 ImGui
//    4. Win32 消息钩子转发鼠标键盘给 ImGui，面板打开时按需吞掉输入
//
//  安全策略（fail-closed，学 MapSense）：
//    · 没等到交换链 → 只打日志，面板不可用，游戏照常跑
//    · 设备被移除 / 初始化失败 → 干净卸载，不再尝试
//    · 本模块绝不主动创建交换链、绝不改游戏文件
// ===========================================================================

#pragma once

namespace RenderHost {

// 插件加载时调用一次：启动"事后抓取"线程（自建诱饵交换链拿到系统共享虚表，
// 换掉 Present/Present1/ExecuteCommandLists，游戏第一帧就会被接住）
auto Install() noexcept -> void;

// 注入日志函数（可空）：渲染宿主的每一步进展都会写进插件日志
using LogFn = void(*)(const char* text) noexcept;
auto SetLogger(LogFn logger) noexcept -> void;

// 插件卸载时调用：把还能恢复的都恢复掉
auto Shutdown() noexcept -> void;

// 面板内容绘制回调（在游戏的渲染线程上被调用，每帧最多一次，仅面板打开时）
using UiCallback = void(*)() noexcept;
auto SetUiCallback(UiCallback callback) noexcept -> void;

// 开关面板（控制台命令 / 其他线程调用都安全）
auto TogglePanel() noexcept -> void;
auto SetPanelOpen(bool open) noexcept -> void;
auto IsPanelOpen() noexcept -> bool;

// 状态查询（给日志/控制台用）
auto IsRendererAlive() noexcept -> bool;   // ImGui 已在游戏窗口上跑起来
auto RendererStatusText() noexcept -> const char*;

} // namespace RenderHost

cmake_minimum_required(VERSION 3.29)

# ---------------------------------------------------------------------------
#  Loot Map — 掉落物地图标记插件
#  构建：需要一个可用的 MSVC + CMake 3.29+，以及 D2RLoader PluginSDK。
# ---------------------------------------------------------------------------

project(LootMapPlugin LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDK 位置（默认取上级目录里 clone 下来的那份，可用 -DD2RL_SDK_DIR=... 覆盖）
set(D2RL_SDK_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../d2rloader-pluginsdk"
    CACHE PATH "Path to the D2RLoader PluginSDK checkout")

if(NOT EXISTS "${D2RL_SDK_DIR}/include/D2RLPlugin/api.h")
    message(FATAL_ERROR "找不到 D2RLoader PluginSDK：${D2RL_SDK_DIR}\n"
                        "请用 -DD2RL_SDK_DIR=<路径> 指定，或把 SDK clone 到上一级目录。")
endif()

# 只取 SDK 的头文件与 CMake helper，不构建它自带的示例
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(D2RLPLUGIN_BUILD_EXAMPLES_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${D2RL_SDK_DIR}" "${CMAKE_BINARY_DIR}/_d2rloader_sdk")

# ---------------------------------------------------------------------------
# Dear ImGui（自绘面板，MapSense 同款路线）
# ---------------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_impl_dx12.cpp"
    "${IMGUI_DIR}/imgui_impl_win32.cpp"
)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}")

# ★★ 千万不要给任何目标定义 IMGUI_DISABLE_OBSOLETE_FUNCTIONS ★★
#   这个宏会把 ImGuiIO 末尾的 3 个指针（GetClipboardTextFn /
#   SetClipboardTextFn / ClipboardUserData，x64 上 24 字节）从结构体里删掉。
#   而 ImGuiContext 内嵌 ImGuiIO，于是 IO 之后**每一个**成员的偏移都会整体
#   前移 24 字节（FrameCount / WithinFrameScope / CurrentWindow / Viewports ...）。
#
#   本插件是借宿主的 ImGuiContext* 画窗口的，所以我们的 imgui.lib 必须和宿主
#   *逐字节同一套布局*。v0.11.1 的故障就是栽在这里：只在 imgui 这个静态库上
#   开了宏、插件目标没开，两份结构体差 24 字节 —— 插件自己的字段读取恰好
#   是对的（和宿主一致），可 imgui.lib 里的 Begin / PushFont / GetCurrentWindow
#   按错的偏移去读宿主上下文，一画就异常，面板退回原生面板。
#
#   宿主布局已实测（tools/fmt_layout_probe.py + tools/dump_layout_region.py，
#   对 MapSense 与 Floating Damage 两份 DLL 都验过，两者字节完全相同）：
#       FrameCount = 0x12B0 / WithinFrameScope = 0x12BC / CurrentWindow = 0x1368
#   即**没有**定义这个宏的默认布局。
#   third_party/imgui/imconfig.h 里加了 #error 兜底，谁再打开它就直接编译不过。

# 与主 DLL 一致使用静态 CRT，否则 /MD 版 imgui.lib 会产生 __imp_* 符号链接错误
set_target_properties(imgui PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
if(MSVC)
    target_compile_options(imgui PRIVATE /utf-8 /W3)
endif()

# ---------------------------------------------------------------------------
# 插件目标：输出 d2rl-loot-map.dll
#   D2RLoader 按文件名推导插件 id，因此 DLL 必须叫 d2rl-<plugin-id>.dll
# ---------------------------------------------------------------------------
add_library(LootMapPlugin SHARED
    src/loot_map_plugin.cpp
    src/render_host.cpp
    src/loot_map_plugin.rc
)

target_link_libraries(LootMapPlugin PRIVATE D2RLPlugin::D2RLPlugin imgui d3d12 dxgi delayimp)

# 图形相关 DLL 一律延迟加载：插件被 LoadLibrary 时只依赖 KERNEL32/USER32，
# d3d12/dxgi/着色器编译器等到真正要用（渲染线程初始化）时才拉起。
# 背景：v0.3.1 曾在游戏进程里出现 LoadLibrary 报 1114（DLL 初始化失败），
# 怀疑与插件在游戏极早期阶段强制首载这批图形 DLL 有关。
if(MSVC)
    target_link_options(LootMapPlugin PRIVATE
        /DELAYLOAD:d3d12.dll
        /DELAYLOAD:dxgi.dll
        /DELAYLOAD:D3DCOMPILER_47.dll
        /DELAYLOAD:IMM32.dll
        /DELAYLOAD:SHELL32.dll
    )
endif()

# 源码里有中文注释：强制按 UTF-8 读源码、按 UTF-8 生成字符串字面量，
# 否则 MSVC 会按当前代码页(936)解释，报 C4819 并可能弄坏字符串内容。
if(MSVC)
    target_compile_options(LootMapPlugin PRIVATE /utf-8 /W4 /permissive-)
endif()

set_target_properties(LootMapPlugin PROPERTIES
    OUTPUT_NAME "d2rl-loot-map"
    PREFIX ""
    SUFFIX ".dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# 把默认配置嵌进 DLL：D2RLoader 会在缺少 config/loot-map.toml 时自动写出来
d2rlplugin_embed_config(
    TARGET LootMapPlugin
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/loot-map.toml"
)

# 构建完把成品和默认配置拷到一个便于取用的目录
set(LOOTMAP_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
add_custom_command(TARGET LootMapPlugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LOOTMAP_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:LootMapPlugin>" "${LOOTMAP_STAGE_DIR}/"
    COMMENT "Loot Map：输出已复制到 ${LOOTMAP_STAGE_DIR}"
)

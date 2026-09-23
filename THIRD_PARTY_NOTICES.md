# 开源组件声明（THIRD PARTY NOTICES）

本仓库（D2R 插件集）在开发中使用了以下第三方开源代码与资料，在此一并致谢。
各组件以其自带的许可证文本为准。

---

## 1. Dear ImGui — MIT License

- 版权所有 (c) 2014-2024 Omar Cornut 及 ImGui 贡献者
- 用途：设置面板与星形标记的绘制（DX12 / Win32 后端）
- 位置：`plugins/loot-map/third_party/`（随附 `LICENSE.txt`，以该文件为准）
- 官方仓库：<https://github.com/ocornut/imgui>

## 2. MinHook — BSD 系许可证

- 版权所有 (c) 2009-2017 Tsuda Kageyu
- 用途：独立叠加层版对 DXGI 的函数挂钩
  （IDXGIFactory 的 CreateSwapChain 系列、IDXGISwapChain 的 Present / Present1 / ResizeBuffers）
- 位置：`plugins/loot-map/third_party/minhook/`（随附 `LICENSE.txt`，以该文件为准）
- 官方仓库：<https://github.com/TsudaKageyu/minhook>

## 3. RuffnecKk MapSense — MIT License（参考实现，特别致谢）

- 版权所有 (c) RuffnecKk（RuffDood）
- 仓库：<https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite>
- 用途：**独立叠加层版**（`plugins/loot-map/src/standalone/`）的叠加层宿主，
  其实现思路参考并改编自该仓库的 `plugins/mapsense/src/d3d12_imgui_host.*`
  （该文件自身注明改编自 locbones/D2RHUD-2.4）。

  具体而言：未逐行复制其源码，而是按其公开的做法独立实现——
  用 MinHook 挂钩 DXGI 工厂的 CreateSwapChain 系列（在创建那一刻精确拿到
  **命令队列 / 交换链 / 窗口** 的配对）、再从交换链虚表读出 Present / ResizeBuffers
  加以挂钩、在 Present 内同步提交到**游戏自己的命令队列**；
  同时兼容其宿主接入接口 `RuffnecKkMapSenseGetOverlayHostApi`，
  使主插件无需修改即可在两种宿主（真 MapSense / 本独立叠加层）之间无缝工作。

- 上游致谢：locbones/D2RHUD-2.4

---

## 本仓库的许可证

除上述第三方组件外，本仓库源码以 **MIT License** 发布（见 [LICENSE](LICENSE)）。
第三方组件仍遵循其各自的许可证；若你复用本仓库代码，请一并保留上述声明。

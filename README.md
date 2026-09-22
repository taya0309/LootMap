# 掉落物地图标记（Loot Map）

暗黑破坏神 II：重制版（D2R）的 **D2RLoader 插件**：在地图叠加层上为掉落物画星形标记——
暗金、套装一眼可见，支持蓝装前缀（珠宝匠/工匠）规则星，面板全部中文。

> **免责声明**：本插件仅供**单机/离线模组**环境使用，请勿在战网等联网环境使用，
> 由此产生的一切后果自负。

## 功能

- **星形标记**：暗金/套装掉落物在地图上显示为立体星（暗黑3风格渐变，也可切简约样式）
- **蓝装前缀规则**：带「珠宝匠」/「工匠」前缀的 4 孔蓝装显示规则星（前缀 ID 判定，
  不会误认「工匠之」；换模组后如编号变化，用 `tools/diff_starid.py` 重新校准）
- **面板（F7）**：总开关 / 品质（暗金、套装）/ 规则 / 星星（样式、大小 12~36、描边、
  左右与上下微调），改动即存
- **颜色**：每类 14 色预设色板 + 完整调色板
- **字体**：启动时自动把微软雅黑注册进宿主字体图集（游戏建库前），面板中文统一字型
- **挡鼠标**：光标在面板上时吞掉点击与滚轮，人物不会跟着动

## 安装（需要 D2RLoader 1.2.x + D2R 3.2.x）

1. 关闭游戏
2. `d2rl-loot-map.dll` → `<游戏目录>\d2rloader\plugins\`
3. `loot-map.toml` → `<游戏目录>\d2rloader\config\`
4. 进游戏按 **F7** 打开面板

## 构建

- Windows + MSVC（x64）+ CMake + Ninja
- 需要 [D2RLoader PluginSDK](https://github.com/) 放在 `../d2rloader-pluginsdk`
  （或用 CMake 变量 `D2RL_SDK_DIR` 指定路径）
- `third_party/` 内已含 ImGui（DX12/Win32 后端），无需额外下载

```powershell
cmake -B build-ninja -G Ninja
cmake --build build-ninja
# 产物：build-ninja/stage/d2rl-loot-map.dll
```

## 目录结构

```
src/           插件源码（单文件主体 + 渲染宿主）
tools/         开发期工具：日志分析、STARID 对比、DX12 偏移探测、字节校验等
third_party/   ImGui（MIT，见 third_party/LICENSE.txt）
```

## 已知限制

- rule0 蓝装前缀判据（前缀 ID 1209/1210）实测于 当时实测所用模组 模组；原版或其他模组可能不同
- 游戏大版本更新可能改变内存偏移，需要重新校准（tools/ 里是全套校准脚本）

## License

插件源码 MIT；`third_party/` 下的 ImGui 遵循其自身 LICENSE。

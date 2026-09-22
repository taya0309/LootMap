# 掉落物地图标记（Loot Map）

暗黑破坏神 II：重制版（D2R）的 **D2RLoader 插件**：在地图叠加层上为掉落物画星形标记——
暗金、套装一眼可见，支持蓝装前缀（珠宝匠/工匠）规则星，面板全部中文。

> **免责声明**：本插件仅供**单机/离线模组**环境使用，请勿在战网等联网环境使用，
> 由此产生的一切后果自负。

## 运行前提（重要，先看这里）

| 组件 | 说明 |
|---|---|
| **D2R 3.2.x**（3.2.92777 实测） | 内存偏移按这个版本校准，大版本更新需重新校准 |
| **D2RLoader 1.2.x** | 插件加载器（`d2rl-*.dll` 的宿主） |
| **RuffnecKk MapSense** ⚠️ **必需** | 提供游戏画面上的叠加层（`d2rloader\plugins\d2rl-ruffneckk-mapsense.dll`） |
| RuffnecKk Floating Damage 1.5.0 | 可选，同作者，实测共存良好 |

**为什么必须装 MapSense？** 游戏上"叠一层 Dear ImGui"这件事只能有一个渲染主人
（自己再叠一层会跟 MapSense 抢 DirectX 12 而崩）。MapSense 主动开放了一个接入接口，
本插件就把星形标记和设置面板**画进它那一层**。

**没装 MapSense 会是什么样？**

- 地图上物品的**颜色**照常生效（那部分直接改游戏自己的绘制，与叠加层无关）；
- 但**星形标记不会出现**；
- 按 F7 弹出的是本插件自带的**老式原生面板**（一堆小方框 + 色块，标题「掉落物地图标记」）。

→ **只要看到那个老面板，就说明缺 MapSense**（日志 `d2rloader\logs\loot-map.log`
里会有一行 `panel fallback ... mapsense_module=0` 明写原因）。

## 功能

- **星形标记**：暗金/套装掉落物在地图上显示为立体星（暗黑3风格渐变，也可切简约样式）
- **蓝装前缀规则**：带「珠宝匠」/「工匠」前缀的 4 孔蓝装显示规则星（前缀 ID 判定，
  不会误认「工匠之」；换模组后如编号变化，用 `tools/diff_starid.py` 重新校准）
- **面板（F7）**：总开关 / 品质（暗金、套装）/ 规则 / 星星（样式、大小 12~36、描边、
  左右与上下微调），改动即存
- **颜色**：每类 14 色预设色板 + 完整调色板
- **字体**：启动时自动把微软雅黑注册进宿主字体图集（在游戏建库之前），面板中文统一字型
- **挡鼠标**：光标在面板上时吞掉点击与滚轮，人物不会跟着动

## 安装

1. 关闭游戏
2. **先确认已装 RuffnecKk MapSense**（见上面「运行前提」，没有它没有星标、也没有新面板）
3. `d2rl-loot-map.dll` → `<游戏目录>\d2rloader\plugins\`
4. `loot-map.toml` → `<游戏目录>\d2rloader\config\`
5. 进游戏按 **F7** 打开面板

## 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| F7 弹出的是**老式面板**（小方框 + 色块） | 缺 **MapSense**：装 `d2rl-ruffneckk-mapsense.dll` 到 `plugins\` |
| 有颜色但**地图上没有星** | 同上：星标画在 MapSense 的叠加层里 |
| 按 F7 **完全没反应** | 看 `logs\loot-map.log` 里 `Overlay panel:` / `panel fallback` 两行 |
| **「珠宝匠/工匠」蓝星不亮或不准确** | 前缀编号是按 当时实测所用模组 模组实测的（1209/1210）；换模组后用 `tools/diff_starid.py` 对样本重新校准 |
| 星的位置或大小不满意 | 面板「星星」区：大小滑条 + 左右/上下微调（拖到对准物品，自动保存） |
| 手改了 `loot-map.toml` 又被面板覆盖 | 面板上动任何控件都会按内存值重写配置文件 —— 请在面板上改 |

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
release/       成品：d2rl-loot-map.dll + loot-map.toml（下载这两个即可）
tools/         开发期工具：日志分析、STARID 对比、蓝装前缀校准、字节校验等
third_party/   ImGui（MIT，见 third_party/LICENSE.txt）
CHANGELOG.md   更新记录（每个版本改了什么）
```

## 已知限制

- rule0 蓝装前缀判据（前缀 ID 1209/1210）实测于 当时实测所用模组 模组；原版或其他模组可能不同
- 星标与新版面板依赖 RuffnecKk MapSense（见「运行前提」）
- 游戏大版本更新可能改变内存偏移，需要重新校准（tools/ 里是全套校准脚本）

## License

插件源码 MIT；`third_party/` 下的 ImGui 遵循其自身 LICENSE。

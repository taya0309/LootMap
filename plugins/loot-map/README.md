# 掉落物地图标记（Loot Map）

暗黑破坏神 II：重制版（D2R）的 **D2RLoader 插件**：在地图叠加层上为掉落物画星形标记——
暗金、套装一眼可见，支持蓝装前缀（珠宝匠/工匠）规则星，F7 面板全中文。
**不依赖 RuffnecKk MapSense**：没装它时由本包自带的独立叠加层接管，界面与装了它时完全一样。

> **免责声明**：本插件仅供**单机/离线模组**环境使用，请勿在战网等联网环境使用，
> 由此产生的一切后果自负。

## 运行前提

| 组件 | 说明 |
|---|---|
| **D2R 3.2.x**（3.2.92777 实测） | 内存偏移按这个版本校准，大版本更新需重新校准 |
| **D2RLoader 1.2.x** | 插件加载器（`d2rl-*.dll` 的宿主） |
| RuffnecKk MapSense 2.x | **可选**。装了 → 星标与面板画在它的叠加层里；没装 → 本包的独立叠加层接管 |
| RuffnecKk Floating Damage 1.5.0 | 可选，同作者，实测共存良好 |

**两种模式（自动切换，无需任何配置）**

- **装了 MapSense** → 星标与面板画在它的叠加层里，独立叠加层**自动完全停用**，一切照旧；
- **没装 MapSense** → **独立叠加层**（`d2rl-loot-map-standalone.dll`）自己充当那层画布。
  画的还是主插件同一份绘制代码，所以界面必然一致。

## 功能

- **星形标记**：暗金/套装掉落物在地图上显示为立体星（暗黑3风格渐变，也可切简约样式）
- **蓝装前缀规则**：带「珠宝匠」/「工匠」前缀的 4 孔蓝装显示规则星（前缀 ID 判定，
  不会误认「工匠之」；换模组后如编号变化，用 `tools/diff_starid.py` 重新校准）
- **隐藏引擎自带标记**：没勾的品质，地图上**连引擎自己画的 X 和物品名字都不显示**——
  只剩你勾选的品质的星（`loot-map.toml` 里 `hide_engine_shape = false` 可关掉此行为）
- **面板（F7）**：总开关 / 品质（暗金、套装）/ 规则 / 星星（样式、大小 12~36、描边、
  左右与上下微调），改动即存
- **颜色**：每类 14 色预设色板 + 完整调色板
- **字体**：启动时自动把微软雅黑注册进字体图集（在游戏建库之前），面板中文统一字型
- **挡鼠标**：光标在面板上时吞掉点击与滚轮，人物不会跟着动

## 安装

1. 关闭游戏
2. 把 `release\` 里的**两个 dll** 复制到 `<游戏目录>\d2rloader\plugins\`：
   - `d2rl-loot-map.dll` —— 主插件（**必须**）
   - `d2rl-loot-map-standalone.dll` —— 独立叠加层（没装 MapSense 的人必须；
     已经装了 MapSense 的人可以不放，放了也会自动让路、不冲突）
3. （可选）`loot-map.toml` → `<游戏目录>\d2rloader\config\`
4. 进游戏按 **F7** 打开面板

## 包里文件说明

| 文件 / 目录 | 是什么 |
|---|---|
| `release/d2rl-loot-map.dll` | **主插件**：读掉落、管设置、画面板和星。两个 dll 之一，必须放 |
| `release/d2rl-loot-map-standalone.dll` | **独立叠加层**：没有 MapSense 时自己充当那层画布；装了 MapSense 时自动停用 |
| `release/loot-map.toml` | 默认配置模板（可选；不放的话插件会自动生成一份） |
| `release/使用说明.txt` | 发给朋友的简版说明（安装三步 + 常见问题） |
| `src/loot_map_plugin.cpp` 等 | 主插件源码 |
| `src/standalone/` | 独立叠加层源码（实现 MapSense 兼容的宿主接口） |
| `tools/` | 开发期工具：字节校验（verify_v0xxx.py）、小桩布局核对（stub_layout_probe.py）、蓝装前缀校准（diff_starid.py）等 |
| `third_party/` | ImGui（MIT）、MinHook（BSD 系），各自带许可证文件 |
| `CHANGELOG.md` | 更新记录（每个版本改了什么） |

## 使用

- **F7** 开关面板；改完自动保存，下次进游戏还是这个样
- 日志：`<游戏目录>\d2rloader\logs\loot-map.log`（主插件）、
  `loot-map-standalone.log`（独立叠加层）。排查问题主要看这两个。

## 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| F7 弹出的是**老式面板**（小方框 + 色块）、地图上没有星 | **两个 dll 没都放**：主插件 + 独立叠加层要一起放（或只放了主插件且没装 MapSense） |
| 面板显示**英文** | 中文字体没加载上（少见）。把 `logs\loot-map.log` 发给作者 |
| 按 F7 **完全没反应** | 看 `logs\loot-map.log` 里 `Overlay panel:` 开头的行 |
| **「珠宝匠/工匠」蓝星不亮或不准确** | 前缀编号按 当时实测所用模组 模组实测（1209/1210）；换模组后用 `tools/diff_starid.py` 重新校准 |
| 星的位置或大小不满意 | 面板「星星」区：大小滑条 + 左右/上下微调（对准一次自动保存） |
| 手改了 `loot-map.toml` 又被面板覆盖 | 面板上动任何控件都会按内存值重写配置文件 —— 请在面板上改 |

## 构建

- Windows + MSVC（x64）+ CMake + Ninja
- 需要 [D2RLoader PluginSDK](https://github.com/) 放在 `../d2rloader-pluginsdk`
  （或用 CMake 变量 `D2RL_SDK_DIR` 指定路径）。**SDK 不包含在本仓库**，请自行获取
- `third_party/` 内已含 ImGui 与 MinHook，无需额外下载

```powershell
cmake -B build-ninja -G Ninja
cmake --build build-ninja
# 产物：build-ninja/stage/d2rl-loot-map.dll 与 d2rl-loot-map-standalone.dll
```

## 已知限制

- rule0 蓝装前缀判据（前缀 ID 1209/1210）实测于 当时实测所用模组 模组；原版或其他模组可能不同
- 游戏大版本更新可能改变内存偏移，需要重新校准（tools/ 里是全套校准脚本）
- 「隐藏引擎自带标记」由 `hide_engine_shape`（默认开）控制；关掉后会重新看到
  引擎为物品画的 X 与名字

## 更新记录

见 [CHANGELOG.md](CHANGELOG.md)。

## License

本插件源码以 **MIT** 发布；第三方组件见仓库根目录的
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)。

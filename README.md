# D2R 插件集（D2RLoader Plugins）

暗黑破坏神 II：重制版（D2R）的 **D2RLoader 插件**集合。
每个插件一个文件夹，自含**源码、成品、使用说明与更新记录**，互不干扰；后面还会陆续加入更多插件。

> **免责声明**：所有插件仅供**单机/离线模组**环境使用，请勿在战网等联网环境使用，
> 由此产生的一切后果自负。

## 插件列表

| 插件 | 一句话说明 | 链接 |
|---|---|---|
| **[loot-map](plugins/loot-map/README.md)** | 掉落物地图标记：暗金/套装在地图上显示为星；**不依赖 MapSense**（内置独立叠加层），F7 全中文面板 | [成品下载](plugins/loot-map/release/) · [使用说明](plugins/loot-map/README.md) · [更新记录](plugins/loot-map/CHANGELOG.md) |

## 通用安装方法

1. 关闭游戏
2. 把插件 `release\` 文件夹里的 `.dll` 复制到 `<游戏目录>\d2rloader\plugins\`
3. （可选）把 `.toml` 配置模板复制到 `<游戏目录>\d2rloader\config\`
4. 进游戏，按各插件自己的热键（如 loot-map 是 **F7**）

个别插件需要放**不止一个 dll**（比如 loot-map 主插件 + 独立叠加层），以各插件 README 为准。

## 补丁（内存修改 JSON）

`patches\` 文件夹里放的是 D2RLoader 的**内存补丁文件**（不是 dll 插件）：
复制到 `<游戏目录>\d2rloader\patches\`，**完全退出游戏再进**才生效；删掉该文件即完全还原。

| 文件 | 干什么 | 注意 |
|---|---|---|
| `Diablo-Spawn-FinaleOff-TEST.json` | **大菠萝免祭坛（保杂兵）**：进入混沌避难所（或随手开任意 1 个封印）大菠萝直接现身，不再需要「开满 5 个封印 + 杀 3 名守护者」；且出场时**不清杂兵** | 保持原样时，第四幕「恐怖终结」任务可能不推进；想恢复"出场清杂兵"，按文件里的说明删掉最后一条即可（文件里写得非常细）。⚠️ **浮动伤害插件必须是 1.5.0**：1.4.2 认不出新版地图插件、会和它抢画面（表现为"开补丁后飘字/地图二缺一"，换回别的补丁又正常），**升级到 1.5.0 后一切正常**（本补丁对两个插件零引用，不是补丁的锅） |
| `auto-identify-items.json` | **自动鉴定**：魔法/稀有物品掉落时即已鉴定，不再需要鉴定卷轴 | 与官方 D2RL-Plugins 的 magicItemsSpawnIdentified / rareItemsSpawnIdentified 同效 |

> ⚠️ **同一主题只能有一个生效**：想换变体，必须「一个开、一个把扩展名改成 .off」——
> 两个同时开着会互相撞车，可能谁都加载不上。补丁只对游戏大版本 3.2.x 有效，
> 内存偏移随版本会变，失效了以 `expected` 字节对不上为准。

## 目录结构

```
plugins/
  loot-map/            掉落物地图标记
    src/               源码（主插件 + 独立叠加层）
    release/           成品（朋友直接下载这个文件夹里的 dll 即可）
    tools/             开发期工具（字节校验、反汇编、前缀校准等）
    third_party/       第三方库（ImGui、MinHook，各自带许可证）
    README.md          使用说明 + 文件说明
    CHANGELOG.md       更新记录（每个版本改了什么）
patches/             内存补丁 JSON（大菠萝免祭坛、自动鉴定，见上「补丁」节）
LICENSE                本仓库源码的许可证（MIT）
THIRD_PARTY_NOTICES.md 开源组件声明（用了谁、谢谁）
```

## 开源声明

本项目源码以 [MIT License](LICENSE) 发布；用到的第三方组件见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

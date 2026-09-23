// ===========================================================================
//  Loot Map —— 掉落物地图标记插件（D2RLoader 插件）v0.18.8
//
//  目标：让地面上的物品也出现在游戏自带的（小）地图上；同时提供一个游戏内设置面板。
//
//  ★ v0.14.0 变更（面板重做 + 修掉"星在屏幕中间就不见了"）：
//    用户反馈（按重要性）：
//      · "调整星星大小后星就不显示了 / 捡起来再丢下又显示了 / 打开物品栏就显示、
//        关掉就消失" —— 三个现象其实是一条：星只画"引擎当帧亲口报过"的那些，
//        而引擎并不是每帧都在报物品（它只在自己重绘地图时才报，所以打开物品栏 /
//        重新丢下物品时它又出现）。而且老代码还有个自家毛病：为了防止★压住
//        面板文字，规定"落在面板那一块矩形里的星不画"—— 面板默认就盖在屏幕
//        正中，地图上的星大多正好在那儿，于是整片消失（日志铁证：buffered=1
//        但 drawn=0，且那颗星的屏幕坐标正落在面板矩形内）。
//      → 三条一起改：① 记忆表**不再猜位移**（v0.13.0 那版"和上一帧配对求位移
//        中位数"就是"星标位置全错"的根因）：改存"上次看到的屏幕坐标 + 当时的
//        相机偏移"，每帧用引擎自己放在 ctx 里的相机偏移算出**精确**平移量
//        （v0.12.7 已反汇编确认字段位置），位置不可能漂；② 面板那一块的星
//        改成**淡淡地透出来**，不再整个藏掉；面板关掉后立刻恢复；③ "引擎安静"
//        的清表阈值从 0.7 秒放宽到至少 8 秒 —— 引擎不报 ≠ 地图关了。
//      · "默认显示全部勾选 / 颜色要能自己选 / 预留自定义接入位 / 星星大小要留"
//        → 面板重做：八类品质默认全勾，每行右边一排**固定色板**（点一下即换色，
//        星是我们自己画的，所以一定生效）；预留 4 条"自定义规则"槽位（如蓝色
//        [珠宝匠]/[工匠]），判定函数接进去就生效，面板/配置不用动；星星大小做成
//        档位按钮 + 微调滑条（8~160，读配置时钳死，不再出现 0 或 200）。
//      · 配置新增：`engine_colour_rescue`（引擎色号兜底，默认开）、
//        `rule0..3_enable/_rgb/_name`（自定义规则槽位）。
//
//  v0.13.0 变更（三个反馈一次做齐：小绿星+灰块 / 显示范围 / 点面板人物也动）：
//    · 【小绿星 + 灰底块】真根因不是我们画的星，也不是大小设置 —— 是**引擎自己**
//      还给一部分物品画了标记，而那部分物品我们的 ReadItemQuality() 没读成套装/
//      暗金（于是既没画我们的星、也没拉起"跳过引擎绘制"的闸门）。
//      证据：用户截图上那几颗小星的像素是**贴图渐变绿**（rgb 128→253 都有），
//      我们画的星是纯色 #00FF00 / #FFD700；同时日志里 `diag: item / siteA /
//      shapeSkips` 三个计数完全相等（凡是走过的物品我们都拦住了）。
//      修法：取色钩子里，原始函数调用**之后**的 out1 = 引擎自己给这件物品选的
//      色号。拿"我们判得准的套装/暗金物品都用了哪个色号"去投票，学出引擎的
//      套装色号与暗金色号（argmax，要求票数≥4 且严格超过第二名 2 倍）；之后凡
//      引擎色号命中这两个值的物品，一律按套装/暗金处理（画我们的星 + 拉闸门）。
//      → 漏判的物品被救回来，引擎的小星和灰块就不会再出现。日志里看
//        `engSet= / engUniq= / rescued=` 三个数就能确认。
//    · 【显示范围】引擎只画它自己那一圈里的物品，走开一点星就没了；雷达倍率想
//      直接改引擎的地图缩放，实测**标定失败**（日志 cannot calibrate the zoom
//      field，matches=0）。改成"记住已经看到的星"：每帧用配对求出的整体位移
//      **中位数**把旧星跟着地图一起平移（新看到的补进表里，超时的丢掉）。
//      三道安全阀：① 引擎安静 0.7 秒（= 地图关了）→ 整表清空，绝不留假星；
//      ② 单帧位移 > 420px（换场景/传送）→ 清空重建；③ 超过"星标保留（秒）"
//      没再被引擎看到 → 逐颗丢掉。秒数做成了面板滑条（默认 60），
//      0 = 回到"只显示引擎当帧报的"。持久值存 toml `star_persist_sec`。
//    · 【点面板人物也动】先取证（活进程反汇编，见 tools/live_core_hookcalls.py）：
//      整个进程里只有 D2RCore.dll 的导入表里有 GetAsyncKeyState /
//      SetWindowsHookExW；而它用 GetAsyncKeyState 只轮询 **Tab(9) 与 Esc(0x1B)**
//      两个键（完全没有轮询鼠标键），读鼠标靠的是 **5 个 WH_MOUSE(=5) 钩子**
//      （idHook=5 / hMod=NULL / dwThreadId=游戏 UI 线程）。
//      所以最干净的做法是**我们自己也装一个 WH_MOUSE 钩子、装在同一个线程上**：
//      Windows 的钩子链"后装的先调"，我们排在游戏那 5 个钩子之前 → 面板矩形内的
//      按键消息直接 return 1，后面的钩子和窗口过程都收不到 → 游戏看不到这一下
//      点击。**一个字节的游戏代码都不动**，面板关掉 / 插件卸载就 Unhook 还回去。
//      面板挡鼠标 = 面板上的勾选框（默认开），配置键 `block_game_mouse`。
//    · 面板字体：改成"挑一块笔画统一的字体" —— 遍历宿主字体表，硬条件是有中文
//      字形（品）+ 数字字形，评分优先 ConfigDataCount ≤ 1（单一字体源 = 笔画
//      统一，合并图集才会看着不统一）、字号贴近 18。不缓存指针（宿主重建图集
//      后指针会失效），每次现挑。
//
//  v0.12.8 变更（按用户反馈的六件事，一次做齐）：
//    · 暗金 → **亮金**（#FFD700）：以前用游戏表的暗金色 #C7B377 太暗看不清。
//    · 面板状态行不再显示"已画星 N 颗"这个**累计值**（一直涨，看着像 bug），
//      改成"地图上星标 N 颗（套装 x / 暗金 y）"的当帧真实数量。
//    · 面板新增控件（改完即时生效并存盘，不用再手改 toml）：
//        星标大小滑条（4~200px，默认 88）、左右/上下微调滑条、
//        雷达倍率按钮 x1 / x2 / x5 / x10。
//      —— "star_size 改了不管用"的根因：面板上任何一次点击都会把整个 toml
//         按内存里的值重写一遍，用户手改的值会被覆盖；现在直接在面板上调。
//    · 字体一致性：以前固定 PushFont(我们自己找到的那个中日韩字体)，于是
//      面板字体和宿主菜单不一致。现在**宿主当前字体本身有中文字形就不 push**，
//      面板直接跟宿主界面同一套字体；只有缺字形时才退回我们找到的那个。
//      （诊断行里的 fonts=18/cjk=yes 就是依据。）
//    · ★不再盖在面板上：面板打开时，落在面板矩形里的星不画。
//    · 雷达范围（用户要"至少 10 倍"）：引擎每帧把"地图缩放"作为参数传给
//      sub_858510（我们小桩能拿到），该缩放同时存在于渲染上下文 ctx 的某个
//      float 字段里。做法：记录最近一次的 ctx + 缩放值 → 在 ctx+0xd4..0x200
//      里**自动标定**出唯一与缩放值逐位相等的 float → 每帧把该字段写成
//      原值/倍率（倍率 2/5/10 → 地图整体缩小，看得更远）。
//      安全阀：倍率=1 一个字节都不写；标定不到唯一匹配就完全不写，只留一行日志。
//
//  v0.12.7 变更（★★★ 修"星跑到屏幕右下角/位置错得离谱"——坐标解释从猜测变成
//      引擎同款公式，这是 v0.12.0 以来一直没做对的最后一块拼图）：
//    · 根因（活体反汇编 tools/live_probe_v0127.py，RVA 0x79DA50 = 引擎标记
//      渲染器）：引擎拿到 point 后是这样算屏幕坐标的——
//          x_最终 = point.x - ctx[0xc4]*缩放 + ctx[0xd0]*缩放
//          y_最终 = point.y - ctx[0xc8]*缩放 + ctx[0xc0]*缩放
//      其中 ctx = *(void**)obj（rcx 传进来的全局结构），缩放 = sub_858510 的
//      第 4 参数（xmm3）。而 v0.12.0~v0.12.6 一直用"屏幕中心 + point"这种
//      没有依据的假设 → point 本体是"地图坐标系"的值（量级几百），加上
//      屏幕中心直接飞到右下角。跟用户实测完全吻合（星在右下角、物品在左上）。
//    · 修法：小桩 call 进钩子时寄存器原样保留 → 钩子声明第 4 个 float 形参
//      拿到缩放，从 obj 读 ctx（0xbc~0xd0 六个 int32），**记录时就按引擎公式
//      换算好真实屏幕坐标**（StarScreenPos）；叠加层直接画，不再加中心。
//    · toml 新增 star_coord_mode（0=point原值 / 1=引擎换算[默认] / 2=旧版中心+point）
//      和 star_coord_debug（在 point 原始位置画白色小十字，一次截图即可对照
//      三种解释；确认无误后可改 false 关掉）。
//    · 面板：应用户要求**去掉颜色选择列**（品质颜色已固定：套装绿/暗金暗金）。
//
//  v0.12.6 变更（★★★ v0.12.5 实测还是翻车：星照样没画、灰方块照样在。活体
//      反汇编（tools/live_probe_v0126.py）+ 时间线复盘终于钉死全部真相）：
//    · 真相一：DrawBlob(0xD6DB0) **自己一个字节都不画** —— 它只是算坐标，然后
//      call sub_858510（A 点）。所以"灰底块是另一笔直连调用"的猜想里，剩下两个
//      候选（B=0xD7354 / C=0xD2DF5）v0.12.4/v0.12.5 各补过一次，灰块都还在
//      → 两条直连调用对物品标记**都不执行**；同时 v0.12.4/12.5 会话里
//      "Overlay stars" 零条 → 星坐标记录断供，叠加层一颗星都没画，
//      **用户看到的小图形 + 灰块全是引擎自己画的**（star_size=88 因此"无效"）。
//    · 真相二（灰底块真身）：F1 里 DrawBlob 调用之后还有一排**跳转表形状分支**
//      （0xD77D4 jmp rcx → 0xD6AA0/0xD6B20 等形状绘制函数），画的是取色 out2
//      选的"额外图形"。v0.12.2 把 out2 从"写死 -1（不画额外图形）"改成读配置
//      shape[slot]，而 shape_set/shape_unique 默认= 0 → v0.12.3 起引擎每个
//      套装/暗金标记都多画一个 0 号图形 —— **这就是灰色方块**，时间线完全吻合。
//      → 修法：星品物品且 hide_engine_shape 时**强制 *out2 = -1**（跳转表走
//        "不画"分支），灰块从原理上消失，不用再猜哪个调用点。
//    · 真相三：星坐标记录退回 **A 点**（v0.12.3 实测有效的那条路）——
//      HookBlobIconPrep 恢复"消费 starSlot → 记坐标 → 跳过绘制"的完整逻辑；
//      C 点钩子保留作后备（pending 单令牌，谁先抢到谁记，不会画双星）。
//    · 新增诊断计数器 + 每 5 秒一条限流日志（item 取色数 / A 点命中 / C 点命中 /
//      out2 强制次数 / 已画星数）—— 再出问题，一条日志定位断点，不用再猜。
//    · star_size 一直是读配置的（叠加层 radius = g_settings.starSize），
//      之前"调 88 无效"纯粹因为星管线断供；本版恢复后即生效。
//
//  v0.12.5 变更（★ 被本版推翻：当时以为物品标记走 F2，把记坐标+跳灰块挪到
//      C 点 0xD2DF5 —— 实测 C 对物品同样不执行，星管线依旧断供）。
//
//  v0.12.4 变更（★★ 补错了函数，被 v0.12.5 推翻）：当时以为灰底块是 F1 里的
//      0xD7354 直连调用，把"记坐标+跳灰块"挪了过去 —— 实测那条路对物品不执行，
//      星星彻底消失。其双站点小桩机制（kBlobPatchSites[] + 各自钩子 + 闸门）
//      是对的，v0.12.5 沿用，只是把站点 B 换成 C。
//
//  v0.12.3 变更（★★ 用户要求"既然有★就别显示 X"——v0.12.4 发现只拦了一笔）：
//    · 日志铁证（v0.12.2 实测）：BLOB-ITEM color=2（洋红 X）、color=7（金黄 X）
//      与用户截图逐一吻合 → **引擎形状的颜色只认"色号"（out1），改样式 r/g/b
//      对它完全无效**。给 X 染色的路（v0.12.2）彻底堵死。
//    → 做法：小桩加 5 字节分支 —— 钩子发现是"画星的物品"就返回 1，小桩就地
//      ret 回 DrawBlob，**引擎一个字节都不画**；坐标已在钩子里记下，地图上
//      只剩叠加层的实心★（颜色 = 面板/配置里的 rgb，所见即所得）。
//      非 star 物品照旧走原函数（本版本只有套装/暗金，即全部只画星）。
//    · 新配置 hide_engine_shape = true（改成 false 可回退到"引擎照画"的旧样子）。
//
//  v0.12.2 变更（★ 修 X 盖不住、选色无效、调色器被面板压住；X 同色方案被 v0.12.3 否定）：
//    · X 与星同色：实测星画出来了（已画 3 万+颗）但引擎形状还是"色号"颜色 ——
//      说明 prep 钩子触发时 g_pendingRgb 已经不在（两段式渲染：取色在先、绘制在后，
//      中间隔了别的单位），而**星槽位还在**（星能正好落在 X 上就是证据）。
//      → HookBlobIconPrep 里只要发现本次是"我们要画星的物品"（starSlot>=0），
//        就直接用该槽位的 rgb 改写样式 r/g/b —— 不再依赖 pendingRgb 那条窄路。
//        X 与星同色后，即使星的五个瓣盖不住 X 的四个角，看起来也只剩星星。
//    · 调色器不再用弹窗（弹窗是独立的 ImGui 窗口，在宿主"纯显示"叠加层里
//      层级/hover 都不可靠，被面板压住 → 用户点不到 → 以为"选颜色无效"）。
//      改成**面板内嵌**的 ColorPicker3（套装/暗金各一个，摆在品质表下面），
//      没有第二扇窗，就没有层级问题；拖动即改即存。
//    · 星默认半径 12 → 16（用户反馈"星星太小了"；toml star_size 仍可调）。
//
//  v0.12.1 变更（★★ 修复 v0.12.0 三件事：星星没画、颜色不对、面板选色不对）：
//    · 根因（日志铁证）：每次加载都 WARN "the blob draw call cannot reach the
//      stub"。光点小桩的内存是 VirtualAlloc(nullptr,...) 要的 —— 系统从高处
//      随便给一页，离 D2R.exe 里那条 call 常常超过 ±2GB，E8 相对跳转的 32 位
//      位移装不下 → 补丁失败 → HookBlobIconPrep 永远不被调（星标坐标 0 条、
//      真彩全灭），地图上只剩引擎默认图形 + 色号颜色（X 形、洋红/黄的来源）。
//      → 修法：在调用点 ±1.75GB 内按 64MB 步进逐个地址尝试 VirtualAlloc，
//        都失败才退回系统默认（并保留原有超范围警告）。
//    · 颜色列改成**直接调色**：引擎 13 色表的编号→颜色对不上（实测 0=蓝、2=紫，
//      表里写 0=白、2=亮绿 —— 选"亮绿"出来洋红）。现在颜色按钮直接绑
//      rgb_* 三通道，弹 ImGui ColorPicker3，所见即所得（星形和底点一起变）。
//    · 字体兜底：宿主字体里没有 ★ 字形（渲染成 ◆），界面文字全部去掉 ★；
//      另外选字体时同时检查 '0'，缺数字的字体不让中文状态行上（退英文）。
//
//  v0.12.0 变更（按用户要求简化：只显示套装+暗金，画实心★，绿/暗金两色）：
//    · 只显示两类：套装（绿 0,255,0）、暗金（暗金 199,179,119）——
//      这两个 RGB 都是引擎 13 色表里原样存在的值（距离 0，无就近吸附偏差）。
//    · 实心★星标：反汇编确认（活进程实读，tools/live_dis_drawblob_full.py）——
//      DrawBlob(0xD6DB0) 在形状参数 == -1 时**整个直接返回**（cmp edx,-1 / je ret），
//      游戏自带的 6 种标记形状全部走 sub_D6B20，没有星形可挑；
//      而 sub_858510(obj, point, style) 的 point 是指针，point+0/+4 就是引擎
//      算好的标记屏幕偏移（两个 int32，来自 point.x - (半宽)*缩放 / point.y - (半高)*缩放）。
//      → 做法：套装/暗金照旧让引擎画底点（形状 0，颜色走既有 RGB 通道），
//        HookBlobIconPrep 里把 point+0/+4 的坐标记下来，叠加层每帧用
//        GetForegroundDrawList() 在 假设位置 = 屏幕中心 + 记录偏移 处画一个
//        更大的实心★（10 个三角形扇形拼成）盖住底点。星标失败最多"位置不对"，
//        底点永远在 —— 不会比 v0.11.3 更差。
//      · 坐标变换带 star_offset_x/y 兜底参数 + 每 ~2s 打一行星标采样日志，
//        万一位置有偏差，从日志就能算出正确偏移，不用猜。
//    · 面板精简：只留 套装/暗金 两行（显示 + 颜色），去掉"图形"列，
//        顶部加"显示星标"总开关（配置键 stars）。
//
//  v0.11.3 变更（★★ 面板能画出来了，但"点不动、拖不动"：把鼠标接管过来）：
//    · 现象：v0.11.2 的面板真的画出来了（用户截图），可是**不能拖动、点击
//      穿透到游戏、点勾选框没反应**。
//    · 根因：宿主的叠加层是"纯显示"用的 —— 它算悬停时把鼠标当成不在窗口里
//      （ImGui 的 ImGuiConfigFlags_NoMouse 一旦置位，HoveredWindow 恒为
//      NULL，连"鼠标在谁身上"都不判）。而 ImGui 判定**任何一个控件能不能被
//      点到**，第一行就是 `g.HoveredWindow == 当前窗口`
//      （imgui.cpp ItemHoverable 第 4545 行）→ 全部控件永远点不到，
//      窗口自然也就拖不动。
//    · 做法：面板开着的时候由我们自己把鼠标喂进宿主的 ImGui：
//        ① 每帧用 Win32 取光标位置 + 左/右/中键，直写进宿主的 ImGuiIO；
//        ② 清掉 ImGuiConfigFlags_NoMouse（清之前先记下它原来的值，
//           面板一关就原样还回去 —— 对称还原，不给宿主留后遗症）；
//        ③ 鼠标落在本窗口范围内时，把这一帧的 HoveredWindow 指向本窗口
//           （只影响我们这一帧：宿主下一帧 NewFrame 会自己重算）。
//      → 为什么直写字段、**不用** io.AddMousePosEvent()：那个 API 会往宿主的
//        ImVector 事件队列里 push_back/扩容，而我们插件是 /MT 静态 CRT、
//        宿主是 /MD 动态 CRT（它的导入表里就是 MSVCP140/VCRUNTIME140）——
//        跨 CRT 的 realloc/free 会踩坏堆。写字段一个字节都不分配，绝对安全。
//    · 拖动自己实现：ImGui 原生拖窗口靠 io.MouseDelta，而直写鼠标时
//      ImGui 在 EndFrame 里把 MousePosPrev 设成了 MousePos（imgui.cpp 5592），
//      于是 delta 恒为 0 → 原生拖动拖不动。所以本插件用自己采到的真实坐标
//      搬窗口（窗口带 NoMove/NoResize，避免两套逻辑打架），并做了边界钳制，
//      拖不丢。
//    · 诊断：面板开着时每 ~2 秒在日志里打一行输入状态
//      （flags / hostNoMouse / 宿主自己的鼠标位置 / 我们写进去的位置 /
//      宿主认定的悬停窗口 / 被我们强指的帧数），一眼就能看出鼠标到底进没进来。
//    · 已知限制（下一步再攻）：点击**仍然会传到游戏**（人会走动）。
//      游戏读鼠标走的不是窗口消息（D2RCore.dll 走 SetWindowsHookExW +
//      GetAsyncKeyState，连 GetCursorPos 都不导入），所以拦不住；
//      要拦得挂钩游戏自己的输入读取，属于另一件事。
//
//  v0.11.2 变更（★★ 面板打不开的真凶：我们自己两份 imgui 的结构体布局不一致）：
//    · v0.11.1 的自检全部通过、面板也 `opened` 了，但下一行立刻是
//      "drawing raised an exception; permanently disabled" —— 说明卡在**绘制**里。
//    · 根因：`CMakeLists.txt` 只在 `imgui` 这个静态库上定义了
//      `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`，插件目标没有。
//      该宏删掉 ImGuiIO 末尾 3 个指针（GetClipboardTextFn /
//      SetClipboardTextFn / ClipboardUserData = x64 上 24 字节），
//      而 ImGuiContext 内嵌 ImGuiIO → IO 之后所有成员偏移整体差 0x18。
//      于是：插件自己读 `c->WithinFrameScope` 等字段恰好是**对的**
//      （插件没定义宏，与宿主一致），所以自检一路通过；
//      但 `imgui.lib` 里的 Begin / PushFont / GetCurrentWindow /
//      BringWindowToDisplayFront 按**错的**偏移去读宿主上下文 → 一画就异常。
//      日志表现完全吻合：校验通过 → opened → 同一毫秒 drawing exception。
//    · 实测确认宿主布局（tools/fmt_layout_probe.py + tools/dump_layout_region.py，
//      同时验了 MapSense 与 Floating Damage 两份 DLL，两者机器码逐字节相同）：
//      宿主 `ImGuiContext::ImGuiContext` 里的初始化序列是
//          mov dword [rdi+0x12b0], 0        ; FrameCount = 0
//          mov qword [rdi+0x12b4], -1       ; FrameCountEnded = FrameCountRendered = -1
//          mov byte  [rdi+0x12bc], 0        ; WithinFrameScope = false
//          mov qword [rdi+0x1368], 0        ; CurrentWindow = NULL
//      与 imgui.cpp 源码 3835-3837 行逐条对应 → 宿主用的是**默认布局**
//      （宏未定义），而我们的 imgui.lib 反而是定义了宏的那一份。
//      → 修法：把 `imgui` 目标上的宏去掉，全工程统一用默认布局。
//    · 防回退：third_party/imgui/imconfig.h 里加了 `#error`，谁再打开这个宏
//      就直接编译失败；插件里另加 static_assert 钉死宿主的那几个偏移。
//    · 诊断加强：绘制分 15 步，异常时日志直接报 "stage=N"（配 stage 文案），
//      再出问题一眼就能看出是哪一次 ImGui 调用炸的。
//
//  v0.11.1 变更（★ 修 v0.11.0 面板打不开的真凶 + 对齐一个已经在跑的样板）：
//    · 【真凶】v0.11.0 的"布局自检"是**一次定生死**：第一次不符合就永久
//      kStateDead。而宿主的第一个回调 cb0 是在它**刚开始初始化叠加层**时
//      打过来的 —— 那一刻 ImGui_ImplDX12_Init / ImGui_ImplWin32_Init 还
//      没跑，io.BackendRendererName / BackendPlatformName 都还是 nullptr，
//      于是面板在注册完成后的第 1 秒就被判了死刑，之后每帧都没机会再验。
//      游戏日志实证：
//        [19:28:23.170] [WARN] Overlay panel: the host ImGui context does not
//        match the imgui build we shipped (layout mismatch); ... permanently disabled
//      现在改成**可重试**：失败只累计计数并留痕，连续 2400 次（约 20 秒）
//      仍不满足才放弃。另用 tools/io_probe 实测过：我们和 MapSense 的
//      ImGuiIO 偏移完全一致（PlatformName 0x88 / RendererName 0x90），
//      所谓"布局不匹配"根本不成立，纯粹是抢跑了。
//    · 【对齐样板】RuffnecKk Floating Damage 1.5.0 是**已经在跑**的叠加层
//      客户端（同一套 MapSense、同一版 Dear ImGui 1.91.5，日志写着
//      "rendering through the priority MapSense ImGui host"）。反汇编它的
//      registerClient 调用可知它填 cb0 / cb1 / cb2 / **cb4**，**cb3 留空**，
//      画东西的是 cb4。我们原来 5 个全填，宿主就把 cb3 也叫起来了
//      （每帧一对 cb3+cb4）—— 多出来的那个只会让日志变吵。现在 cb3 留空，
//      与它完全一致。
//    · 索引宽度自检收紧：只有**确认**不一致才判死；读不到 / 出异常一律当作
//      "这帧没数据"，重试到底。也不会再把"一整段全是 0 的缓冲"误判成 32 位。
//    · 日志加了 self-check 读数（renderer= / platform= / fonts= / frame=）
//      与"回调不在帧内"的明确告警，一次跑就能定位卡在哪。
//
//  v0.11.0 变更（★ 设置面板换成"跟地图插件同一种"：真正的 ImGui 窗口）：
//    · 前情：v0.8~v0.10 的面板走的是游戏自带的**原生 UI 布局 JSON**。
//      那条路能开、能点，但它本质是"精灵图 + 固定坐标"：按钮多大由贴图决定、
//      文字多大由 style 决定、怪一点就叠在一起。实测截图证明它很丑，
//      而且和用户要的"跟地图插件一样的面板"完全是两码事。
//    · 现在改走**宿主叠加层**：地图插件（MapSense）在 registerClient 里
//      早就把它的 **Dear ImGui 1.91.5 上下文**交给我们了（v0.6.0 已验证
//      ACCEPTED，回调 cb0/cb3/cb4 正常触发）。我们带上**同一个版本**的
//      imgui 源码（third_party/imgui = 1.91.5），用
//      ImGui::SetCurrentContext(宿主上下文) 之后所有 ImGui 调用都作用在
//      它那份上下文上，窗口由宿主的渲染管线画出来。
//      → 全程不碰 DirectX 交换链，所以结构上不可能再出现 v0.3.4 那种
//        "抢渲染主人"的崩溃。
//    · 面板内容（跟地图插件一个风格）：总开关 / 观察-上色模式切换 /
//      八档品质一行一个勾选框 + 颜色色块（点开是引擎那 13 色表）+
//      图形下拉框 / 全部开启·全部关闭·恢复默认 / 实时状态。改动即时存盘。
//    · 安全四道闸（任何一道不过就永久放弃，只写日志、绝不影响游戏）：
//      ① 布局校验：按我们自己的结构体偏移去读宿主上下文里的
//         io.BackendRendererName / io.BackendPlatformName，必须正好是
//         "imgui_impl_dx12" / "imgui_impl_win32"。只有两份 imgui 的
//         结构体布局完全一致才可能同时满足 —— 不一致就在这里拦住。
//      ② 索引宽度自检：拿宿主上一帧画好的索引缓冲反推它的 ImDrawIdx
//         是 2 字节还是 4 字节，必须跟我们这份一致。这是唯一"能单方面
//         改、又会读越界崩游戏"的错配（对方 4 字节、我们 2 字节去写，
//         它的渲染器就会按 4 字节读到分配区外面去）。
//      ③ 帧内校验：ImGuiContext::WithinFrameScope 为真（只能在
//         NewFrame..EndFrame 之间建窗口）+ FrameCount 保证一帧只画一次。
//      ④ SEH：整段读取与绘制包在 __try/__except 里，出一次异常就永久停用。
//    · 原生面板**保留**作为退路：万一 MapSense 不在场（宿主接口拿不到），
//      按键仍然会打开原来的原生面板。
//
//  v0.10.0 变更（面板补齐：颜色 + 形状都能在游戏里改，且看得出当前选了什么）：
//    · 每个品质一行，现在是「[勾选框] 品质名 [7 个形状按钮] [13 个色块]」，
//      也就是配置里支持的设置全部都能在面板上改，不用手改 toml。
//    · 形状按钮来自引擎自带的 6 种地图图形（0..5）+「不画」(-1)，
//      对应配置键 shape_<品质>。
//    · ★ 当前选中的形状会显示成黄色高亮块——SDK 既不能改控件文字、
//      布局里也没有 visible 字段，所以只能"每种图形各配一个高亮块，
//      运行时只把当前那个设成可见"，走 WidgetService 的
//      findPanel / findWidget / setWidgetVisible。
//      这个调用失败的最坏结果是 7 个高亮块全亮（难看但能用），
//      所以只记日志、不中断。
//    · 面板变宽了一行装不下，所以色块从 58px 收窄到 44px 给形状腾位置。
//    · 说明：布局不热重载，改布局必须重启游戏；但**改设置不用**（即时存盘）。
//
//  v0.9.0 变更（两个真问题：一个让面板能被打开，一个让任意颜色真生效）：
//    · 【按键】旧版的 F7 是"自绘面板时代"的遗留，随面板停用一起没了；
//      而 v0.8.0 的原生面板只能靠控制台命令打开 —— 游戏「控制」菜单里
//      根本没有这一项，所以玩家按 F7 无反应、也找不到可改的键。
//      现在用 SDK 的 InputService 把"打开/关闭面板"注册成**游戏原生动作**：
//      出现在 设置 → 控制 里，默认 F7，玩家可自行改键，绑定存进
//      input-bindings.toml 的 [bindings."loot-map/toggle-panel"]。
//    · 【真彩修复】v0.7.0/v0.8.0 的"任意颜色"其实**从来没生效过**：
//      日志里一直是 "the code stub is out of rel32 range; true colours are off."
//      原因是我给机器码小桩用了"相对跳转(rel32, ±2GB)"，而插件 DLL 与游戏
//      主体在 64 位地址空间里相隔几十 GB，够不着。
//      现在小桩改成绝对地址指令（mov rax,imm64 ; call/jmp rax），
//      放在内存任何位置都能用；并且完整保存/还原所有易失寄存器，
//      保证原函数拿到的参数一个字节都没变。
//      → 绿、金、任意 RGB 现在才是真的画得出来。
//
//  v0.8.0 变更（★ 游戏内设置面板回来了 —— 而且这次是"亲生的"）：
//    · 前情：v0.3.4 起面板是停用状态。原因是自绘面板要自己抓交换链，
//      和 MapSense 抢 DirectX 12 拥有者，游戏启动 0.5 秒内必崩。
//    · 这次改走加载器**原生面板**这条路：插件把一份 JSON 布局注册给
//      D2RLoader（registerResource + registerPanel），由游戏自己的 UI
//      系统把它画出来。全程不碰 DirectX / ImGui，
//      从原理上就不可能与地图类插件冲突。
//    · 面板内容：8 档品质的显示开关 + 每档 13 个色块（点哪个就是哪个，
//      直接写入 rgb_* 配置）+ 观察/上色模式切换。按 Esc 关闭。
//    · 注意：加载器不热重载布局 JSON，改布局必须重启游戏才生效。
//
//  v0.7.1 变更（把 v0.7.0 的任意颜色落到"真能用"上）：
//    · 查清了引擎做"就近匹配"的那张表：sub_90E5D0(RVA 0x90E5D0) 循环 13 次，
//      表在 [`[RVA 0x34462F8] 的全局对象 + 0x228`]。13 个颜色就是暗黑自己的
//      地图配色（白/红/亮绿/蓝/暗金/灰/黑/亮金/橙/黄/暗绿/紫/绿）。
//      → 也就是说"任意 RGB"最终会落到这 13 色里最近的一个，而这 13 色
//        刚好覆盖全部物品品质，所以视觉上和地图插件一致。
//    · 默认配色改为直接命中表内原样颜色（距离 0，零偏差）：
//        普通/超强 白 · 魔法 蓝 · 稀有 黄 · 套装 绿 · 暗金 金 · 手工 橙 · 未知 灰
//    · 修掉一个会让颜色偏一档的浮点坑：引擎是 (int)(通道*255) **截断**取整，
//      写 208/255.0f 可能算出 207.99998 → 截成 207。现在统一加 0.25 再除，
//      并顺带让纯白不再等于 (1,1,1) 那个"请用色号"哨兵值。
//    · 新增诊断：彩色取用口如果"上一条还没被取走就被覆盖"，计数 +1。
//      这个数不为 0 就说明游戏是"批量问完颜色再统一画"，单变量会串色；
//      控制台 lootmap-status 能直接看到。
//
//  v0.7.0 变更（★ 任意颜色 —— 用户要的"跟地图插件一样的配色"，做到了）：
//    · 反汇编 + 活进程逐条读出的结论：游戏自己画光点的函数
//      sub_858510(RVA 0x858510) 拿到的样式结构里本来就带 r/g/b 三个浮点，
//      只有当它们**全等于 1.0** 时才回退去查那 8 个色号；只要不是 (1,1,1)，
//      它就 `(int)(通道 * 255)` 打包成真正的颜色。而 DrawBlob(RVA 0xD6DB0)
//      写死的就是 (1,1,1)。
//    · 于是把 RVA 0xD6EE3 那条 `call sub_858510` 接到本插件：物品要画时
//      改写那三个浮点。**绿、任意 RGB 从此都能画**，
//      而且绘制全程仍在游戏自己的管线里 —— 不碰 DXGI / ImGui，
//      与地图插件（MapSense）从原理上不可能冲突。
//    · 配置新增 `rgb_<品质> = "R,G,B"`（0-255，也支持 #RRGGBB），
//      设了就覆盖 color_*；写 "off" 就退回用色号。
//    · 顺手止住日志刷屏：以前进游戏会自动开 30 秒"地面物品监视"，
//      每 250 ms 枚举一次（本版 loader 的 Ground 掩码恒返回 0 条），
//      30 秒能刷出几万行；现在不自动跑，需要时用 lootmap-probe。
//
//  v0.6.0 变更（奔着"跟地图插件（MapSense）一样"去：任意颜色 + 自定义图标）：
//    · 静态逆向 d2rl-ruffneckk-mapsense.dll 发现它导出未公开接口
//      RuffnecKkMapSenseGetOverlayHostApi，让别的插件把内容画进它那一层
//      （只有一个渲染主人 → 不会再出现前两次那种"抢 DirectX 12 拥有者"
//      的崩溃）。结构布局见文件后半段「Overlay host 探测」的注释。
//    · 本版只做第一步：开机注册一个"什么都不画"的客户端并反复重试，
//      把宿主交回的上下文（含它自己的 Dear ImGui 1.91.5 上下文、视口尺寸）
//      与回调触发情况写进日志。不绘制任何东西，失败也只是几行日志。
//    · 地图上的品质光点行为不变（shape 默认 -1，即不额外画装饰图形）。
//
//  v0.5.0 变更（地图标记「换图形」—— 用户要自定义图标）：
//    · 取色函数的第 3 个参数 out2 实测是**图形选择器**（反汇编跳转表
//      @ RVA 0xD78CC，共 6 个分支）。以前一律传 -1（不画额外图形），
//      现在做成可配置：每个品质一个 shape_* 项，-1 = 不画、0~5 = 六种图形。
//    · 关键点：这些图形由**游戏自己**绘制、位置就用该单位自己的坐标，
//      所以完全不碰渲染层，**不存在"和地图插件抢渲染"的问题**
//      —— 这是"自定义图标"最便宜、最安全的路线。
//
//  v0.4.1 变更（修「暗金不显示」）：
//    · 根因：配置键表 kRows 的顺序（unique, set, rare, …）与内部槽位
//      SettingSlot（normal, superior, magic, …）对不上，导致
//      `show_unique` 实际写进了"普通"槽、`show_superior` 写进了"暗金"槽。
//      于是「暗金 = 关、白装 = 开」，正是用户看到的现象。
//      修法：把 kRows 重排成与 SettingSlot 完全一致。
//    · 色号上限 38 → 7：活进程反汇编确认 DrawBlob 入口是
//      `cmp edx,7 / jbe`，游戏只认 0~7；调用方另外跳过 1 和 4，
//      所以真正可用只有 0/2/3/5/6/7 六个。
//    · 每档先给一个互不相同的色号（当作一次"色号对照表"实测）。
//    · 新增取证：最早见到的 5 个地面物品单位各 dump 一次
//      「单位结构 + 物品数据」原始字节，用来找底材/孔数/词缀字段。
//    · 地面物品枚举加一组对照（不带容器过滤）。
//
//  v0.4.0 变更（技术路线切换的第一步：探测官方 SDK）：
//    · 发现 D2RLoader 自带官方插件 SDK（18 个服务：Panel / Input / Item /
//      Inventory / DataTable / Localization / Lifecycle / Widget ...），
//      物品代码、部位、品质、孔数、词缀、鉴定/无形、物品等级、坐标
//      全部有官方只读接口 —— 不必再啃内存。
//    · 本版加入「服务探测」：加载时扫描 18 个服务是否可用并写日志；
//      注册数据表加载 / 玩家就绪两个生命周期监听；提供 lootmap-probe
//      控制台命令，把地面物品的全部字段与关键数据表的行结构写进日志。
//    · 现有「地图上色」逻辑与钩子完全保留，探测部分只读、可整段删除。
//
//  v0.3.6 变更（用户报"无效"的根治）：
//    · 根因 1：读取品质的函数把"偏移 <= 0"当成"还没配置"，而实测偏移
//      就是 0 → 每个物品都被判成品质未知 → 永远不画。改成 `< 0` 才拒绝，
//      并且只取那一个字节（后面几字节混着别的字段）。
//    · 根因 2：automap-blob 的取色桩只用"怪物"逻辑，对物品返回不画。
//      v0.3.5 靠"改桩入口字节"绕，会让桩结尾恢复 rbx/rsi 的固定偏移
//      错开 8 字节（隐患）。改成把 dll + 0x8178 里保存的取色函数指针
//      直接换成我们的函数：桩照常普通 call，栈帧天然正确，怪物逻辑
//      一个字节不动。
//    · 新诊断：桩每见到一个物品就把品质/色号写一行日志（前 8 个），
//      一次运行即可确认"物品有没有走进这条路径"。
//
//  v0.3.1 变更：
//    · 面板修好了：不再依赖被 MapSense 拦截的工厂钩子，改"事后抓取"
//      （自建诱饵交换链换共享虚表，游戏第一帧就会被接住）→ F7 可用
//    · 品质字段确认：物品数据第 0 字节（SDK Quality 枚举交叉验证），
//      默认配置改为上色模式，地面掉落直接按品质画上地图
//    · 渲染宿主的每一步进展都会写进插件日志，可诊断
//
//  v0.3.0 变更（应用户要求）：
//    · 面板从"游戏原生 UI 布局"换成 Dear ImGui 自绘窗口（MapSense 同款路线、
//      同款深色风格、微软雅黑中文字体），原生布局方案整体移除
//    · 观察模式日志按"单位"去重：同一个物品只记一次，不再每帧刷屏
//
//  原理（已逆向确认，见 docs/技术指南.md）：
//    游戏自带地图给每个单位画光点前会调用「取单位绘制颜色」函数（0xD78F0），
//    该函数对物品（type=4）直接返回"不画"。本插件挂在函数入口：
//      · 怪物等单位 → 原样转交原函数，一个字节都不动
//      · 物品       → 按配置决定"画 / 不画"以及用哪个色号
//    与地图插件 MapSense / automap-blob 的关系（反汇编确认可共存）：
//    它们改的是"调用点"，本插件挂在"函数本体"，串成一条链互不干扰。
//
//  两种工作模式（config 里的 mode）：
//    observe —— 只观察、只写日志，绝不改变游戏行为
//    color   —— 按配置真正上色（当前默认）
//
//  铁律：不写游戏目录、不改数据表、不动别的插件。
// ===========================================================================

#include <D2RLPlugin/api.h>

#include "render_host.h"

#include <imgui.h>
// 需要 ImGuiContext 的内部字段（WithinFrameScope / FrameCount）：
// 我们和宿主用的是同一份 1.91.5 源码，所以这些偏移两边一致。
#include <imgui_internal.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_set>

#include <windows.h>
#include <d3d12.h>

namespace {

// ─────────────────────────── 基本信息 ───────────────────────────

// 现役 D2RLoader 1.2.1-beta 能正常加载的插件声明的都是 ABI 2 / 3；
// ABI 4 只多一个 HTTPS 服务，本插件用不到，主动声明 ABI 3 求最大兼容。
constexpr std::uint32_t kPluginAbiVersion = 3;

constexpr D2RL::PluginFlags kFlags =
	D2RL::PluginFlags::Shared |
	D2RL::PluginFlags::NativeHooks;

constexpr D2RL::PluginInfo kPluginInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = kPluginAbiVersion,
	.id          = "loot-map",
	.name        = "Loot Map",
	.version     = "0.18.8",
	.author      = "local build",
	.description = "Shows set & unique ground items on the native automap as solid stars (green / bright gold).",
	.flags       = kFlags,
};

// ─────────────────────── 已确认的游戏地址 ───────────────────────
//  游戏版本：D2R 3.2.0 build 92777（宿主 D2RLoader.exe，基址 0x140000000）
//  RVA 从官方示例插件 automap-blob 的机器码里提取，装钩子前还有字节校验兜底。

constexpr std::uint64_t kGetUnitColorIndexRva = 0x000D78F0;
constexpr std::uint8_t  kGetUnitColorIndexExpected[] { 0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60 };

constexpr std::uint32_t kUnitTypeItem = 4;

// 色号范围：反汇编确认 DrawBlob 入口就是 `cmp edx,7 / jbe`（活进程实读），
// 说明游戏只认 0~7 这 8 个色号；调用方另外主动跳过 1 和 4（画不出来）。
// 所以真正可用的只有 0 / 2 / 3 / 5 / 6 / 7 这 6 个。
constexpr int kColorMin   = 0;
constexpr int kColorMax   = 7;
constexpr int kColorSkip1 = 1;   // 调用方主动跳过色号 1
constexpr int kColorSkip2 = 4;   // 和 4（反汇编确认）

constexpr int kMaxObserveUnitsHard = 5000;   // 观察模式最多记录多少个"不同"物品

// D2R 的品质编号（与 SDK 的 D2RL::Items::Quality 一致）
enum QualityCode : int {
	kQualityUnknown  = 0,
	kQualityInferior = 1,
	kQualityNormal   = 2,
	kQualitySuperior = 3,
	kQualityMagic    = 4,
	kQualitySet      = 5,
	kQualityRare     = 6,
	kQualityUnique   = 7,
	kQualityCrafted  = 8,
	kQualityMax      = 8,
};

// ─────────────────────────── 品质表 ───────────────────────────

enum SettingSlot : int {
	kSlotNormal   = 0,
	kSlotSuperior = 1,
	kSlotMagic    = 2,
	kSlotRare     = 3,
	kSlotSet      = 4,
	kSlotUnique   = 5,
	kSlotCrafted  = 6,
	kSlotUnknown  = 7,
	kSlotCount    = 8,
};

struct QualityRow {
	const char* key;      // 配置键（纯 ASCII）
	const char* label;    // ImGui 面板上的中文名（UTF-8）
};

// 注意：这个数组的下标 **必须** 与 SettingSlot 枚举一一对应
//（历史上这里是 unique/set/rare/... 的顺序，而 SettingSlot 是 normal/superior/...，
//  两边错位导致「配置里写的品质」落到别的槽上 —— 这正是「暗金不显示、
//  白装反而亮」的根因。2026-09-21 修正。）
constexpr QualityRow kRows[kSlotCount] {
	{ "normal",   "普通 / 低劣（白装）" },
	{ "superior", "超强（白装·带增强）" },
	{ "magic",    "魔法（蓝）" },
	{ "rare",     "稀有（红）" },
	{ "set",      "套装（绿★）" },
	{ "unique",   "暗金（金★）" },
	{ "crafted",  "手工（白）" },
	{ "unknown",  "未知品质" },
};

// ─────────────── ★ v0.14.0：面板上的固定色板 ───────────────
//  用户要求："颜色要能自己调，但不用调色盘，给几个固定颜色选就行"。
//  下面这些色**前 13 个全是引擎地图色表里的原色**（RVA 0x90E5D0 那张表，
//  见 Settings::rgb 的注释）—— 选它们不会有"被就近匹配到别的颜色"的偏差；
//  最后一个亮金 #FFD700 是 v0.12.8 给暗金用的、用户已经习惯的那个金。
struct ColourPreset {
	const char* label;   // 中文名（面板上不显示，做 tooltip / 无障碍用）
	int         r;
	int         g;
	int         b;
};
constexpr ColourPreset kPresets[] {
	{ "白",   255, 255, 255 }, { "红",   255,  77,  77 }, { "亮绿",   0, 255,   0 },
	{ "蓝",   105, 105, 255 }, { "暗金", 199, 179, 119 }, { "灰",   105, 105, 105 },
	{ "黑",     0,   0,   0 }, { "亮金", 255, 215,   0 }, { "橙",   255, 168,   0 },
	{ "黄",   255, 255, 100 }, { "暗绿",   0, 128,   0 }, { "紫",   174,   0, 255 },
	{ "绿",     0, 200,   0 }, { "青",    80, 200, 255 },
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

// ─────────────── ★ v0.14.0：自定义规则槽位（预留接口）───────────────
//  用户要求："预留后面各个用户自定义接入的接口（如蓝装那两个词缀等）"。
//  这里先摆好槽位：每条规则一个开关 + 一个颜色 + 一个可改的名字。
//  真正"什么条件才算命中"的判定集中在 RuleMatched()（当前只认贴图/名字类
//  判定还没接入，所以一律返回 false = 面板上显示"待接入"）。
//  以后接入"读物品名字/词缀"时，只要往 RuleMatched() 里加分支 ——
//  面板布局、配置文件、保存逻辑**都不用动**。
constexpr int kRuleCount = 4;
struct RuleRow {
	const char* key;       // 配置键前缀（纯 ASCII）
	const char* label;     // 面板默认显示名（UTF-8）
	const char* labelEn;
	const char* hint;      // 面板上那行小字说明
};
constexpr RuleRow kRuleRows[kRuleCount] {
	{ "rule0", "蓝色·珠宝匠 / 工匠", "Blue: Artisan's / Jeweler's", "已接入：4 孔蓝装（前缀 ID 1209/1210）" },
	{ "rule1", "自定义规则 2",        "Custom rule 2",               "留给你以后要加的" },
	{ "rule2", "自定义规则 3",        "Custom rule 3",               "留给你以后要加的" },
	{ "rule3", "自定义规则 4",        "Custom rule 4",               "留给你以后要加的" },
};

// ─────────────────────────── 配置 ───────────────────────────

struct Settings {
	bool enabled     = true;
	bool observeOnly = true;

	// 下标 = SettingSlot（0 普通 … 7 未知）
	// ★ v0.17.0：面板只放「暗金」「套装」两行，默认也只开这两类 ——
	//   其余六类槽位/配置键保留（接口在），配置文件里 show_* = true 随时能开回来。
	//   注意 rule0（珠宝匠/工匠）与「魔法」勾选互相独立，蓝装关了规则星照样亮。
	bool show[kSlotCount] { false, false, false, false, true, true, false, false };

	// 色号只能在 0/2/3/5/6/7 里挑（1 和 4 会被游戏自己跳过）。
	// 游戏内实测色表（已逐个肉眼确认）：
	//   0 = 蓝   2 = 紫   3 = 青蓝   5 = 红   6 = 白   7 = 金黄
	// 注意：这张表里**没有绿色**，也**只有一个金色**。所以
	//   「套装=绿」做不到，「暗金=金 且 稀有=黄」也无法同时成立（黄＝金，同一个号）。
	//   → 真正的绿/暗金由下面的 rgb_*（真彩通道）实现。
	int color[kSlotCount] { 6, 6, 3, 5, 2, 7, 6, 6 };

	// 图形（取色函数的第 3 个参数 out2）：-1 = 不画额外图形；
	// 0 ~ 5 = 游戏自带的 6 种标记图形之一。这是"换图标"的现成口子，
	// 走的是游戏自己的绘制管线，不碰渲染层、不会和地图插件冲突。
	// ★ v0.12.0：套装/暗金默认 0 —— 必须是非 -1，引擎才会真的画出来，
	//   我们的坐标钩子（HookBlobIconPrep）也才有机会拿到标记位置给★用。
	// ★ v0.14.0：**全部 0**。现在八类默认都显示，每一类都得让引擎先走一遍
	//   标记绘制，我们才拿得到那件物品的位置；标记本身会被闸门拦掉
	//   （hide_engine_shape），地图上只剩我们画的星。
	int shape[kSlotCount] { 0, 0, 0, 0, 0, 0, 0, 0 };

	// ★ 任意颜色（v0.7.0）：这一档不再用游戏那 6 个色号，而是直接给
	//   游戏一个 RGB。反汇编确认（见下方 HookBlobIconColourPatch 的说明）：
	//   光点绘制函数看到样式结构里的 r/g/b **全等于 1.0** 才去查色号表，
	//   只要不是 (1,1,1)，它就把 r/g/b 各乘 255 取整后当成真正的颜色用。
	//
	//   下面这套默认值不是随便挑的：引擎内部做"就近匹配"的那张表
	//   （RVA 0x90E5D0 里循环 13 次，表在 [全局对象+0x228]）正好就是暗黑
	//   自己的 13 个地图颜色，而下面这 7 个颜色**每一个都是表里原样存在的**，
	//   距离为 0，所以不会有任何"被就近匹配到别的颜色"的偏差。
	//   实测可用的 13 色：
	//     白 255,255,255   红 255,77,77    亮绿 0,255,0    蓝 105,105,255
	//     暗金 199,179,119 灰 105,105,105  黑 0,0,0        亮金 208,194,125
	//     橙 255,168,0     黄 255,255,100  暗绿 0,128,0    紫 174,0,255
	//     绿 0,200,0
	//
	//   写法说明：byte+0.25 再除以 255。引擎是 (int)(v*255) **截断**取整，
	//   直接写 208/255.0f 有可能算出 207.99998 → 截成 207 偏一档；加 0.25
	//   保证截断回原字节。顺带让纯白不再等于 (1,1,1) 那个哨兵值。
	bool  rgbOn[kSlotCount] { true, true, true, true, true, true, true, true };
	float rgb[kSlotCount][3] {
		{ 255.25f / 255.0f, 255.25f / 255.0f, 255.25f / 255.0f },   // 0 普通   白
		{ 255.25f / 255.0f, 255.25f / 255.0f, 255.25f / 255.0f },   // 1 超强   白
		{ 105.25f / 255.0f, 105.25f / 255.0f, 255.25f / 255.0f },   // 2 魔法   蓝 #6969FF
		{ 255.25f / 255.0f, 255.25f / 255.0f, 100.25f / 255.0f },   // 3 稀有   黄 #FFFF64
		{   0.0f,           255.25f / 255.0f,   0.0f            },  // 4 套装   亮绿 #00FF00（v0.12.0，表内原值）
		{ 255.25f / 255.0f, 215.25f / 255.0f,   0.25f / 255.0f },   // 5 暗金   亮金 #FFD700（v0.12.8：暗金 → 亮金）
		{ 255.25f / 255.0f, 168.25f / 255.0f,   0.0f            },  // 6 手工   橙 #FFA800
		{ 105.25f / 255.0f, 105.25f / 255.0f, 105.25f / 255.0f },   // 7 未知   灰 #696969
	};

	// ★ v0.12.0 星标开关（面板上有对应的"显示星标"勾选框）。
	bool  stars       = true;
	float starSize    = 16.0f;   // 星形的"半径"（像素）。v0.18.4：默认 16（面板滑条 12~36）
	int   starOffsetX = 0;       // 星标位置微调（像素）。v0.12.8 起面板上可直接拖。
	int   starOffsetY = 0;

	// ★ v0.12.7：星坐标换算模式（活体反汇编 0x79DA50 定案的引擎公式）。
	//   1 = 引擎同款换算（默认）：screen = point + (原点-摄像机)*缩放
	//   0 = 直接用引擎 point（不换算）
	//   2 = 旧版"屏幕中心+point"（v0.12.6 及以前的行为，仅对照用）
	int   starCoordMode  = 1;
	bool  starCoordDebug = false; // 调试十字（位置已确认，默认关）

	// ★ v0.12.8：地图显示范围倍率（雷达）。
	//   1 = 原样（不动游戏）；2/5/10 = 把游戏地图的缩放按同样倍数缩小，
	//   让雷达看到 2/5/10 倍远。实现方式：每帧把渲染上下文里那个"缩放"
	//   字段改成 原值/倍率（字段位置运行时自动标定，见 ApplyMapZoom）。
	int   mapZoomDiv  = 1;

	// ★ v0.12.3：星品物品的引擎形状(X)**不再绘制**，地图上只剩叠加层的星。
	//   实测定案：引擎形状颜色只认"色号"、不认真彩 RGB，与其染色不如不画。
	//   万一要回旧样子（引擎 X 照画），把配置里 hide_engine_shape 改成 false。
	bool  hideEngineShape = true;

	// ★ v0.14.3：星标"驻留"秒数。**默认 0**（= 只显示引擎当帧报的那几件）。
	//   为什么把默认从 4 改成 0：引擎给的坐标**只在它当帧的语境里是对的**；
	//   一旦引擎不再报（物品出了雷达范围 / 开了背包菜单），我们手里这对数字
	//   就不再跟着世界走 —— 再"留着"它，星就会停在屏幕原来的位置，
	//   看起来就是"跟着人物移动、位置不对"（v0.14.2 用户实测）。
	//   => 优先保证"位置永远准"，所以默认不留。
	//   想要"多看一会儿"（代价：那一刻位置会飘）就把这个值调大，范围 0~8 秒。
	float starPersistSec = 0.0f;

	// ★ v0.14.4：星标描边粗细（= 半径的百分之几，0 = 不描边）。
	//   默认 8：半径 88 的星约 7px 黑边（亮色填充配黑边、暗色填充配白边，自动选），
	//   缩到多小都按比例跟着缩。
	float starOutline = 8.0f;

	// ★ v0.15.0：星标样式。0 = 简约（单色平板，v0.14.4 的样子）；
	//             1 = 暗黑3（分层渐变：边缘深、中心亮，圆润星臂 + 深色描边，默认）。
	//   颜色仍然跟着每一类品质的 rgb_xxx 走：渐变是在你选的颜色上做"变暗/提亮"。
	int   starStyle = 1;

	// ★ v0.14.3：面板底下的星要不要**变淡**（默认关）。
	//   默认关：星的亮度只跟品质有关，不再随"面板/背包开没开"变化 ——
	//   v0.14.2 用户报的"开背包亮、关背包暗"就是这个"变淡"被状态牵着走：
	//   游戏里一开别的窗口，宿主就那一帧不画面板 → 我们误以为面板关了 → 不变淡 → 变亮。
	bool  dimStarUnderPanel = false;

	// ★ v0.13.0：面板上的点击不再穿到游戏里（点面板时人物不动）。
	//   true = 面板打开且光标在面板上时，屏蔽游戏读到的鼠标按键。
	bool  blockGameMouse = true;

	// ★ v0.14.7："引擎色号兜底"**默认关闭**。
	//   它原本是为了补"引擎自己画的绿星/灰块"，但实测会大面积误判
	//   （把 0 学成"暗金色号"，而 0 是绝大多数普通物品的返回值 ⇒ 满地的普通物品
	//   全被判成暗金，"只爆一件却一片星星"就是这么来的）。
	//   现在物品的引擎标记改成**一律压掉**，不再需要它；想用的人可以在面板上开。
	bool  engineColourRescue = false;

	// ★ v0.17.7：自带字体图集的总开关（保命开关）。
	//   默认开。如果实测发现面板文字颜色异常/花字，把配置里的 own_font 改成
	//   false 就能立刻回到"宿主合并字体"（可读、混排），不用重装旧版。
	bool  ownFont = true;

	// ★ v0.14.0：自定义规则槽位（面板上预留的接入位，见 kRuleRows）。
	//   ruleOn  = 这条规则开不开；
	//   ruleRgb = 命中这条规则时星的颜色（面板上点色块改）；
	//   ruleName= 面板上显示的名字（用户可改，留空就用默认名）。
	//   ★ v0.16.0：rule0（珠宝匠/工匠）已接入判定，默认开 —— 装上就生效。
	bool  ruleOn[kRuleCount] { true, false, false, false };
	float ruleRgb[kRuleCount][3] {
		{ 105.25f / 255.0f, 105.25f / 255.0f, 255.25f / 255.0f },   // 蓝（珠宝匠/工匠）
		{ 255.25f / 255.0f, 215.25f / 255.0f,   0.25f / 255.0f },   // 亮金
		{ 255.25f / 255.0f, 255.25f / 255.0f, 255.25f / 255.0f },   // 白
		{ 255.25f / 255.0f, 168.25f / 255.0f,   0.25f / 255.0f },   // 橙
	};
	char  ruleName[kRuleCount][24] { "", "", "", "" };

	int qualityOffset = 0;
};

Settings g_settings;

// ─────────────────────────── 全局状态 ───────────────────────────

const D2RL::PluginContext* g_context = nullptr;

using GetUnitColorIndexFn = bool(__fastcall*)(const void* unit, std::int32_t* out1, std::int32_t* out2) noexcept;
GetUnitColorIndexFn g_originalGetUnitColorIndex = nullptr;

std::uint32_t g_itemUnits  = 0;   // 见过多少个不同的物品单位
std::uint32_t g_itemCalls  = 0;   // 钩子被物品命中的总次数
std::unordered_set<const void*> g_seenUnits;   // 观察模式去重

// ───────── 任意颜色（v0.7.0）：待用的 RGB ─────────
//  取色钩子决定"这个物品要画"时把颜色放这里；紧接着游戏画光点时会来取。
//  地图渲染是单线程、取色后立刻绘制，所以一个普通全局变量就够。
struct PendingRgb {
	bool  active = false;
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
};
PendingRgb                 g_pendingRgb {};

// 诊断用：如果"上一条颜色还没被取走就被新的覆盖了"，说明游戏是先把一批
// 单位的颜色都问完、再统一绘制的（批量模式），而不是"问一个画一个"。
// 那样单变量会串色。有这个计数我们第一时间就能从日志看出来，
// 不用靠肉眼看地图猜。
std::atomic<std::uint32_t> g_pendingOverwrites { 0 };

// ★ v0.12.0 星标：取色决策 → 绘制钩子 的"顺手记坐标"信号（slot+1，0 = 无）。
// 定义必须在 ClearPendingRgb 之前（它在里面被清零）。
std::atomic<int> g_starSlotPending { 0 };
// ★ v0.14.1：和上面那个令牌一起送出的“这颗星的判定可信度”
//   （2 = 按品质直接判定；1 = 靠学到的引擎色号兑底救回来的）。
std::atomic<int> g_starConfPending { 0 };
// ★ v0.14.6：跟令牌一起送出的**物品单位指针**。
//   实测（2026-09-22 19:56）：引擎对**同一件物品**一帧里会反复来取色，
//   `bu` 能到 40（地上其实只有三四件东西）→ 一颗物品被画成一堆星。
//   拿单位指针在当帧内去重，就能做到"一件物品一帧只出一颗星"，且不需要猜坐标容差。
std::atomic<const void*> g_starUnitPending { nullptr };

// ★ v0.12.4：两个绘制调用点各自的"跳过一次绘制"闸门（单次消费）。
//  标记函数每个标记画两笔（DrawBlob 一笔 + 直接 call sub_858510 的灰色底块一笔，
//  见 kBlobPatchSites 注释）。星品取色时两个闸门同时拉上，两个小桩各自消费
//  自己的闸门后就地 ret —— 两笔都不画，地图只剩叠加层的星。
//  每次取色开始都会先把两个闸门清零（SetSkipGates(false)），星品决策再按需
//  拉上 —— 这样怪物等其它单位绝不会被上一条星品的闸门误伤。
std::atomic<int> g_skipSiteA { 0 };   // 调用点 A：DrawBlob 内部（RVA 0xD6EE3）
std::atomic<int> g_skipSiteB { 0 };   // 调用点 C：物品标记函数直连（RVA 0xD2DF5）

// ★ v0.12.6 诊断计数器：整条星管线每一环各一个，每 5 秒限流打一条日志。
//   再断供，从一行日志就能看出断在哪一环（取色→A点→叠加层），不用再猜。
std::atomic<std::uint32_t> g_siteAHits    { 0 };   // A 点钩子实际命中（星品）
std::atomic<std::uint32_t> g_siteCHits    { 0 };   // C 点钩子被调（任何单位）
std::atomic<std::uint32_t> g_shapeHits    { 0 };   // 形状绘制闸门实际拦下的次数（灰块）

// ★ v0.18.7 诊断：三个被改写的调用点**分别被叫到多少次**（不管闸门有没有拉）。
//   配合 g_hiddenItems（我们叫引擎"这一件不要画"的次数）就能回答那个矛盾：
//   "我们明明叫它别画，它却还在画" —— 到底是哪一环没生效（或者根本不是这条管线画的）。
std::atomic<std::uint32_t> g_siteATotal   { 0 };   // A 点被调总次数
std::atomic<std::uint32_t> g_shapeTotal   { 0 };   // S1..S5 形状点被调总次数
std::atomic<std::uint32_t> g_hiddenItems  { 0 };   // 我们判定"不画"的物品取色次数
std::atomic<std::uint32_t> g_siteASkipped  { 0 };   // A 点被我们拦掉的总笔数（星品 + 隐藏品）

auto SetSkipGates(bool on) noexcept -> void {
	const int v = on ? 1 : 0;
	g_skipSiteA.store(v, std::memory_order_relaxed);
	g_skipSiteB.store(v, std::memory_order_relaxed);
}

// 取色钩子每轮开始时调用：上一条颜色若还没被"绘制那边"取走就丢了，记一笔。
auto ClearPendingRgb() noexcept -> void {
	if (g_pendingRgb.active) {
		g_pendingOverwrites.fetch_add(1, std::memory_order_relaxed);
	}
	g_pendingRgb.active = false;
	g_starSlotPending.store(0, std::memory_order_relaxed);
	g_starConfPending.store(0, std::memory_order_relaxed);
}

// 取色钩子判定"这个物品要画"时调用：把它的颜色放到取用口。
auto SetPendingRgb(int slot) noexcept -> void {
	if (g_pendingRgb.active) {
		g_pendingOverwrites.fetch_add(1, std::memory_order_relaxed);
	}
	g_pendingRgb.r      = g_settings.rgb[slot][0];
	g_pendingRgb.g      = g_settings.rgb[slot][1];
	g_pendingRgb.b      = g_settings.rgb[slot][2];
	g_pendingRgb.active = true;
}
std::atomic<std::uint32_t> g_rgbDraws { 0 };    // 真正用上任意颜色的光点数

// ───────── ★ v0.12.0 星标：从取色钩子往叠加层送标记坐标 ─────────
//  反汇编确认：sub_858510(obj, point, style) 的 point+0/+4 = 引擎算好的
//  标记屏幕偏移（两个 int32）。HookBlobIconPrep 每次被调（= 引擎真画了一个
//  标记）把坐标记进缓冲；叠加层每帧取走画★后清零。
//  生产者 = 游戏地图渲染线程；消费者 = 宿主叠加层回调（同一渲染线程），
//  即便万一不同步，也只是某一帧少画/多画一颗★，无安全风险。
struct StarPoint {
	float x;
	float y;
	float ax;     // ★ v0.12.7：按引擎公式换算出的真实屏幕坐标（仅调试用）
	float ay;
	int   slot;   // SettingSlot
	int   conf;   // ★ v0.14.1：2 = 品质直接读出；1 = 引擎色号兑底
	const void* unit;   // ★ v0.14.6：这件物品的单位指针（同一帧内按它去重，一件物品只画一颗星）
};
constexpr int kStarMax = 512;
StarPoint              g_starPoints[kStarMax] {};
std::atomic<int>       g_starCount { 0 };
// （g_starSlotPending 的定义在 ClearPendingRgb 之前）
std::atomic<std::uint32_t> g_starSamples { 0 };   // 已画★总数（诊断，累计值）
// ★ v0.12.8：面板显示用"当帧真实数量"（上面那个累计值会一直涨，容易看着像 bug）。
std::atomic<int> g_starsLive       { 0 };
std::atomic<int> g_starsLiveSet    { 0 };
std::atomic<int> g_starsLiveUnique { 0 };
// ★ v0.12.8：面板矩形（同一渲染线程内写读；面板打开时让落在里面的★不画）。
// ★ v0.13.0：visible/inside 改成原子 —— 游戏输入线程要读它（面板挡鼠标）。
std::atomic<bool> g_panelVisible { false };
std::atomic<bool> g_mouseInPanel  { false };
float g_panelX0 = 0.0f;
float g_panelY0 = 0.0f;
float g_panelX1 = 0.0f;
float g_panelY1 = 0.0f;

// ───────────────────────────────────────────────────────────────────────────
//  ★ v0.13.0（一）："小绿星 + 灰色底块"的真正根因
//
//  硬证据（截图逐像素 + 日志计数）：
//    · 用户看到的小星星像素是**贴图渐变绿**（rgb 从 128 到 253 都有），
//      而我们自己画的星是纯色（大绿星 #00FF00、大黄星 #FFD700）；
//    · 日志里 `diag: item / siteA / shapeSkips` 三个计数**完全相等**，
//      说明凡是走过的物品我们都拦住了；
//    · 于是结论只有一个：**引擎自己还给一部分物品画了标记**，而且这些物品
//      我们的 ReadItemQuality() 没判成套装/暗金（于是既没画我们的星，也没拉闸）。
//
//  修法：引擎调用原始取色函数时会把它自己选的**色号**写进 out1。我们拿
//  "我们判成套装/暗金的物品都用了哪个色号"去投票，学出引擎的套装色号与
//  暗金色号；之后凡是引擎色号命中这两个值的物品，一律按套装/暗金处理
//  （画我们的星 + 拦引擎的标记）。这样漏判的物品也会被救回来。
constexpr int              kColorVoteSlots   = 64;
std::atomic<std::uint32_t> g_voteSetColor[kColorVoteSlots]    {};
std::atomic<std::uint32_t> g_voteUniqueColor[kColorVoteSlots] {};
std::atomic<int>           g_engineSetColor    { -1 };   // 学到的"引擎套装色号"
std::atomic<int>           g_engineUniqueColor { -1 };   // 学到的"引擎暗金色号"
std::atomic<std::uint32_t> g_starRecovered     { 0 };    // 靠色号兜底救回来的物品数
std::atomic<std::uint32_t> g_rule0Hits         { 0 };    // ★ v0.16.0：rule0（珠宝匠/工匠）命中数
std::atomic<std::uint32_t> g_voteTick          { 0 };

// ───────────────────────────────────────────────────────────────────────────
//  ★ v0.14.2：星标 = “快照 + 驻留”（第五版；前四版全翻车，见 DrawStars 顶部注释）
//
//  实测铁证（2026-09-22 19:06–19:10 日志）：引擎当帧只报 1 件物品（buffered=1），
//  插件却记到 512 条、画出 117 颗星 —— 说明**同一件物品的“原始坐标”根本不是恒定值**，
//  它会一点一点漂（同一件东西几十秒里换过上百个坐标）。
//  ⇒ 任何“按坐标认物品”的写法（v0.13.0 配对求位移 / v0.14.0 相机差分预测 /
//    v0.14.1 地图坐标+命中计数）都会把同一件东西当成新星、每帧多记一颗。
//    这是幽灵星的唯一根因，跟用哪个坐标系无关。
//  ⇒ v0.14.2：不认物品、不配对、不积累 —— 引擎这一帧报了几件，就把“当前星表”
//    **整表替换**成这几件。上限 = 单帧最多报过的件数 ⇒ 结构上不可能越攒越多。
struct StarMemory {
	float mx = 0.0f;    // 引擎给的原始坐标（快照）
	float my = 0.0f;
	int   slot = 0;
	int   conf = 0;     // 2 = 品质直接读出；1 = 引擎色号兑底
};
constexpr int   kStickyMax      = 512;    // 快照容量（日志里 hold= 就是实际条数）
constexpr float kStarHoldSecMax = 8.0f;   // 驻留上限（秒）：配置写再大也钳到这里
constexpr float kStarMergeUnits = 2.5f;   // 只用于“同一帧内”给同一件物品去重
StarMemory      g_sticky[kStickyMax] {};
int             g_stickyCount = 0;
std::atomic<std::int64_t> g_lastObservationMs { 0 };   // 最近一次“引擎报了物品”的时刻
std::int64_t    g_starSnapshotMs = 0;                  // 快照的取样时刻（诊断 age=）

// ───────────────────────────────────────────────────────────────────────────
//  ★ v0.13.0（三）：面板挡住鼠标 —— 在面板上点一下，游戏里不许也动
//
//  先取证（活进程，tools/live_iat_owners.py + live_core_hookcalls.py + live_iat_check.py）：
//    · 谁在用鼠标：整个进程里只有 D2RCore.dll 的导入表里有 GetAsyncKeyState /
//      SetWindowsHookExW（主模块在内存里的导入目录被抹掉了，但按"值扫描"
//      也找不到这两个函数指针）；而我们自己的插件当然是另一回事。
//    · D2RCore 拿 GetAsyncKeyState 只轮询两个键：`mov ecx,9` = Tab(0x09)、
//      `mov ecx,0x1b` = Esc —— **完全没有轮询鼠标键**。
//    · 它读鼠标走的是 **5 个 WH_MOUSE(=5) 钩子**：五处调用点都是
//      `mov ecx,5`（idHook=WH_MOUSE）/ `xor r8d,r8d`（hMod=NULL）/
//      `mov r9d,eax`（dwThreadId = 游戏 UI 线程）。
//  → 所以做"挡住面板里的点击"最安全的路子不是去改游戏代码，而是**我们自己也
//    装一个 WH_MOUSE 钩子、装在同一个线程上**：
//      Windows 的钩子链是"后装的先被调用"，我们在面板打开时才装 → 排在游戏那
//      5 个钩子之前 → 面板矩形内的按键消息直接 `return 1`，系统就不会把它交给
//      后面的钩子和窗口过程，游戏**根本看不到这一下点击**。
//      一个字节的游戏代码都不动；面板关掉 / 插件卸载就 Unhook 还回去。
//      连点也不是问题：ImGui 那边的输入是我们每帧直写字段喂进去的，不靠消息。
std::atomic<bool>  g_blockGameMouse { true };    // 面板挡鼠标总开关（= 配置项）
std::atomic<bool>  g_panelScrValid  { false };   // 下面这个矩形是不是有效
std::atomic<int>   g_panelScrX0     { 0 };       // 面板矩形（**屏幕**像素）
std::atomic<int>   g_panelScrY0     { 0 };
std::atomic<int>   g_panelScrX1     { 0 };
std::atomic<int>   g_panelScrY1     { 0 };
// 最多同时挂两个线程：① 宿主给我们的那扇窗口的线程（一般就是游戏 UI 线程）；
// ② 前台顶层窗口的线程 —— 万一 ① 是宿主自己的窗口，② 兜住游戏那一边。
// 只渲染线程读写这两个数组；钩子回调只读上面那几个原子量。
constexpr int kMouseHookSlots = 2;
struct MouseHookSlot {
	void*         handle = nullptr;   // HHOOK
	std::uint32_t tid    = 0;
};
MouseHookSlot              g_gameMouseHooks[kMouseHookSlots] {};
std::atomic<int>           g_gameMouseHookCount { 0 };   // 装上了几个（面板状态行用）
std::atomic<std::uint32_t> g_swallowedClicks { 0 };   // 吞掉了几次点击（诊断）
HWND               g_gameHwnd = nullptr;         // 游戏窗口（SampleMouse 里刷新）
// "游戏窗口不在我们自己进程里、所以挂不上钩子"这条 warn 的最早下一条时间戳（毫秒），
// 免得每帧刷屏。
std::uint64_t      g_mouseHookWarnNextMs = 0;

auto RecordStar(float x, float y, float ax, float ay, int slot, int conf,
                const void* unit) noexcept -> void {
	const int n = g_starCount.load(std::memory_order_relaxed);
	if (n < kStarMax) {
		g_starPoints[n].x    = x;
		g_starPoints[n].y    = y;
		g_starPoints[n].ax   = ax;
		g_starPoints[n].ay   = ay;
		g_starPoints[n].slot = slot;
		g_starPoints[n].conf = conf;
		g_starPoints[n].unit = unit;   // ★ v0.14.6
		g_starCount.store(n + 1, std::memory_order_release);
	}
}

// 定义在后面（OverlayPanel 区），这里先声明：读别人进程里的指针前先探一下可读性。
auto AddressReadable(const void* p, std::size_t n) noexcept -> bool;

// ───────── ★ v0.12.7：按引擎自己的公式把标记坐标换算成真实屏幕坐标 ─────────
//  活体反汇编定案（tools/live_probe_v0127.py / _v0127b，RVA 0x79DA50）：
//    引擎画标记时把 sub_858510 收到的 point 这样用：
//      x_最终 = point.x - ctx[0xc4]*缩放 + ctx[0xd0]*缩放
//      y_最终 = point.y - ctx[0xc8]*缩放 + ctx[0xc0]*缩放
//    其中 ctx = *(void**)obj（obj = rcx = DrawBlob 里 lea 的那个全局结构 G），
//    缩放 = sub_858510 的第 4 个参数（xmm3；我们小桩 call 进钩子时寄存器
//    原样保留，声明成第 4 个 float 形参就能拿到）。
//  之前 v0.12.0~v0.12.6 的"屏幕中心 + point"纯属没有依据的猜测假设，
//  这就是星跑到屏幕右下角的根因。
auto StarScreenPos(const void* obj, const void* point, float zoom,
                   float& rawX, float& rawY, float& absX, float& absY) noexcept -> bool {
	if (point == nullptr || !AddressReadable(point, 8)) {
		return false;
	}
	const auto* ints = static_cast<const std::int32_t*>(point);
	rawX = static_cast<float>(ints[0]);
	rawY = static_cast<float>(ints[1]);
	absX = rawX;
	absY = rawY;
	if (obj != nullptr && AddressReadable(obj, sizeof(void*))) {
		const void* ctx = *static_cast<const void* const*>(obj);
		if (ctx != nullptr && AddressReadable(ctx, 0xd4)) {
			const auto* f = static_cast<const std::int32_t*>(ctx);
			const float orgX = static_cast<float>(f[0xd0 / 4]);
			const float camX = static_cast<float>(f[0xc4 / 4]);
			const float orgY = static_cast<float>(f[0xc0 / 4]);
			const float camY = static_cast<float>(f[0xc8 / 4]);
			absX = rawX + (orgX - camX) * zoom;
			absY = rawY + (orgY - camY) * zoom;
		}
	}
	return true;
}

// ───────── ★ v0.12.8：雷达范围（把引擎地图的缩放按倍率缩小）─────────
//  思路：引擎每画一个标记都会把"当前缩放"作为参数传给 sub_858510
//  （我们小桩能拿到 → g_lastZoom），它同时一定存在渲染上下文（ctx）里的
//  某个 float 字段（引擎每帧从那儿读）。所以：
//    ① 记录最近一次绘制用的 ctx 指针 + 缩放值；
//    ② 叠加层每帧在 ctx 里**自动标定**那个字段（在 0xd4..0x200 里找
//       唯一一个与缩放值逐位相等的 float）；
//    ③ 标定成功后，每帧把该字段写成 原值/倍率 → 地图整体缩小、看得更远。
//  安全阀：①倍率=1 时一个字节都不写；②标定不到唯一匹配就完全不写（只写日志）；
//  ③ 只写这一个 float 位置，且原值必须 > 0。
// 日志函数定义在后面，这里先声明（雷达标定要写日志）。
auto LogInfo(const char* text) noexcept -> void;
auto LogWarn(const char* text) noexcept -> void;

std::atomic<void*>         g_lastCtx      { nullptr };
std::atomic<float>         g_lastZoom     { 0.0f };
std::atomic<int>           g_zoomFieldOff { -1 };
std::atomic<std::uint32_t> g_zoomWrites   { 0 };
std::atomic<std::uint32_t> g_zoomBadCal   { 0 };

auto RememberRenderCtx(const void* obj, float zoom) noexcept -> void {
	if (obj != nullptr && zoom > 0.0f) {
		g_lastCtx.store(const_cast<void*>(obj), std::memory_order_relaxed);
		g_lastZoom.store(zoom, std::memory_order_relaxed);
	}
}

auto ApplyMapZoom() noexcept -> void {
	const int div = g_settings.mapZoomDiv;
	if (div <= 1) {
		return;
	}
	void* obj = g_lastCtx.load(std::memory_order_relaxed);
	const float z = g_lastZoom.load(std::memory_order_relaxed);
	if (obj == nullptr || z <= 0.0f || !AddressReadable(obj, sizeof(void*))) {
		return;
	}
	auto* const* objWords = static_cast<void* const*>(obj);
	void* ctx = objWords[0];
	if (ctx == nullptr || !AddressReadable(ctx, 0x200)) {
		return;
	}
	auto* f = static_cast<float*>(ctx);

	int off = g_zoomFieldOff.load(std::memory_order_relaxed);
	if (off < 0) {
		// 自动标定：在摄像机/原点字段之后（0xd4）到 0x200 之间找唯一匹配。
		int found = -1;
		int hits  = 0;
		for (int byteOff = 0xd4; byteOff + 4 <= 0x200; byteOff += 4) {
			if (f[byteOff / 4] == z) {
				if (found < 0) {
					found = byteOff;
				}
				++hits;
			}
		}
		if (hits == 1) {
			g_zoomFieldOff.store(found, std::memory_order_relaxed);
			char line[200] {};
			std::snprintf(line, sizeof(line),
				"Loot Map radar: zoom field calibrated at ctx+0x%X (zoom %.5f, div %d).",
				found, static_cast<double>(z), div);
			LogInfo(line);
		} else {
			if (g_zoomBadCal.fetch_add(1, std::memory_order_relaxed) == 0) {
				char line[220] {};
				std::snprintf(line, sizeof(line),
					"Loot Map radar: cannot calibrate the zoom field (zoom=%.5f, matches=%d) -- "
					"the radar multiplier has no effect this session.",
					static_cast<double>(z), hits);
				LogWarn(line);
			}
			return;
		}
		off = g_zoomFieldOff.load(std::memory_order_relaxed);
	}

	float& field = f[off / 4];
	if (!(field > 0.0f)) {
		return;
	}
	const float want = z / static_cast<float>(div);
	if (field != want) {
		field = want;
		g_zoomWrites.fetch_add(1, std::memory_order_relaxed);
	}
}


// ───────────── automap-blob 取色函数指针重定向（让物品也能上色） ─────────────
//  反汇编确认（automap-blob v1.0.0，dll + 0x24D0 是它的取色桩）：
//    桩开头先 call 一个"函数指针"——该指针存在 dll + 0x8178，运行时值就是
//    游戏本体的取色函数（D2RLoader.exe + 0xD78F0）。桩拿到结果后只处理
//    怪物（unit[0]==1），其余类型直接返回"不画"，本插件对物品的决策
//    就是这样被扔掉的。
//
//  v0.3.6 的做法（替代 v0.3.5 那层"改桩入口字节"的包装）：
//    直接把 dll + 0x8178 里那个指针换成我们的函数。桩依旧是一次普通 call，
//    栈帧完全正常（v0.3.5 的包装会多压一层返回地址，把桩结尾恢复
//    rbx/rsi 用的固定偏移错开 8 字节，属于必须消除的隐患）。
//    我们函数里先原样调用真正的取色函数（怪物行为一个字节不变），
//    再对物品按品质追加"要画 + 色号"的决策。
using BlobColorStubFn = bool(__fastcall*)(const void*, std::int32_t*, std::int32_t*) noexcept;

constexpr std::uintptr_t kBlobDllColorFnSlotRva = 0x00008178;   // 桩读的那个函数指针格子
// （游戏本体取色函数的 RVA 复用上面的 kGetUnitColorIndexRva）

void**            g_blobColorFnSlot  = nullptr;   // 被我们改过的那个格子
void*             g_blobColorFnSaved = nullptr;   // 原值（卸载时还原）
std::atomic<bool> g_blobPtrHookTried     { false };
std::atomic<bool> g_blobPtrHookInstalled { false };

// 诊断计数（限流写日志，热路径上只做原子自增）
std::atomic<std::uint32_t> g_blobStubCalls { 0 };   // 桩调用我们的总次数
std::atomic<std::uint32_t> g_blobItemSeen  { 0 };   // 其中"单位是物品"的次数

// ── 原始内存取证：只对最早见到的几个地面物品单位做一次 hexdump ──
//   目的：拿到"单位结构 + 物品数据"的真实字节，用来找底材/孔数/词缀字段。
//   纯读取，写完这几条就再也不进入这段逻辑。
//   （DumpUnitRaw 的实现在 HexDump 之后，因为要用到它。）
constexpr int kUnitDumpMax = 5;
std::atomic<int>        g_unitDumpCount { 0 };
std::atomic<const void*> g_unitDumpSeen[kUnitDumpMax];

auto DumpUnitRaw(const void* unit, const void* itemData) noexcept -> void;

// ── ★ v0.14.5：星品身份取证的"去重 + 限流"表 ──
//   键 = (槽位, 物品类型号)，同一个键每 3 秒最多写一行日志。
//   放在这里（HexDump / LogInfo 之后使用，声明在前）是因为取色钩子要用到。
struct StarIdSeen {
	std::uint32_t key    = 0;   // (slot<<24) | txtFileNo
	std::uint64_t nextMs = 0;   // 下次允许写日志的时刻
};
constexpr int kStarIdSlots = 16;
StarIdSeen    g_starIdSeen[kStarIdSlots] {};

// ─────────────────────── 小工具：日志 ───────────────────────

auto LogInfo(const char* text) noexcept -> void {
	if (g_context != nullptr && text != nullptr) {
		g_context->LogInfo(text);
	}
}

auto LogWarn(const char* text) noexcept -> void {
	if (g_context != nullptr && text != nullptr) {
		g_context->LogWarn(text);
	}
}

auto LogError(const char* text) noexcept -> void {
	if (g_context != nullptr && text != nullptr) {
		g_context->LogError(text);
	}
}

// ─────────────────── 小工具：字符串构造器 ───────────────────

class TextBuilder {
public:
	TextBuilder(char* buffer, std::size_t capacity) noexcept
		: m_buffer(buffer), m_capacity(capacity) {}

	void Append(const char* format, ...) noexcept {
		if (!m_ok || m_length >= m_capacity) {
			m_ok = false;
			return;
		}
		va_list args {};
		va_start(args, format);
		const int written = std::vsnprintf(m_buffer + m_length, m_capacity - m_length, format, args);
		va_end(args);
		if (written < 0 || static_cast<std::size_t>(written) >= m_capacity - m_length) {
			m_ok = false;
			return;
		}
		m_length += static_cast<std::size_t>(written);
	}

	[[nodiscard]] auto Ok() const noexcept -> bool { return m_ok; }
	[[nodiscard]] auto Data() noexcept -> char* { return m_buffer; }
	[[nodiscard]] auto Size() const noexcept -> std::size_t { return m_length; }

private:
	char*       m_buffer   = nullptr;
	std::size_t m_capacity = 0;
	std::size_t m_length   = 0;
	bool        m_ok       = true;
};

// ─────────────────── 小工具：极简 TOML 读取 ───────────────────

auto TrimInPlace(char* text) noexcept -> char* {
	if (text == nullptr) {
		return nullptr;
	}
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
		++text;
	}
	char* end = text + std::strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		--end;
	}
	*end = '\0';
	return text;
}

auto ParseBool(const char* value, bool fallback) noexcept -> bool {
	if (value == nullptr) return fallback;
	if (std::strcmp(value, "true") == 0)  return true;
	if (std::strcmp(value, "false") == 0) return false;
	return fallback;
}

auto ParseInt(const char* value, int fallback) noexcept -> int {
	if (value == nullptr || *value == '\0') return fallback;
	char* end = nullptr;
	const long parsed = std::strtol(value, &end, 0);
	if (end == value) return fallback;
	return static_cast<int>(parsed);
}

auto ParseFloat(const char* value, float fallback) noexcept -> float {
	if (value == nullptr || *value == '\0') return fallback;
	char* end = nullptr;
	const double parsed = std::strtod(value, &end);
	if (end == value) return fallback;
	return static_cast<float>(parsed);
}

auto ClampColor(int value) noexcept -> int {
	if (value < kColorMin) return kColorMin;
	if (value > kColorMax) return kColorMax;
	if (value == kColorSkip1) return 2;
	if (value == kColorSkip2) return 3;
	return value;
}

// 图形编号（out2）：-1 = 不画额外图形；0~5 = 游戏自带的 6 种标记图形。
// 反汇编确认（跳转表 @ RVA 0xD78CC）：out2 = 0/1/2/3/4/5 各走一个分支，
// 每个分支调不同的绘制函数和参数，图形画在该单位自己的坐标上。
constexpr int kShapeNone = -1;
constexpr int kShapeMax  = 5;

auto ClampShape(int value) noexcept -> int {
	if (value < 0 || value > kShapeMax) {
		return kShapeNone;
	}
	return value;
}

// 把 0~255 的字节转成引擎要的 0~1 浮点分量。
// 引擎那边是 (int)(v * 255.0f) 截断取整，所以不能简单地写 byte/255.0f ——
// 208/255.0f*255.0f 可能算出 207.99998，截断成 207，颜色就偏一档。
// 加 0.25 之后截断必定回到用户写的那个字节；同时 255 会变成 1.000980，
// 不再等于引擎用来判断"走色号还是走真颜色"的哨兵值 1.0。
auto ChannelFromByte(int byte) noexcept -> float {
	if (byte <= 0) {
		return 0.0f;
	}
	if (byte >= 255) {
		return 255.25f / 255.0f;
	}
	return (static_cast<float>(byte) + 0.25f) / 255.0f;
}

// 把 "255,170,0" / "255 170 0" / "#FFAA00" 解析成 0~1 的三个浮点分量。
// 解析失败返回 false（调用方保持原值）。
auto ParseRgb(const char* value, float out[3]) noexcept -> bool {
	if (value == nullptr || out == nullptr) {
		return false;
	}
	while (*value == ' ' || *value == '\t') {
		++value;
	}
	if (*value == '#') {
		++value;
		std::uint32_t packed = 0;
		int digits = 0;
		for (; digits < 6; ++digits) {
			const char c = value[digits];
			std::uint32_t nibble = 0;
			if (c >= '0' && c <= '9')      nibble = static_cast<std::uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') nibble = static_cast<std::uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') nibble = static_cast<std::uint32_t>(c - 'A' + 10);
			else break;
			packed = (packed << 4) | nibble;
		}
		if (digits != 6) {
			return false;
		}
		out[0] = ChannelFromByte(static_cast<int>((packed >> 16) & 0xFFu));
		out[1] = ChannelFromByte(static_cast<int>((packed >> 8) & 0xFFu));
		out[2] = ChannelFromByte(static_cast<int>(packed & 0xFFu));
		return true;
	}

	int   channel[3] = { 0, 0, 0 };
	const char* cursor = value;
	for (int i = 0; i < 3; ++i) {
		char* end = nullptr;
		const long parsed = std::strtol(cursor, &end, 10);
		if (end == cursor) {
			return false;
		}
		if (parsed < 0)   channel[i] = 0;
		else if (parsed > 255) channel[i] = 255;
		else channel[i] = static_cast<int>(parsed);
		cursor = end;
		while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == '/') {
			++cursor;
		}
	}
	if (*cursor != '\0' && *cursor != '\r' && *cursor != '\n' && *cursor != '#') {
		return false;
	}
	out[0] = ChannelFromByte(channel[0]);
	out[1] = ChannelFromByte(channel[1]);
	out[2] = ChannelFromByte(channel[2]);
	return true;
}

auto Rgb255(float v) noexcept -> int {
	if (v <= 0.0f) return 0;
	if (v >= 1.0f) return 255;
	return static_cast<int>(v * 255.0f + 0.5f);
}

auto FindRow(const char* key) noexcept -> int {
	if (key == nullptr) return -1;
	for (int i = 0; i < kSlotCount; ++i) {
		if (std::strcmp(key, kRows[i].key) == 0) {
			return i;
		}
	}
	return -1;
}

auto SlotForQuality(int quality) noexcept -> int {
	switch (quality) {
		case kQualityNormal:   return kSlotNormal;
		case kQualityInferior: return kSlotNormal;
		case kQualitySuperior: return kSlotSuperior;
		case kQualityMagic:    return kSlotMagic;
		case kQualityRare:     return kSlotRare;
		case kQualitySet:      return kSlotSet;
		case kQualityUnique:   return kSlotUnique;
		case kQualityCrafted:  return kSlotCrafted;
		default:               return kSlotUnknown;
	}
}

auto ApplySetting(Settings& s, const char* key, const char* value) noexcept -> void {
	if (key == nullptr || value == nullptr) return;

	if (std::strcmp(key, "enabled") == 0) {
		s.enabled = ParseBool(value, s.enabled);
		return;
	}
	if (std::strcmp(key, "mode") == 0) {
		s.observeOnly = (std::strcmp(value, "observe") == 0);
		return;
	}
	if (std::strcmp(key, "quality_offset") == 0) {
		s.qualityOffset = ParseInt(value, s.qualityOffset);
		return;
	}
	// ★ v0.12.0 星标
	if (std::strcmp(key, "stars") == 0) {
		s.stars = ParseBool(value, s.stars);
		return;
	}
	if (std::strcmp(key, "star_size") == 0) {
		s.starSize = ParseFloat(value, s.starSize);
		// ★ v0.14.0：钳一下范围。用户实测"调完星星大小星就不见了" —— 一部分
		//   原因就是这值被拖到 0 或者极端值。8~160 px 之外一律拉回来。
		// ★ v0.17.0：面板滑条范围 12~36，配置超范围的也按这个区间钳住
		if (s.starSize < 12.0f)  { s.starSize = 12.0f; }
		if (s.starSize > 36.0f)  { s.starSize = 36.0f; }
		return;
	}
	if (std::strcmp(key, "star_offset_x") == 0) {
		s.starOffsetX = ParseInt(value, s.starOffsetX);
		return;
	}
	if (std::strcmp(key, "star_offset_y") == 0) {
		s.starOffsetY = ParseInt(value, s.starOffsetY);
		return;
	}
	// ★ v0.12.3：星品物品的引擎形状是否还画（默认 false = 不画 = 只剩星）
	if (std::strcmp(key, "hide_engine_shape") == 0) {
		s.hideEngineShape = ParseBool(value, s.hideEngineShape);
		return;
	}
	// ★ v0.12.7：星坐标换算模式 + 调试十字
	if (std::strcmp(key, "star_coord_mode") == 0) {
		s.starCoordMode = ParseInt(value, s.starCoordMode);
		if (s.starCoordMode < 0 || s.starCoordMode > 2) {
			s.starCoordMode = 1;
		}
		return;
	}
	if (std::strcmp(key, "star_coord_debug") == 0) {
		s.starCoordDebug = ParseBool(value, s.starCoordDebug);
		return;
	}
	// ★ v0.12.8：雷达范围倍率（1 = 原样；2/5/10 = 地图缩小同倍数）
	if (std::strcmp(key, "map_zoom_div") == 0) {
		s.mapZoomDiv = ParseInt(value, s.mapZoomDiv);
		if (s.mapZoomDiv < 1)  { s.mapZoomDiv = 1; }
		if (s.mapZoomDiv > 20) { s.mapZoomDiv = 20; }
		return;
	}
	// ★ v0.13.0：星标保留秒数（显示范围）＋ 面板挡鼠标
	if (std::strcmp(key, "star_persist_sec") == 0) {
		s.starPersistSec = ParseFloat(value, s.starPersistSec);
		if (s.starPersistSec < 0.0f)                  { s.starPersistSec = 0.0f; }
		if (s.starPersistSec > kStarHoldSecMax)       { s.starPersistSec = kStarHoldSecMax; }
		return;
	}
	if (std::strcmp(key, "block_game_mouse") == 0) {
		s.blockGameMouse = ParseBool(value, s.blockGameMouse);
		return;
	}
	// ★ v0.14.3：面板底下的星变淡（默认关）
	if (std::strcmp(key, "dim_star_under_panel") == 0) {
		s.dimStarUnderPanel = ParseBool(value, s.dimStarUnderPanel);
		return;
	}
	// ★ v0.15.0：星标样式（0 = 简约；1 = 暗黑3 立体）
	if (std::strcmp(key, "star_style") == 0) {
		s.starStyle = ParseInt(value, s.starStyle);
		if (s.starStyle < 0 || s.starStyle > 1) { s.starStyle = 1; }
		return;
	}
	// ★ v0.14.4：星标描边粗细（%半径，0 = 不描边）
	if (std::strcmp(key, "star_outline") == 0) {
		s.starOutline = ParseFloat(value, s.starOutline);
		if (s.starOutline < 0.0f)  { s.starOutline = 0.0f; }
		if (s.starOutline > 30.0f) { s.starOutline = 30.0f; }
		return;
	}
	// ★ v0.14.0：引擎色号兜底开关
	if (std::strcmp(key, "engine_colour_rescue") == 0) {
		s.engineColourRescue = ParseBool(value, s.engineColourRescue);
		return;
	}
	if (std::strcmp(key, "own_font") == 0) {
		s.ownFont = ParseBool(value, s.ownFont);
		return;
	}
	// ★ v0.14.0：自定义规则槽位（rule0_enable / rule0_rgb / rule0_name …）
	for (int r = 0; r < kRuleCount; ++r) {
		const char*      base  = kRuleRows[r].key;      // "rule0" …
		const std::size_t blen = std::strlen(base);
		if (std::strncmp(key, base, blen) != 0) {
			continue;
		}
		const char* suffix = key + blen;
		if (std::strcmp(suffix, "_enable") == 0) {
			s.ruleOn[r] = ParseBool(value, s.ruleOn[r]);
			return;
		}
		if (std::strcmp(suffix, "_rgb") == 0) {
			float parsed[3] { 1.0f, 1.0f, 1.0f };
			if (ParseRgb(value, parsed)) {
				s.ruleRgb[r][0] = parsed[0];
				s.ruleRgb[r][1] = parsed[1];
				s.ruleRgb[r][2] = parsed[2];
			}
			return;
		}
		if (std::strcmp(suffix, "_name") == 0) {
			// 就地复制（这里在 CopyBounded 之前，不能调它）
			std::size_t i   = 0;
			const std::size_t cap = sizeof(s.ruleName[r]) - 1;
			while (i < cap && value[i] != '\0') {
				s.ruleName[r][i] = value[i];
				++i;
			}
			s.ruleName[r][i] = '\0';
			return;
		}
		return;
	}

	enum class KeyKind { Show, Color, Shape, Rgb };

	KeyKind     kind = KeyKind::Show;
	const char* name = nullptr;
	if (std::strncmp(key, "show_", 5) == 0) {
		kind = KeyKind::Show;
		name = key + 5;
	} else if (std::strncmp(key, "color_", 6) == 0) {
		kind = KeyKind::Color;
		name = key + 6;
	} else if (std::strncmp(key, "shape_", 6) == 0) {
		kind = KeyKind::Shape;
		name = key + 6;
	} else if (std::strncmp(key, "rgb_", 4) == 0) {
		kind = KeyKind::Rgb;
		name = key + 4;
	} else {
		return;
	}

	const int slot = FindRow(name);
	if (slot < 0) {
		return;
	}

	switch (kind) {
		case KeyKind::Show:
			s.show[slot] = ParseBool(value, s.show[slot]);
			break;
		case KeyKind::Color:
			s.color[slot] = ClampColor(ParseInt(value, s.color[slot]));
			break;
		case KeyKind::Shape:
			s.shape[slot] = ClampShape(ParseInt(value, s.shape[slot]));
			break;
		case KeyKind::Rgb: {
			// 空值 / "off" / "-" = 关掉任意颜色，回到用色号。
			if (*value == '\0' || std::strcmp(value, "off") == 0
			    || std::strcmp(value, "none") == 0 || std::strcmp(value, "-") == 0) {
				s.rgbOn[slot] = false;
				break;
			}
			float parsed[3] { 1.0f, 1.0f, 1.0f };
			if (ParseRgb(value, parsed)) {
				s.rgb[slot][0] = parsed[0];
				s.rgb[slot][1] = parsed[1];
				s.rgb[slot][2] = parsed[2];
				s.rgbOn[slot]  = true;
			}
			break;
		}
	}
}

auto LoadSettings(const D2RL::PluginContext* ctx, Settings& s) noexcept -> bool {
	if (ctx == nullptr) {
		return false;
	}
	std::array<char, 16 * 1024> buffer {};
	std::uint32_t required = 0;
	if (!ctx->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		return false;
	}

	char* cursor = buffer.data();
	while (*cursor != '\0') {
		char* lineEnd = cursor;
		while (*lineEnd != '\0' && *lineEnd != '\n') {
			++lineEnd;
		}
		const char saved = *lineEnd;
		*lineEnd = '\0';

		char* line = TrimInPlace(cursor);
		if (*line != '\0' && *line != '#' && *line != '[') {
			if (char* comment = std::strchr(line, '#'); comment != nullptr) {
				*comment = '\0';
			}
			char* eq = std::strchr(line, '=');
			if (eq != nullptr) {
				*eq = '\0';
				char* key   = TrimInPlace(line);
				char* value = TrimInPlace(eq + 1);
				const std::size_t vlen = std::strlen(value);
				if (vlen >= 2 && (value[0] == '"' || value[0] == '\'') && value[vlen - 1] == value[0]) {
					value[vlen - 1] = '\0';
					++value;
				}
				ApplySetting(s, key, value);
			}
		}

		if (saved == '\0') {
			break;
		}
		cursor = lineEnd + 1;
	}
	return true;
}

auto SaveSettings(const D2RL::PluginContext* ctx) noexcept -> bool {
	if (ctx == nullptr) {
		return false;
	}
	// v0.14.0：加上自定义规则槽位后文本变长，缓冲区从 4K 提到 8K。
	std::array<char, 8 * 1024> buffer {};
	TextBuilder out(buffer.data(), buffer.size());

	out.Append("# Loot Map - plugin settings.\n");
	out.Append("# Written by the plugin when you change something in the F7 panel.\n");
	out.Append("# mode: \"observe\" = only write a log, never change what the game draws\n");
	out.Append("#       \"color\"   = actually draw ground items on the automap\n");
	out.Append("\n[loot-map]\n\n");
	out.Append("enabled = %s\n", g_settings.enabled ? "true" : "false");
	out.Append("mode = \"%s\"\n", g_settings.observeOnly ? "observe" : "color");
	out.Append("\n# Solid star markers on the overlay (set/unique items).\n");
	out.Append("# star_size = star radius in pixels; star_offset_x/y = manual pixel correction.\n");
	out.Append("stars = %s\n", g_settings.stars ? "true" : "false");
	out.Append("star_size = %g\n", static_cast<double>(g_settings.starSize));
	out.Append("star_offset_x = %d\n", g_settings.starOffsetX);
	out.Append("star_offset_y = %d\n", g_settings.starOffsetY);
	out.Append("star_coord_mode = %d\n", g_settings.starCoordMode);
	out.Append("star_coord_debug = %s\n", g_settings.starCoordDebug ? "true" : "false");
	out.Append("hide_engine_shape = %s\n", g_settings.hideEngineShape ? "true" : "false");
	out.Append("\n# Stars stay on the map for this many seconds after the engine stops\n");
	out.Append("# reporting them (follows the map scroll). 0 = old behaviour.\n");
	out.Append("star_persist_sec = %g\n", static_cast<double>(g_settings.starPersistSec));
	out.Append("\n# When true, clicks on the F7 panel do not reach the game.\n");
	out.Append("block_game_mouse = %s\n", g_settings.blockGameMouse ? "true" : "false");
	out.Append("dim_star_under_panel = %s\n", g_settings.dimStarUnderPanel ? "true" : "false");
	out.Append("star_outline = %g\n", static_cast<double>(g_settings.starOutline));
	out.Append("star_style = %d\n", g_settings.starStyle);
	out.Append("\n# Radar range multiplier: 1 = untouched, 2/5/10 = map zoomed out that much.\n");
	out.Append("map_zoom_div = %d\n", g_settings.mapZoomDiv);
	out.Append("\n# When false, items whose quality we cannot read are left to the engine\n");
	out.Append("# (leave true to hide the engine's own little markers on those items).\n");
	out.Append("engine_colour_rescue = %s\n", g_settings.engineColourRescue ? "true" : "false");
	out.Append("own_font = %s\n", g_settings.ownFont ? "true" : "false");
	out.Append("\n# Custom rule slots (reserved): enable / colour / display name.\n");
	for (int r = 0; r < kRuleCount; ++r) {
		out.Append("%s_enable = %s\n", kRuleRows[r].key, g_settings.ruleOn[r] ? "true" : "false");
		out.Append("%s_rgb    = \"%d,%d,%d\"\n", kRuleRows[r].key,
			Rgb255(g_settings.ruleRgb[r][0]), Rgb255(g_settings.ruleRgb[r][1]), Rgb255(g_settings.ruleRgb[r][2]));
		out.Append("%s_name   = \"%s\"\n", kRuleRows[r].key, g_settings.ruleName[r]);
	}
	out.Append("\n# Which qualities to draw on the map.\n");
	for (int i = 0; i < kSlotCount; ++i) {
		out.Append("show_%-9s = %s\n", kRows[i].key, g_settings.show[i] ? "true" : "false");
	}
	out.Append("\n# Color index for each quality. 1 and 4 are skipped by the game,\n");
	out.Append("# the plugin silently turns them into 2 and 3.\n");
	for (int i = 0; i < kSlotCount; ++i) {
		out.Append("color_%-9s = %d\n", kRows[i].key, g_settings.color[i]);
	}
	out.Append("\n# Marker shape for each quality: -1 = none, 0..5 = game marker shapes.\n");
	for (int i = 0; i < kSlotCount; ++i) {
		out.Append("shape_%-9s = %d\n", kRows[i].key, g_settings.shape[i]);
	}
	out.Append("\n# TRUE colour for a quality: \"R,G,B\" in 0-255 (or #RRGGBB).\n");
	out.Append("# When set it overrides color_* and the marker gets exactly this colour.\n");
	out.Append("# \"off\" = use the game colour index from color_* instead.\n");
	for (int i = 0; i < kSlotCount; ++i) {
		if (g_settings.rgbOn[i]) {
			out.Append("rgb_%-9s = \"%d,%d,%d\"\n", kRows[i].key,
				Rgb255(g_settings.rgb[i][0]), Rgb255(g_settings.rgb[i][1]), Rgb255(g_settings.rgb[i][2]));
		} else {
			out.Append("rgb_%-9s = \"off\"\n", kRows[i].key);
		}
	}
	out.Append("\n# Byte offset of the quality field inside the item data.\n");
	out.Append("# 0 = not known yet; run in observe mode and read the plugin log.\n");
	out.Append("quality_offset = %d\n", g_settings.qualityOffset);

	if (!out.Ok()) {
		LogWarn("Loot Map: settings text did not fit the buffer; config not written.");
		return false;
	}
	return ctx->WriteConfig(out.Data());
}

// ─────────────────── 核心：单位绘制颜色钩子 ───────────────────

auto HexDump(const std::uint8_t* bytes, std::size_t count, char* out, std::size_t outSize) noexcept -> void {
	if (bytes == nullptr || out == nullptr || outSize == 0) {
		return;
	}
	std::size_t written = 0;
	for (std::size_t i = 0; i < count && written + 4 < outSize; ++i) {
		const int n = std::snprintf(out + written, outSize - written, "%02X ", static_cast<unsigned>(bytes[i]));
		if (n <= 0) {
			break;
		}
		written += static_cast<std::size_t>(n);
	}
}

auto DumpUnitRaw(const void* unit, const void* itemData) noexcept -> void {
	char head[3 * 64 + 1] {};
	HexDump(static_cast<const std::uint8_t*>(unit), 0x40, head, sizeof(head));
	char line[300] {};
	std::snprintf(line, sizeof(line), "PROBE raw unit[0..0x40] unit=%p: %s", unit, head);
	LogInfo(line);

	if (itemData == nullptr) {
		return;
	}
	char data[3 * 128 + 1] {};
	HexDump(static_cast<const std::uint8_t*>(itemData), 0x80, data, sizeof(data));
	char line2[440] {};
	std::snprintf(line2, sizeof(line2), "PROBE raw data[0..0x80] data=%p: %s", itemData, data);
	LogInfo(line2);
}

// ★ v0.16.0：rule0 蓝装前缀的判据 ——「珠宝匠」「工匠」的魔法前缀 ID。
//   本机 2026-09-22 用真实样本对照实测（tools/diff_starid.py，8 件蓝装逐字节 diff）：
//   物品数据 +0x48 处一个 word = 魔法前缀 ID ——
//     6 件「珠宝匠」/「工匠」（钩斧/骸骨魔杖/锁链甲/步战矛/连枷等）= **1209 / 1210**；
//     普通前缀（严冬之）= 1046；「工匠之良质」那件前缀 ID = **0**（它的名字来自别的字段）
//     ⇒ 用数值 ID 判定，天然不会把「工匠之」误认成「工匠」。
//   ⚠️ 这两个值来自实测当时的游戏数据；模组大更新后可能变化，如失灵，
//      重跑一次样本对比即可更新。
constexpr std::uint16_t kAffixJewelers = 1209;   // 「珠宝匠」（4 孔）
constexpr std::uint16_t kAffixArtisans = 1210;   // 「工匠」（4 孔）

auto ReadMagicPrefixId(const void* itemData) noexcept -> std::uint16_t {
	if (itemData == nullptr) {
		return 0;
	}
	return *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(itemData) + 0x48);
}

auto ReadItemQuality(const void* itemData) noexcept -> int {
	// v0.3.6 修正：偏移 0 是**合法**配置（实测就是物品数据第 0 字节），
	// 旧代码把 <=0 当成"还没确定"，导致默认配置下所有物品都被判成
	// "品质未知"、永远不画——这正是 0.3.4 / 0.3.5 看着一切正常却
	// 地图上什么都没有的真正原因。
	if (itemData == nullptr || g_settings.qualityOffset < 0) {
		return -1;
	}
	// 品质是单字节枚举（2=普通 … 8=手工），只取低字节最稳：
	// 之后几个字节混着别的字段，按 4 字节读会把品质读成大数。
	const int quality = static_cast<const std::uint8_t*>(itemData)[g_settings.qualityOffset];
	if (quality > kQualityMax) {
		return -1;
	}
	return quality;
}

// 观察模式：每个"不同"的物品单位只记一次日志
auto WriteItemObservation(std::uint32_t type, std::uint32_t id, std::uint32_t mode,
                          const void* unit, const void* dataPtr,
                          int result, int in1, int in2, int after1, int after2) noexcept -> void {
	char line[560] {};

	char unitHead[3 * 32 + 1] {};
	HexDump(static_cast<const std::uint8_t*>(unit), 32, unitHead, sizeof(unitHead));

	int quality = -1;
	if (dataPtr != nullptr) {
		quality = ReadItemQuality(dataPtr);
	}

	std::snprintf(line, sizeof(line),
		"ITEM type=%u id=%u mode=%u q=%d unit=%p data=%p ret=%d out1=%d->%d out2=%d->%d | unit[0..32]=%s",
		static_cast<unsigned>(type),
		static_cast<unsigned>(id),
		static_cast<unsigned>(mode),
		quality,
		unit,
		dataPtr,
		result,
		in1,
		after1,
		in2,
		after2,
		unitHead);
	LogInfo(line);

	if (dataPtr != nullptr) {
		char dataHead[3 * 64 + 1] {};
		HexDump(static_cast<const std::uint8_t*>(dataPtr), 64, dataHead, sizeof(dataHead));
		char line2[280] {};
		std::snprintf(line2, sizeof(line2), "    data[0..64]=%s", dataHead);
		LogInfo(line2);
	}
}

// ───────────── automap-blob 取色函数指针重定向 ─────────────

// 被 automap-blob 的取色桩直接用 call 调用（普通调用约定，栈帧天然正确）。
auto __fastcall HookBlobColorStub(const void* unit, std::int32_t* out1, std::int32_t* out2) noexcept -> bool {
	// 1) 先让游戏本体的取色逻辑照跑（怪物 / 其它单位行为一个字节不变）
	GetUnitColorIndexFn original = g_originalGetUnitColorIndex;
	const bool originalResult = (original != nullptr) ? original(unit, out1, out2) : false;

	// 2) 诊断：这条路径到底有没有被调用、有没有见到物品（严格限流）
	std::uint32_t callNo = 0;
	if (unit != nullptr) {
		callNo = g_blobStubCalls.fetch_add(1, std::memory_order_relaxed) + 1;
		if (*static_cast<const std::uint32_t*>(unit) == kUnitTypeItem) {
			const void* dataPtr = *reinterpret_cast<const void* const*>(
				static_cast<const std::uint8_t*>(unit) + 0x10);

			// 原始内存取证：最早见到的 5 个不同物品单位各 dump 一次
			const int seen = g_unitDumpCount.load(std::memory_order_relaxed);
			if (seen < kUnitDumpMax) {
				bool known = false;
				for (int i = 0; i < seen && !known; ++i) {
					known = (g_unitDumpSeen[i].load(std::memory_order_relaxed) == unit);
				}
				if (!known) {
					const int idx = g_unitDumpCount.fetch_add(1, std::memory_order_relaxed);
					if (idx < kUnitDumpMax) {
						g_unitDumpSeen[idx].store(unit, std::memory_order_relaxed);
						DumpUnitRaw(unit, dataPtr);
					}
				}
			}

			// 品质/槽位提到外面算，下面的“星品身份取证”也要用（很便宜）。
			const int qualityNow = ReadItemQuality(dataPtr);
			const int slotNow    = SlotForQuality(qualityNow);

			const std::uint32_t itemNo = g_blobItemSeen.fetch_add(1, std::memory_order_relaxed) + 1;
			if (itemNo <= 8) {
				const int quality = qualityNow;
				const int slot    = slotNow;
				const bool draw   = (quality >= 0 && g_settings.show[slot] && !g_settings.observeOnly);
				char line[300] {};
				std::snprintf(line, sizeof(line),
					"BLOB-ITEM #%u unit=%p data=%p q=%d slot=%d draw=%d color=%d (call #%u)",
					static_cast<unsigned>(itemNo), unit, dataPtr, quality, slot,
					draw ? 1 : 0, draw ? g_settings.color[slot] : -1,
					static_cast<unsigned>(callNo));
				LogInfo(line);
			}
			// （星品身份取证的 STARID 行放在真正会被调用的那个取色钩子里，见 HookGetUnitColorIndex。）
		}
	}

	// ★ v0.12.4：每个单位取色开始时先解开两个"跳过绘制"闸门，
	//   星品决策（下面）再按需拉上。怪物等其它单位因此绝不会被误伤。
	SetSkipGates(false);

	if (g_settings.observeOnly || unit == nullptr || out1 == nullptr) {
		return originalResult;
	}

	// 3) 物品：按品质决定要不要画、用什么色号 / 什么图形 / 什么颜色
	if (*static_cast<const std::uint32_t*>(unit) != kUnitTypeItem) {
		return originalResult;
	}

	const auto* bytes   = static_cast<const std::uint8_t*>(unit);
	const void* dataPtr = *reinterpret_cast<const void* const*>(bytes + 0x10);
	const int   quality = ReadItemQuality(dataPtr);
	const int   slot    = SlotForQuality(quality);
	if (quality < 0 || !g_settings.show[slot]) {
		return originalResult;
	}

	int index = g_settings.color[slot];
	if (index == kColorSkip1 || index == kColorSkip2 || index < kColorMin || index > kColorMax) {
		index = kColorMin;
	}
	*out1 = index;
	// ★ v0.12.0：星品 → 让绘制钩子顺手把标记坐标记下来给星标用。
	// ★ v0.12.4：同时拉上"跳过引擎绘制"的闸门。
	// ★ v0.12.6：out2 **不能**对星品写 -1（DrawBlob 入口会整个 ret，
	//   A 点不执行 → 星坐标断供）。保持读配置，灰块由形状分支闸门拦。
	// ★ v0.14.0：八类品质一视同仁 —— 这一类勾上了就记坐标 + 拦引擎标记。
	if (g_settings.stars && slot >= 0 && slot < kSlotCount && g_settings.show[slot]) {
		g_starSlotPending.store(slot + 1, std::memory_order_relaxed);
		g_starConfPending.store(2, std::memory_order_relaxed);   // ★ v0.14.1：直接按品质判定
		g_starUnitPending.store(unit, std::memory_order_relaxed);   // ★ v0.14.6
		if (g_settings.hideEngineShape) {
			SetSkipGates(true);
		}
	}
	if (out2 != nullptr) {
		// 以前这里写死 -1（不画额外图形），把面板上的 shape 配置整个吞掉了。
		// 现在跟本体钩子取同一个配置。
		*out2 = g_settings.shape[slot];
	}

	// ★ 任意颜色：交给光点绘制钩子（见 HookBlobIconPrep 的说明）
	if (g_settings.rgbOn[slot]) {
		SetPendingRgb(slot);
	}
	return true;
}

// 首次进游戏（本体钩子第一次被调）时安装一次；装不上就放弃，
// 只损失"地图上显示物品"，绝不影响游戏与 automap-blob 本身。
auto TryInstallBlobColorPointerHook() noexcept -> void {
	if (g_blobPtrHookInstalled.load(std::memory_order_relaxed)) {
		return;
	}

	HMODULE h = ::GetModuleHandleW(L"d2rl-plugin-automap-blob.dll");
	if (h == nullptr) {
		return;   // 还没加载（或用户没装），下次再试
	}

	const auto expected = reinterpret_cast<void*>(
		reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr)) + kGetUnitColorIndexRva);

	auto** slot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(h) + kBlobDllColorFnSlotRva);

	if (*slot != expected) {
		if (!g_blobPtrHookTried.exchange(true)) {
			char line[220] {};
			std::snprintf(line, sizeof(line),
				"Loot Map: automap-blob colour slot holds %p (expected %p); pointer hook skipped.",
				*slot, expected);
			LogWarn(line);
		}
		return;
	}

	DWORD oldProtect = 0;
	if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
		LogWarn("Loot Map: VirtualProtect on the automap-blob colour slot failed; pointer hook skipped.");
		g_blobPtrHookTried.store(true);
		return;
	}

	g_blobColorFnSaved = *slot;
	*slot = reinterpret_cast<void*>(&HookBlobColorStub);
	::VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);

	g_blobColorFnSlot = slot;
	g_blobPtrHookInstalled.store(true);
	LogInfo("Loot Map: automap-blob colour function redirected — item colours now survive its monster-only veto.");
}

// ═══════════════════════════════════════════════════════════════════════════
//  ★ 任意颜色（v0.7.0）—— 用户要的"跟地图插件一样的配色"
//
//  背景：游戏自带的地图光点只有 6 个可用色号（1 和 4 被游戏自己跳过），
//        里面**没有绿色**、也**只有一个金色**，所以「套装=绿」「暗金=金且
//        稀有=黄」这类暗黑系配色在这套色号里根本凑不出来。之前一直拿这个
//        当解释，用户回了一句「他能行你为什不行」，对的——能行。
//
//  反汇编（活进程逐条读出的，不是猜的）：
//
//    · DrawBlob（RVA 0xD6DB0）负责画一个光点。它在自己的栈上拼一个
//      "绘制样式"结构（r8 = [rsp+0x40]），然后：
//
//          RVA 0xD6E72   movaps [rsp+0x40], xmm0      ; xmm0 = {1.0, 1.0, 1.0, 0.0}
//          RVA 0xD6EE3   call   sub_858510            ; 真正去画
//
//    · sub_858510（RVA 0x858510）拿到的样式结构布局：
//
//          +0x00 f32 r      +0x04 f32 g      +0x08 f32 b      +0x0C f32 a
//          +0x10 f32 (alpha/缩放)   +0x14 i32   +0x18 i32 色号
//
//      它开头做的第一件事就是判断：
//
//          RVA 0x858651  movss xmm1, [rva 0x1CBA904]   ; = 1.0f
//          0x858659/0x858665/0x858671  ucomiss r/g/b, xmm1
//              → 三个**全都等于 1.0** → 走"色号"分支（读 +0x18，查 8 色表）
//              → 只要有一个不等于 1.0 → 走 RGBA 分支
//
//    · RGBA 分支（sub_90E670，RVA 0x90E670）干的事：
//
//          xmm2 = [rva 0x1CBB684]   ; = 255.0f
//          r = (int)(struct[+0x00] * 255)
//          g = (int)(struct[+0x04] * 255)
//          b = (int)(struct[+0x08] * 255)
//          → sub_90E5D0 打包成游戏内部颜色
//
//  也就是说：**游戏自己的光点本来就支持任意 RGB，只是被写死了 (1,1,1)
//  从而永远走色号分支。** 我们只要在它 call sub_858510 的那一处插一脚，
//  把结构里的三个浮点数换成我们想要的颜色，光点就变成任意颜色——
//  绘制全程仍在游戏自己的渲染管线里，不碰 DXGI、不碰 ImGui，
//  和地图插件（MapSense）从原理上就不可能冲突。
//
//  实现：把 RVA 0xD6EE3 那条 `call` 换成 `call 我们的小桩`。小桩是**手写的
//  机器码**（不依赖编译器怎么分配浮点参数寄存器，xmm3 原样保存/还原）：
//
//      sub    rsp, 0x38                 ; 32 字节影子空间 + 对齐
//      movaps [rsp+0x20], xmm3          ; 保存光点大小
//      call   HookBlobIconPrep          ; C++ 这边改写样式结构里的 r/g/b
//      movaps xmm3, [rsp+0x20]          ; 还原
//      add    rsp, 0x38
//      jmp    sub_858510                ; 尾跳，ret 直接回到 DrawBlob
//
//  取色钩子在决定"这个物品要画"的同时把颜色放进 g_pendingRgb；地图渲染是
//  单线程、取色后立刻绘制，所以这里一个普通全局变量就够。
// ═══════════════════════════════════════════════════════════════════════════

// ★ v0.12.6：补丁站点定案（活体字节核实 tools/live_probe_v0126.py / live_shape_sites.py）：
//    A  = 0xD6EE3  DrawBlob 内部 call sub_858510 —— 引擎标记主笔；钩子记星坐标+跳过。
//    C  = 0xD2DF5  F2 直连 call sub_858510 —— 后备（v0.12.5 实测对物品不执行）。
//    S1..S5 = F1（0xD6F10 起，含跳转表 0xD78CC）里 DrawBlob 之后的**形状分支**：
//         跳转表按取色 out2 索引，各 case 调 0xD6AA0/0xD6B20 画"额外图形"
//         —— **灰色底块的真身就是它**（v0.12.2 把 out2 从写死 -1 改成读配置
//         shape[slot]=0 后，v0.12.3 起才露出来）。星品时经 g_skipSiteB 闸门
//         就地返回 → 额外图形不画；out2 保持 0，DrawBlob/A 点照常跑，
//         星坐标不受影响。
//    注意：out2 **不能**对星品写 -1 —— DrawBlob 入口 cmp edx,-1 直接 ret，
//         A 点不执行 → 星坐标断供（星直接消失）。
struct BlobPatchSite {
	std::uintptr_t callRva;      // call 指令的 RVA
	std::uint8_t   expected[5];  // 该处原始字节（call rel32），不匹配就报警不补
	std::uintptr_t targetRva;    // 该 call 原本调的函数（小桩尾跳目标）
};
constexpr BlobPatchSite kBlobPatchSites[] {
	{ 0x000D6EE3, { 0xE8, 0x28, 0x16, 0x78, 0x00 }, 0x000858510 },   // A：DrawBlob 内部
	{ 0x000D2DF5, { 0xE8, 0x16, 0x57, 0x78, 0x00 }, 0x000858510 },   // C：F2 直连（后备）
	{ 0x000D77F2, { 0xE8, 0xA9, 0xF2, 0xFF, 0xFF }, 0x000D6AA0 },    // S1：形状 case
	{ 0x000D7818, { 0xE8, 0x83, 0xF2, 0xFF, 0xFF }, 0x000D6AA0 },    // S2：形状 case
	{ 0x000D784F, { 0xE8, 0x4C, 0xF2, 0xFF, 0xFF }, 0x000D6AA0 },    // S3：形状 case
	{ 0x000D7872, { 0xE8, 0xA9, 0xF2, 0xFF, 0xFF }, 0x000D6B20 },    // S4：形状 case
	{ 0x000D7894, { 0xE8, 0x87, 0xF2, 0xFF, 0xFF }, 0x000D6B20 },    // S5：形状 case
};
constexpr int kBlobPatchSiteCount =
	static_cast<int>(sizeof(kBlobPatchSites) / sizeof(kBlobPatchSites[0]));

constexpr std::uintptr_t kBlobIconTargetRva = 0x000858510;   // sub_858510 本身

struct BlobPatchState {
	std::uint8_t  saved[5] {};     // 补丁前的原始字节（卸载时还原）
	std::uint8_t* page = nullptr;  // 小桩所在页（卸载时释放）
	bool          patched = false;
};
BlobPatchState g_blobPatchStates[kBlobPatchSiteCount] {};
std::atomic<bool> g_blobIconHookInstalled { false };

// 钩子 A（DrawBlob 内部那笔）：★ v0.12.6 恢复 v0.12.3 的完整逻辑（实测有效）——
//   消费 starSlot（单令牌）→ 记星坐标（point+0/+4）→ 需要时就地返回跳过引擎绘制。
//   C 点钩子保留作后备；pending 是单令牌，谁先抢到谁记，不会画双星。
//   第 4 个 float 形参 = xmm3 = 引擎传给 sub_858510 的"缩放"（寄存器原样保留）。
auto __fastcall HookBlobIconPrep(void* obj, const void* point, void* style, float zoom) noexcept -> int {
	g_siteATotal.fetch_add(1, std::memory_order_relaxed);   // ★ v0.18.7 诊断：A 点被叫总次数
	RememberRenderCtx(obj, zoom);   // ★ v0.12.8：给雷达倍率留一份"当帧 ctx + 缩放"
	const int starSlot = g_starSlotPending.exchange(0, std::memory_order_relaxed) - 1;
	const int starConf = g_starConfPending.exchange(0, std::memory_order_relaxed);   // ★ v0.14.1
	const void* starUnit = g_starUnitPending.exchange(nullptr, std::memory_order_relaxed);   // ★ v0.14.6
	if (starSlot >= 0) {
		g_siteAHits.fetch_add(1, std::memory_order_relaxed);
		float rawX = 0.0f, rawY = 0.0f, absX = 0.0f, absY = 0.0f;
		if (StarScreenPos(obj, point, zoom, rawX, rawY, absX, absY)) {
			RecordStar(rawX, rawY, absX, absY, starSlot, starConf, starUnit);
		}
		if (g_settings.hideEngineShape) {
			// ★ v0.15.2：这一笔本身就是 A 点闸门要拦的那笔 —— 顺手把它消费掉，
			//   别让它一直武装着去误吃共用这条绘制口的其它绘制（v0.14.8"地图缺块"的教训）。
			//   B 点/形状闸门（灰底块）保留：那一笔在后面才画，还要靠它们拦。
			(void)g_skipSiteA.exchange(0, std::memory_order_relaxed);
			g_pendingRgb.active = false;   // 作废待用色，防止串到别的单位身上
			g_siteASkipped.fetch_add(1, std::memory_order_relaxed);
			return 1;                      // 就地返回，引擎这一笔不画
		}
	}

	if (g_skipSiteA.exchange(0, std::memory_order_relaxed) != 0) {   // ★ v0.14.9：回退成一次性（v0.14.8 的常开会压掉别人共用的绘制口）
		g_pendingRgb.active = false;   // 作废待用色，防止串到别的单位身上
		g_siteASkipped.fetch_add(1, std::memory_order_relaxed);
		return 1;                      // 就地返回，引擎这一笔不画
	}

	if (style == nullptr || !g_pendingRgb.active) {
		return 0;   // 正常进引擎绘制
	}
	auto* channels = static_cast<float*>(style);
	float r = g_pendingRgb.r;
	float g = g_pendingRgb.g;
	float b = g_pendingRgb.b;
	// 陷阱：游戏判断"要不要走任意颜色"的标准是 r/g/b **是否全等于 1.0**。
	// 所以用户真想要纯白 (255,255,255) 时会被当成"用色号"。
	// 这里把蓝通道退一格（254/255），视觉上看不出来，但能保证走任意颜色。
	if (r == 1.0f && g == 1.0f && b == 1.0f) {
		b = 254.0f / 255.0f;
	}
	channels[0] = r;
	channels[1] = g;
	channels[2] = b;
	g_pendingRgb.active = false;
	g_rgbDraws.fetch_add(1, std::memory_order_relaxed);
	return 0;   // 正常进引擎绘制
}

// 钩子 C（标记函数直连那笔）：★ v0.12.6 实测这条路对物品标记不执行
// （v0.12.5 全会话零命中），保留作后备：万一某些路径真的走 C，
// pending 单令牌谁先抢到谁记坐标，闸门照常跳灰块。
auto __fastcall HookBlobShapeBase(void* obj, const void* point, void* /*style*/, float zoom) noexcept -> int {
	RememberRenderCtx(obj, zoom);   // ★ v0.12.8：雷达倍率取样
	g_siteCHits.fetch_add(1, std::memory_order_relaxed);
	const int starSlot = g_starSlotPending.exchange(0, std::memory_order_relaxed) - 1;
	const int starConf = g_starConfPending.exchange(0, std::memory_order_relaxed);   // ★ v0.14.1
	const void* starUnit = g_starUnitPending.exchange(nullptr, std::memory_order_relaxed);   // ★ v0.14.6
	if (starSlot >= 0) {
		float rawX = 0.0f, rawY = 0.0f, absX = 0.0f, absY = 0.0f;
		if (StarScreenPos(obj, point, zoom, rawX, rawY, absX, absY)) {
			RecordStar(rawX, rawY, absX, absY, starSlot, starConf, starUnit);
		}
	}
	if (g_skipSiteB.exchange(0, std::memory_order_relaxed) != 0) {   // ★ v0.14.9：同上，回退成一次性
		g_pendingRgb.active = false;   // 作废待用色，防止串到别的单位身上
		return 1;                      // 就地返回，灰底块也不画
	}
	return 0;
}

// 钩子 S1..S5（F1 跳转表形状分支里的 5 个形状绘制调用点 = 灰底块真身）：
// 星品时 g_skipSiteB 拉闸 → 就地返回，"额外图形"不画；非星品照常进原函数。
// 参数不认识也不需要认识 —— 小桩会原样保存/还原所有易失寄存器。
auto __fastcall HookShapeDrawer(void*, const void*, void*, float) noexcept -> int {
	g_shapeTotal.fetch_add(1, std::memory_order_relaxed);   // ★ v0.18.7 诊断：形状点被叫总次数
	if (g_skipSiteB.exchange(0, std::memory_order_relaxed) != 0) {   // ★ v0.14.9：同上，回退成一次性
		g_shapeHits.fetch_add(1, std::memory_order_relaxed);
		g_pendingRgb.active = false;
		return 1;                      // 就地返回，形状/底块不画
	}
	return 0;
}

// ─────────────────────── 机器码小桩（绝对地址版）───────────────────────
//  v0.9.0 修正：v0.8.0 这里用的是"相对跳转（rel32）"，结果日志报
//      "the code stub is out of rel32 range; true colours are off."
//  原因是插件 DLL 和游戏主体在 64 位地址空间里相隔几十 GB，rel32（±2GB）
//  根本够不着 —— 任意颜色因此从来没有真正生效过。
//
//  现在改成"绝对地址"指令，小桩放内存任何位置都能用：
//      mov rax, imm64 ; call rax     （跳进我们的 C++ 函数）
//      mov rax, imm64 ; jmp  rax     （跳回原来的游戏函数）
//
//  ★ 为什么必须保存那一堆寄存器：我们是"透明地"插在中间 —— 原函数
//    sub_858510 的三个参数就在 rcx / rdx / r8 里。我们的 C++ 函数（编译器
//    自动生成）会随手用这些寄存器当草稿纸，若不还原，原函数就会收到一堆
//    垃圾参数。所以除 rax 之外的易失寄存器（rcx, rdx, r8~r11, xmm0~xmm5）
//    都在调用我们的函数前存好、回来后原样复原。
//    rax 是**故意不存的**：它是钩子函数的返回值（见下面 v0.18.6 那条）。
//
//  栈帧布局（刚进小桩时 rsp % 16 == 8，因为 call 压了一个返回地址）：
//      sub rsp, 0xC8        -> rsp % 16 == 0
//      [rsp+0x00 .. 0x1F]   被调函数的"影子空间"，32 字节，我们不能占用
//      [rsp+0x28 .. 0x50]   6 个通用寄存器（rcx/rdx/r8~r11；0x20 空着不用）
//      [rsp+0x60 .. 0xB0]   6 个 xmm 寄存器（偏移都是 16 的倍数，movaps 才合法）
// v0.12.3：小桩尾部加了 4 字节分支 —— 我们的钩子返回 1（"这是画星的物品"）时
// 就地 ret 回 DrawBlob，**根本不进引擎的绘制函数**（X 彻底消失，地图上只剩星）；
// 返回 0 才尾跳进原函数。
// ★ v0.18.6：**判定必须看钩子的返回值**——rax 不恢复，AL 一路留着返回值到最后
//   （见 kStubGpSlots 上面的长注释）。rax 不再进出保存区 ⇒ 251 → 235 字节。
//   这个数字必须和 BuildBlobIconStub 实际发射的字节数**逐字节**相等，
//   否则函数直接返回失败（宁可不补丁，也不要有半截机器码在游戏里跑）。
constexpr std::size_t   kBlobStubSize  = 235;
constexpr std::uint32_t kBlobStubFrame = 0xC8;
constexpr std::uint32_t kBlobStubXmmLo = 0x60;   // 第 0 个 xmm 的偏移

struct StubRegSlot {
	int           code;   // 0=rax 1=rcx 2=rdx 8=r8 9=r9 10=r10 11=r11
	std::uint32_t disp;
};
// 只保存绘制函数真正可能用到的易失寄存器（rdi/rsi/rbx/rbp 是"非易失"，
// 我们的 C++ 函数自己会保护，不用管；rsp 靠 add 还原）
//
// ★★★ v0.18.6 修正（很重要，别改回去）：**这里绝对不能保存/恢复 rax。**
//   小桩尾部用 `test al,al` 判断"我们的钩子函数有没有要求跳过这一笔"——
//   而 rax 就是钩子返回值所在。以前 kStubGpSlots 里带着 rax，那一句恢复
//   会把返回值覆盖成"调用点当时的旧 rax"：于是"跳不跳"跟钩子彻底无关，
//   变成由调用点决定的固定值 —— 结果引擎所有走这批调用点的绘制
//   （NPC 名字、门/箱子/传送点的白色十字标记…）被成片吃掉。
//   症状：装上插件后地图上少了这些标记；因为 MapSense 自己也画标记，
//   以前一直没被发现（拆掉 MapSense 才露出来）。
//   rax 本来就是**易失**寄存器、原函数也不靠它收参数 ⇒ 不恢复它才是正确做法。
constexpr StubRegSlot kStubGpSlots[] {
	{  1, 0x28 }, {  2, 0x30 }, {  8, 0x38 },
	{  9, 0x40 }, { 10, 0x48 }, { 11, 0x50 },
};

auto EmitU32(std::uint8_t*& w, std::uint32_t v) noexcept -> void {
	std::memcpy(w, &v, sizeof(v));
	w += sizeof(v);
}
auto EmitU64(std::uint8_t*& w, std::uint64_t v) noexcept -> void {
	std::memcpy(w, &v, sizeof(v));
	w += sizeof(v);
}
// mov [rsp+disp32], reg64
auto EmitStubStoreGp(std::uint8_t*& w, int reg, std::uint32_t disp) noexcept -> void {
	*w++ = (reg >= 8) ? 0x4C : 0x48;
	*w++ = 0x89;
	*w++ = static_cast<std::uint8_t>(0x84 | ((reg & 7) << 3));   // mod=10, rm=100(SIB)
	*w++ = 0x24;
	EmitU32(w, disp);
}
// mov reg64, [rsp+disp32]
auto EmitStubLoadGp(std::uint8_t*& w, int reg, std::uint32_t disp) noexcept -> void {
	*w++ = (reg >= 8) ? 0x4C : 0x48;
	*w++ = 0x8B;
	*w++ = static_cast<std::uint8_t>(0x84 | ((reg & 7) << 3));
	*w++ = 0x24;
	EmitU32(w, disp);
}
// movaps [rsp+disp32], xmmN   或   movaps xmmN, [rsp+disp32]
auto EmitStubMovaps(std::uint8_t*& w, int xmm, std::uint32_t disp, bool store) noexcept -> void {
	*w++ = 0x0F;
	*w++ = store ? 0x29 : 0x28;
	*w++ = static_cast<std::uint8_t>(0x84 | ((xmm & 7) << 3));
	*w++ = 0x24;
	EmitU32(w, disp);
}
// mov rax, imm64
auto EmitStubMovRaxImm64(std::uint8_t*& w, std::uint64_t v) noexcept -> void {
	*w++ = 0x48; *w++ = 0xB8;
	EmitU64(w, v);
}

// ★ v0.12.4：小桩生成改为"钩子函数作参数"—— 两个调用点各挂各的钩子
//（A 点 = HookBlobIconPrep，C 点 = HookBlobShapeBase；v0.12.5 把 B 站点换成 C）。
using BlobHookFn = int (__fastcall*)(void*, const void*, void*, float) noexcept;

auto BuildBlobIconStub(std::uint8_t* stub, std::uint8_t* target, BlobHookFn hook) noexcept -> bool {
	std::uint8_t* w = stub;

	// sub rsp, 0xC8
	*w++ = 0x48; *w++ = 0x81; *w++ = 0xEC;
	EmitU32(w, kBlobStubFrame);

	for (const StubRegSlot& slot : kStubGpSlots) {
		EmitStubStoreGp(w, slot.code, slot.disp);
	}
	for (int i = 0; i < 6; ++i) {
		EmitStubMovaps(w, i, kBlobStubXmmLo + static_cast<std::uint32_t>(i) * 16u, true);
	}

	// mov rax, <我们的函数> ; call rax
	EmitStubMovRaxImm64(w, reinterpret_cast<std::uint64_t>(hook));
	*w++ = 0xFF; *w++ = 0xD0;

	for (int i = 0; i < 6; ++i) {
		EmitStubMovaps(w, i, kBlobStubXmmLo + static_cast<std::uint32_t>(i) * 16u, false);
	}
	for (const StubRegSlot& slot : kStubGpSlots) {
		EmitStubLoadGp(w, slot.code, slot.disp);
	}

	// add rsp, 0xC8
	*w++ = 0x48; *w++ = 0x81; *w++ = 0xC4;
	EmitU32(w, kBlobStubFrame);

	// ★ v0.12.3：test al,al ; jne +12（越过"mov rax/jmp rax"共 12 字节直达 ret）
	//   钩子返回非 0 = 星品物品 = 引擎一个字节都不画，就地返回 DrawBlob。
	*w++ = 0x84; *w++ = 0xC0;
	*w++ = 0x75; *w++ = 0x0C;

	// mov rax, <原来的游戏函数> ; jmp rax（尾跳，原函数的返回地址没变过）
	EmitStubMovRaxImm64(w, reinterpret_cast<std::uint64_t>(target));
	*w++ = 0xFF; *w++ = 0xE0;

	// .skip: ret
	*w++ = 0xC3;

	return static_cast<std::size_t>(w - stub) == kBlobStubSize;
}

auto TryInstallBlobIconColourPatch() noexcept -> void {
	if (g_blobIconHookInstalled.load(std::memory_order_relaxed)) {
		return;
	}

	auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));

	// 每个调用点独立走一遍：校验字节 → 就近分配小桩页 → 生成小桩 → 改 call。
	// 任一点失败只影响自己这一笔（A 失败 = 没真彩/星坐标；S1..S5 失败 = 灰底块拦不掉）。
	for (int si = 0; si < kBlobPatchSiteCount; ++si) {
		BlobPatchState& st = g_blobPatchStates[si];
		if (st.patched) {
			continue;
		}
		const BlobPatchSite& desc = kBlobPatchSites[si];
		auto* site = base + desc.callRva;

		if (std::memcmp(site, desc.expected, sizeof(desc.expected)) != 0) {
			LogWarn("Loot Map: a blob draw call site does not match this game build; "
			        "true colours are off (colour indices still work).");
			continue;   // 下次再试别的点
		}

		// 1) 在调用点 ±1.75GB 范围内找一页可执行内存。
		//    v0.12.0 教训：直接 VirtualAlloc(nullptr,...) 系统从高处随便给一页，
		//    离 D2R.exe 的调用点超过 ±2GB，E8 相对跳转（32 位位移）装不下 →
		//    "cannot reach the stub" → 真彩和星标坐标全灭。
		//    按 64MB 步进从近到远逐个地址试探，被占用就换下一个。
		void* page = nullptr;
		constexpr std::intptr_t kStep  = 0x04000000;   // 64MB
		constexpr std::intptr_t kLimit = 0x6C000000;   // 1.6875GB（< 2GB，留足余量）
		const std::intptr_t siteAddr = reinterpret_cast<std::intptr_t>(site);
		for (std::intptr_t off = kStep; off <= kLimit && page == nullptr; off += kStep) {
			const std::intptr_t candidates[2] { siteAddr - off, siteAddr + off };
			for (const std::intptr_t cand : candidates) {
				if (cand < 0x10000LL || cand > 0x7FFFFFFEFFLL) {
					continue;   // 太低（空指针保留区）/太高（用户态外）
				}
				page = ::VirtualAlloc(reinterpret_cast<void*>(cand), 0x1000,
				                      MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
				if (page != nullptr) {
					break;
				}
			}
		}
		if (page == nullptr) {
			// 兜底：让系统自己挑（有可能超 ±2GB，后面 delta 检查会拦住）
			page = ::VirtualAlloc(nullptr, 0x1000,
			                      MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		}
		if (page == nullptr) {
			LogWarn("Loot Map: could not allocate a code stub; true colours are off.");
			continue;
		}

		// 2) 生成小桩（各站点各挂各的钩子；尾跳目标 = 该 call 原本调的函数）
		BlobHookFn hook = (si == 0) ? &HookBlobIconPrep
		                : (si == 1) ? &HookBlobShapeBase
		                    : &HookShapeDrawer;
		if (!BuildBlobIconStub(static_cast<std::uint8_t*>(page), base + desc.targetRva, hook)) {
			LogWarn("Loot Map: the code stub did not assemble as expected; true colours are off.");
			::VirtualFree(page, 0, MEM_RELEASE);
			continue;
		}
		::FlushInstructionCache(::GetCurrentProcess(), page, kBlobStubSize);

		// 3) 把这条 call 指向小桩
		DWORD oldProtect = 0;
		if (!::VirtualProtect(site, sizeof(desc.expected), PAGE_EXECUTE_READWRITE, &oldProtect)) {
			LogWarn("Loot Map: VirtualProtect on the blob draw call site failed; true colours are off.");
			::VirtualFree(page, 0, MEM_RELEASE);
			continue;
		}

		std::memcpy(st.saved, site, sizeof(st.saved));

		const auto stubAddr = reinterpret_cast<std::intptr_t>(page);
		const auto nextAddr = reinterpret_cast<std::intptr_t>(site) + sizeof(st.saved);
		const std::intptr_t delta = stubAddr - nextAddr;
		if (delta < -0x7FFFFFFFLL || delta > 0x7FFFFFFFLL) {
			LogWarn("Loot Map: the blob draw call cannot reach the stub; true colours are off.");
			::VirtualProtect(site, sizeof(st.saved), oldProtect, &oldProtect);
			::VirtualFree(page, 0, MEM_RELEASE);
			continue;
		}

		std::uint8_t patch[sizeof(st.saved)] {};
		patch[0] = 0xE8;
		const std::int32_t rel = static_cast<std::int32_t>(delta);
		std::memcpy(patch + 1, &rel, sizeof(rel));
		std::memcpy(site, patch, sizeof(patch));
		::FlushInstructionCache(::GetCurrentProcess(), site, sizeof(patch));
		::VirtualProtect(site, sizeof(st.saved), oldProtect, &oldProtect);

		st.page    = static_cast<std::uint8_t*>(page);
		st.patched = true;
		{
			char line[160] {};
			std::snprintf(line, sizeof(line),
				"Loot Map: blob draw call site %s (RVA 0x%06X) patched (stub delta=%+d MB).",
				(si == 0) ? "A" : (si == 1) ? "C" : "S",
				static_cast<unsigned>(desc.callRva),
				static_cast<int>((reinterpret_cast<std::intptr_t>(page) - (siteAddr + 5)) >> 20));
			LogInfo(line);
		}
	}

	// 全部就位（或判定永久失败）才不再重试
	int done = 0;
	int dead = 0;
	for (int si = 0; si < kBlobPatchSiteCount; ++si) {
		if (g_blobPatchStates[si].patched) {
			++done;
		}
	}
	if (done == kBlobPatchSiteCount) {
		g_blobIconHookInstalled.store(true);
		LogInfo("Loot Map: all blob/shape call sites patched — stars only on the map, true colours available.");
	} else {
		// 有失败点：如果是字节校验不过（版本不匹配），重试也没用；标死，只报一次。
		// （分配/保护类失败下一帧还会重试，靠 g_blobIconHookInstalled 不置位。）
		for (int si = 0; si < kBlobPatchSiteCount; ++si) {
			if (!g_blobPatchStates[si].patched) {
				auto* site = base + kBlobPatchSites[si].callRva;
				if (std::memcmp(site, kBlobPatchSites[si].expected, 5) != 0) {
					++dead;
				}
			}
		}
		if (dead == kBlobPatchSiteCount - done && dead > 0 && done == 0) {
			LogWarn("Loot Map: all blob draw call sites mismatch this game build; giving up.");
			g_blobIconHookInstalled.store(true);
		}
	}
}

// ★ v0.13.0：从投票表里挑出"胜出"的色号。要求票数 ≥4 且严格多于第二名两倍，
//   否则返回 false（说明还没学出来 / 学到的不可信）。
// ★ v0.14.7：**下标 0 永远不参与** —— 0 = "引擎没给颜色"，
//   它恰好是绝大多数普通物品的返回值。以前会把它学成"暗金色号"，
//   然后满地的药水/卷轴全被判成暗金（"只爆一件却一片星星"的根因）。
auto PickEngineColour(const std::atomic<std::uint32_t>* votes, int& outColour) noexcept -> bool {
	std::uint32_t best = 0;
	std::uint32_t second = 0;
	int           bestIdx = -1;
	for (int i = 1; i < kColorVoteSlots; ++i) {   // ← 从 1 开始：跳过 0
		const std::uint32_t v = votes[i].load(std::memory_order_relaxed);
		if (v > best) {
			second = best;
			best = v;
			bestIdx = i;
		} else if (v > second) {
			second = v;
		}
	}
	if (bestIdx <= 0 || best < 4 || best <= second * 2) {
		return false;
	}
	outColour = bestIdx;
	return true;
}

auto __fastcall HookGetUnitColorIndex(const void* unit, std::int32_t* out1, std::int32_t* out2) noexcept -> bool {
	GetUnitColorIndexFn original = g_originalGetUnitColorIndex;

	if (original == nullptr || unit == nullptr || out1 == nullptr || out2 == nullptr) {
		return original != nullptr ? original(unit, out1, out2) : false;
	}

	// 首次被调用时尝试重定向 automap-blob 的取色函数指针；
	// 还没加载就下次再试（函数内部自带"已装即返回"的短路）。
	TryInstallBlobColorPointerHook();
	// 同一个时机把光点绘制的那条 call 补上，任意颜色就有了。
	TryInstallBlobIconColourPatch();

	// 每个单位都从"取色"开始，所以这里清掉上一条待用颜色，
	// 避免某个单位被跳过后它的颜色漏到下一个单位身上。
	ClearPendingRgb();
	// ★ v0.15.2：闸门**只在"换了一个【物品】单位"时才解开**。
	//   v0.14.9 的写法有个大漏洞：这里的"换单位"判定对**所有单位**生效 ——
	//   而取色钩子对**怪物/其它单位也会被调**，它们虽然走不到下面的物品分支，
	//   却会在这里把刚拉上的闸门解开 ⇒ 星品的标记失去保护被引擎画出来
	//   （"绿色小星星 + 灰色底块"经常出现的真因：地图上怪物成堆，随时路过）。
	//   · 一次性闸门 + 每次取色都解（v0.14.7 及以前）要求"取色→绘制"严格 1:1，本来就漏；
	//   · v0.14.8 试过"常开"，这些绘制点被别的模块共用，把地图压缺了一块；
	//   ⇒ 现在的折中：**只有"换成另一个物品"才解开**；怪物等其它单位既不解开、
	//     也不记录（它们的取色与物品标记绘制无关，物品判定在下面 type 检查处）。
	static const void* s_lastGateUnit = nullptr;   // 只在渲染线程访问
	if (unit != s_lastGateUnit
	    && *static_cast<const std::uint32_t*>(unit) == kUnitTypeItem) {
		SetSkipGates(false);
		s_lastGateUnit = unit;
	}

	const std::uint32_t type = *static_cast<const std::uint32_t*>(unit);

	// ★ v0.12.6 限流诊断（每 5 秒、计数有变化才打）：星管线每一环各一个计数。
	//   断供时一行日志定位断点：item=0 → 取色钩子没被调；item>0 而 siteA=0 →
	//   A 点补丁没生效；siteA>0 而 stars=0 → 叠加层没画；以此类推。
	{
		static std::atomic<std::uint64_t> s_lastTick { 0 };
		static std::atomic<std::uint64_t> s_lastSig   { 0 };
		const std::uint64_t now  = ::GetTickCount64();
		const std::uint64_t last = s_lastTick.load(std::memory_order_relaxed);
		if (now - last >= 5000) {
			const std::uint64_t sig =
				  static_cast<std::uint64_t>(g_itemCalls) << 0
				| static_cast<std::uint64_t>(g_siteAHits.load(std::memory_order_relaxed)) << 20
				| static_cast<std::uint64_t>(g_siteCHits.load(std::memory_order_relaxed)) << 40
				| static_cast<std::uint64_t>(g_starSamples.load(std::memory_order_relaxed)) << 52;
			if (sig != s_lastSig.load(std::memory_order_relaxed) || now - last >= 30000) {
				s_lastTick.store(now, std::memory_order_relaxed);
				s_lastSig.store(sig, std::memory_order_relaxed);
				char dline[400] {};
				std::snprintf(dline, sizeof(dline),
					"diag: item=%u hidden=%u blobStub=%u siteA=%u/%u skipA=%u siteC=%u shape=%u/%u "
					"starsDrawn=%u overwrites=%u engSet=%d engUniq=%d rescued=%u rule0=%u",
					static_cast<unsigned>(g_itemCalls),
					static_cast<unsigned>(g_hiddenItems.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_blobStubCalls.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_siteAHits.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_siteATotal.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_siteASkipped.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_siteCHits.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_shapeHits.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_shapeTotal.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_starSamples.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_pendingOverwrites.load(std::memory_order_relaxed)),
					g_engineSetColor.load(std::memory_order_relaxed),
					g_engineUniqueColor.load(std::memory_order_relaxed),
					static_cast<unsigned>(g_starRecovered.load(std::memory_order_relaxed)),
					static_cast<unsigned>(g_rule0Hits.load(std::memory_order_relaxed)));
				LogInfo(dline);
			}
		}
	}

	if (type != kUnitTypeItem) {
		return original(unit, out1, out2);   // 不是物品：一个字节都不动
	}

	const auto* bytes = static_cast<const std::uint8_t*>(unit);
	const std::uint32_t id      = *reinterpret_cast<const std::uint32_t*>(bytes + 0x08);
	const std::uint32_t mode    = *reinterpret_cast<const std::uint32_t*>(bytes + 0x0C);
	const void*         dataPtr = *reinterpret_cast<const void* const*>(bytes + 0x10);

	const std::int32_t beforeOut1 = *out1;
	const std::int32_t beforeOut2 = *out2;
	const bool originalResult = original(unit, out1, out2);
	// ★ v0.13.0：原始函数调用**之后**的 out1 = 引擎自己给这件物品选的色号。
	//   这是"引擎怎么分类这件物品"的第一手信息（见文件头 v0.13.0 说明）。
	const std::int32_t engineOut1 = *out1;

	++g_itemCalls;

	if (g_settings.observeOnly) {
		if (g_seenUnits.find(unit) == g_seenUnits.end() && g_seenUnits.size() < kMaxObserveUnitsHard) {
			g_seenUnits.insert(unit);
			++g_itemUnits;
			WriteItemObservation(type, id, mode, unit, dataPtr,
				originalResult ? 1 : 0, beforeOut1, beforeOut2, *out1, *out2);
		}
		return originalResult;   // 观察模式：绝不改变游戏行为
	}

	// ── 着色模式 ──
	//   ★ v0.15.2：这里**不再**像 v0.12.4 那样"每个物品取色先解开一次闸门"——
	//   那会把"上一件星品刚拉上的闸门"被这一件普通物品的取色解掉，
	//   等引擎真正来画上一件的标记时就漏了（绿色小星星/灰底块）。
	//   解闸门只发生在上面"换了一个物品单位"的那一处；
	//   同一件物品反复取色只会重新拉上闸门（星品决策在下面）。
	const int  quality = ReadItemQuality(dataPtr);
	int        slot    = SlotForQuality(quality);

	// ── ① 学"引擎的套装色号 / 暗金色号"（v0.13.0）──
	//   只用我们判得准的物品投票；每 256 次取色重算一次 argmax。
	if (static_cast<unsigned>(engineOut1) < static_cast<unsigned>(kColorVoteSlots)) {
		if (slot == kSlotSet) {
			g_voteSetColor[engineOut1].fetch_add(1, std::memory_order_relaxed);
		} else if (slot == kSlotUnique) {
			g_voteUniqueColor[engineOut1].fetch_add(1, std::memory_order_relaxed);
		}
		if ((g_voteTick.fetch_add(1, std::memory_order_relaxed) % 256) == 0) {
			int learned = -1;
			if (PickEngineColour(g_voteSetColor, learned)) {
				g_engineSetColor.store(learned, std::memory_order_relaxed);
			}
			if (PickEngineColour(g_voteUniqueColor, learned)) {
				g_engineUniqueColor.store(learned, std::memory_order_relaxed);
			}
		}
	}

	// ── ② 决定这一件要不要画星 ──
	//   ★ v0.14.0：八类品质**一视同仁** —— 只要这一类在面板上是勾着的，就按
	//   "星品"处理（记坐标 + 用我们选的颜色画星）。
	//   ★ v0.14.7：**"引擎色号兜底"默认关闭，并且 0 永远不算有效色号。**
	//   实测（2026-09-22 20:11 的 STARID 行）这条兜底的危害：
	//     `engSet=-1 engUniq=0`（学到的"暗金色号"居然是 0），而引擎对**普通物品**
	//     返回的色号也正好是 0（engOut1=0）⇒ 满地的药水/卷轴/魔法装备全部命中
	//     "暗金色号"被判成暗金 ⇒ 用户看到"只爆一件套装，地图上却一片星星"。
	//   只要那一类品质没勾（比如只留套装+暗金），其余物品就会掉进这条兜底里，
	//   所以它是"星星爆炸"的放大器。**0 = 引擎没给颜色，不是颜色。**
	int starSlot = -1;
	int starConf = 0;   // ★ v0.14.1：2 = 品质直接判定；1 = 引擎色号兑底救回来的
	if (g_settings.stars) {
		if (slot >= 0 && slot < kSlotCount && g_settings.show[slot]) {
			starSlot = slot;
			starConf = 2;
		} else if (g_settings.engineColourRescue) {
			const int engineSet    = g_engineSetColor.load(std::memory_order_relaxed);
			const int engineUnique = g_engineUniqueColor.load(std::memory_order_relaxed);
			const int colour       = static_cast<int>(engineOut1);
			// ★ v0.14.7：色号必须是 1..kColorMax 的"真颜色"，且不能等于 0。
			if (engineUnique > 0 && colour == engineUnique && g_settings.show[kSlotUnique]) {
				starSlot = kSlotUnique;
			} else if (engineSet > 0 && colour == engineSet && g_settings.show[kSlotSet]) {
				starSlot = kSlotSet;
			}
			if (starSlot >= 0) {
				slot = starSlot;
				starConf = 1;
				g_starRecovered.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	// ★ v0.16.0：rule0 = 蓝装前缀「珠宝匠」/「工匠」（4 孔蓝装）。
	//   命中 = 魔法品质 + 前缀 ID ∈ {1209,1210}（本机样本实测，见 kAffixJewelers 注释）。
	//   命中给一个"规则虚拟槽位"（kSlotCount + 规则号 = 8），颜色走 ruleRgb[0] ——
	//   **与「蓝色(魔法)」勾选互相独立**：蓝装没勾，这两类照样亮蓝星；
	//   蓝装勾了，普通蓝装走普通星、这两类走规则星。
	if (g_settings.stars && starSlot < 0 && g_settings.ruleOn[0]
	    && slot == kSlotMagic) {
		const std::uint16_t prefixId = ReadMagicPrefixId(dataPtr);
		if (prefixId == kAffixJewelers || prefixId == kAffixArtisans) {
			starSlot = kSlotCount + 0;   // 虚拟槽位：0 号规则（= kRuleRows[0]）
			starConf = 3;                // 3 = 规则命中（诊断/STARID 里看得出来）
			g_rule0Hits.fetch_add(1, std::memory_order_relaxed);
		}
	}

	// ★ v0.14.7：**物品一律由我们接管**（压掉引擎自己画的"小绿星 / 灰块"）。
	//   以前只接管"勾上的那些品质"，没勾的品质就露出引擎自己的标记 ——
	//   正是为了补它们才有了"色号兜底"，而兜底又会误判（见上）。
	//   压掉绘制**不影响记坐标**：坐标是在 A 点钩子里记的，钩子照旧会被调用。
	const bool draw = true;
	// 下面所有按品质查表的地方都必须用合法下标（品质没读出来时 slot 是 -1）。
	const int slotIdx = (slot >= 0 && slot < kSlotCount) ? slot : kSlotNormal;
	if (draw) {
		// ★ v0.14.6：星品身份取证（放在这条**真正会被调用**的钩子里 —— 之前那版
		//   错放到了另一个已经不再使用的取色钩子里，所以一行都没打出来）。
		//   同一件物品（同一 unit）最多每 3 秒写一行：把"这颗星画在什么物品上"
		//   记成可复查的证据（底材类型号 + 引擎色号 + 我们读到的品质 + 原始字节）。
		if (starSlot >= 0 && unit != nullptr) {
			const auto* ub = static_cast<const std::uint8_t*>(unit);
			const std::uint32_t typeNo = *reinterpret_cast<const std::uint32_t*>(ub + 0x04);
			const std::uint32_t key =
				(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(unit) & 0xFFFFu) << 8)
				| (static_cast<std::uint32_t>(starSlot) & 0xFFu);
			const std::uint64_t nowIdMs = static_cast<std::uint64_t>(::GetTickCount64());
			int idx = -1;
			for (int i = 0; i < kStarIdSlots; ++i) {
				if (g_starIdSeen[i].key == key) {
					idx = i;
					break;
				}
			}
			if (idx < 0) {
				for (int i = 0; i < kStarIdSlots; ++i) {
					if (g_starIdSeen[i].key == 0) {
						idx = i;
						break;
					}
				}
				if (idx < 0) {
					std::uint64_t oldest = ~static_cast<std::uint64_t>(0);
					for (int i = 0; i < kStarIdSlots; ++i) {
						if (g_starIdSeen[i].nextMs < oldest) {
							oldest = g_starIdSeen[i].nextMs;
							idx    = i;
						}
					}
				}
				if (idx >= 0) {
					g_starIdSeen[idx].key    = key;
					g_starIdSeen[idx].nextMs = 0;
				}
			}
			if (idx >= 0 && nowIdMs >= g_starIdSeen[idx].nextMs) {
				g_starIdSeen[idx].nextMs = nowIdMs + 3000;
				// ★ v0.15.3：取证从 16 字节加长到 **128 字节**（dataPtr+0x00..0x7F）。
				//   目的：找"蓝装前缀"的判据 —— 用户仓库里有现成的 [珠宝匠]/[工匠] 蓝装，
				//   把它们和普通蓝装一起丢到地上，对比 STARID 行就能圈出是哪几个字节
				//   在区分前缀（词缀 ID / 孔数），然后 rule0 就用那几个字节做判定。
				char db[3 * 128 + 1] {};
				HexDump(static_cast<const std::uint8_t*>(dataPtr), 128, db, sizeof(db));
				char sid[1200] {};
				std::snprintf(sid, sizeof(sid),
					"STARID slot=%d conf=%d q=%d mode=%u id=%u engOut1=%d txt=%u pfx=%u unit=%p data=%p data[0..128]=%s",
					starSlot, starConf, quality, static_cast<unsigned>(mode),
					static_cast<unsigned>(id), static_cast<int>(engineOut1),
					static_cast<unsigned>(typeNo),
					static_cast<unsigned>(ReadMagicPrefixId(dataPtr)), unit, dataPtr, db);
				LogInfo(sid);
			}
		}
		// ★ v0.12.0：套装/暗金 → 让绘制钩子顺手把标记坐标记下来给星标用。
		// ★ v0.12.4：同时拉上"跳过引擎绘制"的闸门（X 笔 + 形状分支笔都不画）。
		if (starSlot >= 0) {
			g_starSlotPending.store(starSlot + 1, std::memory_order_relaxed);
			g_starConfPending.store(starConf, std::memory_order_relaxed);   // ★ v0.14.1
			g_starUnitPending.store(unit, std::memory_order_relaxed);       // ★ v0.14.6：带上单位指针，当帧去重用
			if (g_settings.hideEngineShape) {
				SetSkipGates(true);
			}
		}
		// ★ v0.18.8：**关掉的类别 = 引擎一个字都不许画**，走**和星品完全一样的路**：
		//   拉闸门 → 引擎照常来画 → 小桩在绘制口就地返回（整笔含名字都不画）。
		//
		//   为什么不再用前两版那两招（实测都不行）：
		//     · out2 = -1：只挡住"光点"那一笔，挡不住引擎自己画的标记（用户看到满地 X）；
		//     · out1 = "跳过色号"：**根本没有"跳过的色号"这回事** —— 写 1 之后引擎
		//       把它当颜色用，于是所有这类物品的标记变成了**绿色的 X**（v0.18.7 的锅）。
		//   闸门这条路是验证过的：星品 8407:8407:8407 一一对应，说明"取色 → 紧接着那一笔
		//   绘制"是同一个单位、顺序紧挨着，所以闸门必定被它自己那一笔消费，不会误吃别人。
		const bool hideEngine = (starSlot < 0) && g_settings.hideEngineShape;
		int index = g_settings.color[slotIdx];
		if (index == kColorSkip1 || index == kColorSkip2 || index < kColorMin || index > kColorMax) {
			index = kColorMin;   // 给个一定能画的值兜底
		}
		if (hideEngine) {
			SetSkipGates(true);
			g_hiddenItems.fetch_add(1, std::memory_order_relaxed);
		}
		*out1 = index;
		// out2 = 额外图形选择器：-1 = 不画；0~5 = 游戏自带的 6 种标记图形。
		// ★ v0.12.6 注意：这里**不能**对星品写 -1！DrawBlob 入口 cmp edx,-1 会
		//   整个直接返回 → A 点不执行 → 星坐标断供（星直接消失）。
		// ★ v0.18.8：隐藏品也**不能**写 -1 —— 写了它的绘制就不来了，闸门没人消费，
		//   反过来会去误吃下一笔（别人的标记）。让引擎照常走完，交给小桩拦截。
		*out2 = g_settings.shape[slotIdx];

		// ★ 任意颜色：如果这一档配了 rgb_xxx，就把颜色交给光点绘制钩子，
		//   由它改写游戏样式结构里的 r/g/b（见 HookBlobIconPrep 的说明）。
		if (g_settings.rgbOn[slotIdx]) {
			SetPendingRgb(slotIdx);
		}
		return true;
	}
	return originalResult;
}

// ══════════════ 原生面板（SDK Panel 服务）—— v0.8.0 ══════════════
//  这是"跟游戏自带窗口一模一样"的设置界面：把一份 JSON 布局注册给加载器，
//  由游戏自己的 UI 系统把它画出来。
//
//  为什么走这条路（而不是继续 ImGui 自绘）：
//    v0.3.4 的自绘面板要自己抓交换链，和 MapSense 撞车，游戏启动 0.5 秒就崩。
//    原生面板完全不碰 DirectX / ImGui —— 它只是"往游戏的 UI 系统里加一个窗口"，
//    从原理上就不可能与地图类插件冲突。
//
//  做法照 SDK 附带的 ui-panel / shared-events 两个官方示例：
//    1) registerResource 把布局 JSON 注册到
//       data/global/ui/layouts/loot-map/LootMapPanelhd.json
//    2) registerPanel 注册面板，逻辑名 loot-map/LootMapPanel
//       （布局根节点的 name 必须和它逐字相同）
//    3) 按钮的 onClickMessage 是"目标:命令:文本"三段式，
//       由 registerUiMessageListener 的回调接住 → 改设置 → 存盘
//
//  形态上的一个取舍：SDK 没有"改控件文字"的接口，所以选色不做轮播，
//  而是每一档直接摆 13 个小色块，"点哪个就是哪个"，不需要任何状态同步。

namespace NativePanel {

constexpr char kLogicalName[] = "loot-map/LootMapPanel";
constexpr char kLocalId[]     = "LootMapPanel";
constexpr char kLayoutPath[]  = "data/global/ui/layouts/loot-map/LootMapPanelhd.json";
constexpr char kMsgTarget[]   = "LootMap";   // 按钮消息的目标名（三段式的第一段）

// 背板（游戏自带无标题弹窗底图）左上角相对屏幕中心的位置，尺寸约 1254x540。
// 下面所有坐标都写成"背板内部坐标"，再统一偏移成根节点的局部坐标。
constexpr int kFrameX    = -627;
constexpr int kFrameY    = -270;
constexpr int kRowTop    = 250;
constexpr int kRowStep   = 34;

constexpr int kChkX      = 40;    // 勾选框（该品质要不要显示）
constexpr int kLabelX    = 76;    // 品质名
constexpr int kLabelW    = 92;

// 形状按钮组。原先一行只放 13 个色块，已经顶到背板右边缘；
// 这一版要再塞进 7 个形状按钮，所以把色块收窄，给形状腾出位置。
constexpr int kShapeX    = 176;
constexpr int kShapeStep = 44;
constexpr int kShapeW    = 40;
constexpr int kShapeH    = 26;

constexpr int kCellX     = 506;   // 色块组
constexpr int kCellStep  = 50;
constexpr int kCellW     = 44;
constexpr int kCellH     = 26;

// 形状选项：按钮下标 n(0..6) → 引擎的 shape 值 = kShapeValue[n]。
// -1 = 不画图形（只剩基础光点），0..5 = 游戏自带的 6 种图形
// （跳转表见 0xD78CC，反汇编确认）。
constexpr int kShapeOptionCount = 7;
constexpr int kShapeValue[kShapeOptionCount] { -1, 0, 1, 2, 3, 4, 5 };
constexpr const char* kShapeOptionLabel[kShapeOptionCount] {
	"不画", "1", "2", "3", "4", "5", "6",
};

// 引擎自己那张 13 色地图配色表（反汇编 sub_90E5D0 得到，
// 表在 [全局对象+0x228]，循环 13 次）。插件给引擎的颜色如果正好取这 13 个之一，
// 就不会被它的"就近匹配"挪到别的颜色上。
struct PaletteEntry {
	const char* name;
	int         r;
	int         g;
	int         b;
};

constexpr PaletteEntry kPalette[] {
	{ "白",   255, 255, 255 },
	{ "红",   255,  77,  77 },
	{ "亮绿",   0, 255,   0 },
	{ "蓝",   105, 105, 255 },
	{ "暗金", 199, 179, 119 },
	{ "灰",   105, 105, 105 },
	{ "黑",     0,   0,   0 },
	{ "亮金", 208, 194, 125 },
	{ "橙",   255, 168,   0 },
	{ "黄",   255, 255, 100 },
	{ "暗绿",   0, 128,   0 },
	{ "紫",   174,   0, 255 },
	{ "绿",     0, 200,   0 },
};
constexpr int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

// 面板上用的短名字（kRows 里那串又长带括号，摆在表格里太挤）
constexpr const char* kShortLabel[kSlotCount] {
	"普通", "超强", "魔法", "稀有", "套装", "暗金", "手工", "未知",
};

const D2RL::ResourceService*     g_resources = nullptr;
const D2RL::PanelService*        g_panels    = nullptr;
const D2RL::SharedEventService*  g_events    = nullptr;
const D2RL::ThreadService*       g_threads   = nullptr;
const D2RL::WidgetService*       g_widgets   = nullptr;
D2RL::Panels::RegistrationHandle     g_panel    = D2RL::Panels::InvalidHandle;
D2RL::Resources::RegistrationHandle  g_resource = D2RL::Resources::InvalidHandle;
D2RL::SharedEvents::ListenerHandle   g_listener = D2RL::SharedEvents::InvalidHandle;
std::atomic<bool> g_installed { false };

auto ResultText(D2RL::Panels::Result r) noexcept -> const char* {
	switch (r) {
	case D2RL::Panels::Result::Success:         return "Success";
	case D2RL::Panels::Result::InvalidArgument: return "InvalidArgument";
	case D2RL::Panels::Result::Unsupported:     return "Unsupported";
	case D2RL::Panels::Result::Unavailable:     return "Unavailable";
	case D2RL::Panels::Result::Conflict:        return "Conflict";
	case D2RL::Panels::Result::NotFound:        return "NotFound(布局没加载成功)";
	case D2RL::Panels::Result::Busy:            return "Busy(线程不对或还在切换)";
	case D2RL::Panels::Result::OwnerInactive:   return "OwnerInactive";
	case D2RL::Panels::Result::OwnerMismatch:   return "OwnerMismatch";
	case D2RL::Panels::Result::StaleHandle:     return "StaleHandle";
	case D2RL::Panels::Result::CallbackFault:   return "CallbackFault";
	default:                                    return "Unknown";
	}
}

auto SlotFromKey(const char* key) noexcept -> int {
	if (key == nullptr) {
		return -1;
	}
	for (int i = 0; i < kSlotCount; ++i) {
		if (std::strcmp(key, kRows[i].key) == 0) {
			return i;
		}
	}
	return -1;
}

// 当前配置离哪个调色板颜色最近（纯显示用）
auto PaletteIndexOf(int slot) noexcept -> int {
	if (slot < 0 || slot >= kSlotCount) {
		return 0;
	}
	const int cur[3] {
		Rgb255(g_settings.rgb[slot][0]),
		Rgb255(g_settings.rgb[slot][1]),
		Rgb255(g_settings.rgb[slot][2]),
	};
	int  best   = 0;
	long bestD2 = 0x7FFFFFFFL;
	for (int i = 0; i < kPaletteCount; ++i) {
		const long dr = static_cast<long>(cur[0] - kPalette[i].r);
		const long dg = static_cast<long>(cur[1] - kPalette[i].g);
		const long db = static_cast<long>(cur[2] - kPalette[i].b);
		const long d2 = dr * dr + dg * dg + db * db;
		if (d2 < bestD2) {
			bestD2 = d2;
			best   = i;
		}
	}
	return best;
}

// 把某一档设成调色板里的第 index 个颜色。
// +0.25 的用意见 Settings::rgb 那段注释：让引擎 (int)(v*255) 的截断取整
// 精确落回原始字节，同时让纯白不再等于 (1,1,1) 这个"改用色号表"的哨兵值。
auto ApplyPalette(int slot, int index) noexcept -> void {
	if (slot < 0 || slot >= kSlotCount) {
		return;
	}
	if (index < 0) {
		index = 0;
	}
	if (index >= kPaletteCount) {
		index = kPaletteCount - 1;
	}
	const PaletteEntry& e = kPalette[index];
	g_settings.rgb[slot][0] = (static_cast<float>(e.r) + 0.25f) / 255.0f;
	g_settings.rgb[slot][1] = (static_cast<float>(e.g) + 0.25f) / 255.0f;
	g_settings.rgb[slot][2] = (static_cast<float>(e.b) + 0.25f) / 255.0f;
	g_settings.rgbOn[slot]  = true;
}

auto Append(std::string& out, const char* format, ...) noexcept -> void {
	char    buffer[2048] {};
	va_list args {};
	va_start(args, format);
	const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (written > 0) {
		out += buffer;
	}
}

// 在运行时把整份布局拼出来，而不是写一个几百行的字面量：
// 8 档 × 13 个色块本身是重复结构，循环生成既短又不容易写错。
auto BuildLayout(std::string& out) noexcept -> void {
	out.clear();
	out.reserve(64 * 1024);

	Append(out,
		"{\n"
		"  \"type\": \"Panel\",\n"
		"  \"name\": \"%s\",\n"
		"  \"fields\": { \"anchor\": { \"x\": 0.5, \"y\": 0.5 }, \"priority\": 8500 },\n"
		"  \"children\": [\n",
		kLogicalName);

	// 背板
	Append(out,
		"    { \"type\": \"ImageWidget\", \"name\": \"Frame\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d },\n"
		"                    \"filename\": \"Panel\\\\Modals\\\\Modal_No_Title_BG\" } },\n",
		kFrameX, kFrameY);

	// 标题
	Append(out,
		"    { \"type\": \"TextBoxWidget\", \"name\": \"Title\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": 900, \"height\": 44 },\n"
		"                    \"text\": \"掉落物地图标记\",\n"
		"                    \"style\": \"$StyleSettingsTitle\" } },\n",
		kFrameX + 96, kFrameY + 44);

	// 说明
	Append(out,
		"    { \"type\": \"TextBoxWidget\", \"name\": \"Hint\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": 1100, \"height\": 76 },\n"
		"                    \"text\": \"方框 = 这一档要不要显示在地图上；形状 = 点哪个用哪个（黄色高亮的就是当前用的）；色块 = 颜色，点一下就换。改动立刻生效并存盘。\",\n"
		"                    \"style\": \"$StyleModalDialogDescription\" } },\n",
		kFrameX + kChkX, kFrameY + 92);

	// 模式按钮
	Append(out,
		"    { \"type\": \"ButtonWidget\", \"name\": \"ModeButton\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": 210, \"height\": 36 },\n"
		"                    \"filename\": \"PANEL\\\\Modals\\\\ModalButton\",\n"
		"                    \"onClickMessage\": \"%s:mode:switch\" } },\n"
		"    { \"type\": \"TextBoxWidget\", \"name\": \"ModeHint\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": 760, \"height\": 36 },\n"
		"                    \"text\": \"← 点这个按钮切换「只观察（不改地图）/ 实际上色」\",\n"
		"                    \"style\": \"$StyleModalDialogDescription\" } },\n",
		kFrameX + 96, kFrameY + 180, kMsgTarget,
		kFrameX + 320, kFrameY + 182);

	// 表头：品质 / 形状 / 颜色
	Append(out,
		"    { \"type\": \"TextBoxWidget\", \"name\": \"Hdr\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": 28 },\n"
		"                    \"text\": \"品质\",\n"
		"                    \"style\": \"$StyleModalDialogDescription\" } },\n"
		"    { \"type\": \"TextBoxWidget\", \"name\": \"Hdr3\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": 28 },\n"
		"                    \"text\": \"地图上的图形（左→右 不画 1 2 3 4 5 6）\",\n"
		"                    \"style\": \"$StyleModalDialogDescription\" } },\n"
		"    { \"type\": \"TextBoxWidget\", \"name\": \"Hdr2\",\n"
		"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": 28 },\n"
		"                    \"text\": \"颜色（白 红 亮绿 蓝 暗金 灰 黑 亮金 橙 黄 暗绿 紫 绿）\",\n"
		"                    \"style\": \"$StyleModalDialogDescription\" } },\n",
		kFrameX + kLabelX, kFrameY + 222, kLabelW,
		kFrameX + kShapeX, kFrameY + 222, kShapeOptionCount * kShapeStep,
		kFrameX + kCellX, kFrameY + 222, kPaletteCount * kCellStep);

	for (int i = 0; i < kSlotCount; ++i) {
		const int y = kFrameY + kRowTop + i * kRowStep;

		// 该品质的显示开关
		Append(out,
			"    { \"type\": \"ToggleButtonWidget\", \"name\": \"SH_%s\",\n"
			"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": 26, \"height\": 26 },\n"
			"                    \"filename\": \"Lobby\\\\CreateGame\\\\CreateGame_AdvancedCheckbox\",\n"
			"                    \"untoggledFrame\": 0, \"untoggledPressedFrame\": 1,\n"
			"                    \"untoggledHoveredFrame\": 3, \"untoggledDisabledFrame\": 2,\n"
			"                    \"toggledFrame\": 4, \"toggledPressedFrame\": 5, \"toggledHoveredFrame\": 6,\n"
			"                    \"onClickMessage\": \"%s:show:%s\" } },\n",
			kRows[i].key, kFrameX + kChkX, y, kMsgTarget, kRows[i].key);

		// 品质名
		Append(out,
			"    { \"type\": \"TextBoxWidget\", \"name\": \"LB_%s\",\n"
			"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": 26 },\n"
			"                    \"text\": \"%s\",\n"
			"                    \"style\": \"$StyleModalDialogDescription\" } },\n",
			kRows[i].key, kFrameX + kLabelX, y, kLabelW, kShortLabel[i]);

		// 7 个形状按钮。每个按钮里套一个"高亮块"，平时由插件在运行时
		// 只把"当前选中"的那一个设成可见（WidgetService::setWidgetVisible），
		// 于是玩家能直接看出这一档现在用的是哪种图形。
		//
		// 为什么要这么绕：SDK 没有"改控件文字/颜色"的接口，布局里也没有
		// "visible" 字段，所以"显示当前值"只能用运行时显隐来做。
		// 万一显隐调用没成功（面板没开、服务不可用），最坏情况就是 7 个
		// 高亮块全亮 —— 难看但能用，不会破坏面板。
		for (int n = 0; n < kShapeOptionCount; ++n) {
			Append(out,
				"    { \"type\": \"ButtonWidget\", \"name\": \"SP_%s_%d\",\n"
				"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": %d },\n"
				"                    \"filename\": \"PANEL\\\\Modals\\\\ModalButton\",\n"
				"                    \"tooltipString\": \"图形 %s\",\n"
				"                    \"onClickMessage\": \"%s:shape:%s.%d\" },\n"
				"      \"children\": [\n"
				"        { \"type\": \"RectangleWidget\", \"name\": \"SPH_%s_%d\",\n"
				"          \"fields\": { \"rect\": { \"x\": 2, \"y\": 2, \"width\": %d, \"height\": %d },\n"
				"                        \"color\": [ 1.0, 0.85, 0.2, 0.45 ] } }\n"
				"      ] },\n",
				kRows[i].key, n,
				kFrameX + kShapeX + n * kShapeStep, y, kShapeW, kShapeH,
				kShapeOptionLabel[n],
				kMsgTarget, kRows[i].key, n,
				kRows[i].key, n,
				kShapeW - 4, kShapeH - 4);
		}

		// 13 个色块：按钮（可点）+ 里面的矩形（真正的颜色）
		//
		// ★ 分隔符为什么用 '.' 而不是 ':'：
		//   onClickMessage 的格式是"目标:命令:文本"三段。游戏自己的布局里
		//   最多也就是三段（如 PanelManager:OpenPanel:SettingsPanel），
		//   从没出现过"文本里再带冒号"的例子 —— 万一加载器是按冒号切成
		//   固定三段、只取第三段，那 "LootMap:color:unique:5" 的文本就只剩
		//   "unique"，色块点了没反应。改用 '.' 就没有这个歧义：
		//   无论加载器怎么切，第三段永远是完整的 "unique.5"。
		for (int c = 0; c < kPaletteCount; ++c) {
			Append(out,
				"    { \"type\": \"ButtonWidget\", \"name\": \"SW_%s_%d\",\n"
				"      \"fields\": { \"rect\": { \"x\": %d, \"y\": %d, \"width\": %d, \"height\": %d },\n"
				"                    \"filename\": \"PANEL\\\\Modals\\\\ModalButton\",\n"
				"                    \"tooltipString\": \"%s\",\n"
				"                    \"onClickMessage\": \"%s:color:%s.%d\" },\n"
				"      \"children\": [\n"
				"        { \"type\": \"RectangleWidget\", \"name\": \"SWC_%s_%d\",\n"
				"          \"fields\": { \"rect\": { \"x\": 3, \"y\": 3, \"width\": %d, \"height\": %d },\n"
				"                        \"color\": [ %.4f, %.4f, %.4f, 1.0 ] } }\n"
				"      ] },\n",
				kRows[i].key, c,
				kFrameX + kCellX + c * kCellStep, y, kCellW, kCellH,
				kPalette[c].name,
				kMsgTarget, kRows[i].key, c,
				kRows[i].key, c,
				kCellW - 6, kCellH - 6,
				static_cast<double>(kPalette[c].r) / 255.0,
				static_cast<double>(kPalette[c].g) / 255.0,
				static_cast<double>(kPalette[c].b) / 255.0);
		}
	}

	// 收尾：最后一个元素后面多了一个逗号，必须去掉（严格 JSON 不接受尾逗号）
	if (out.size() >= 2) {
		out.erase(out.size() - 2);
	}
	out += "\n  ]\n}\n";
}

// ── 形状高亮：让「当前用的那种图形」在面板上亮着 ──
//
//  SDK 没有"改控件文字 / 改控件颜色"的接口，布局里也没有 visible 字段，
//  所以"把当前值显示出来"只能靠运行时显隐：每种图形各配一个高亮块，
//  只把当前那个设成可见。WidgetService 正好提供 findPanel/findWidget/
//  setWidgetVisible 这三件事。
//
//  它的失败模式很温和：找不到就跳过，最坏情况是 7 个高亮块全亮
//  （难看但能用）。所以这里只记日志，绝不中断其它功能。
constexpr char kMarkerPrefix[] = "SPH_";

// 把 "<品质键>.<数字>" 拆开，冒号也兼容（老版本用的是冒号）。
// 这样解析端不用管布局里到底写的是哪种分隔符。
auto SplitKeyValue(const char* text, char* key, std::size_t keySize, int* value) noexcept -> bool {
	if (text == nullptr) {
		return false;
	}
	const char* sep = nullptr;
	for (const char* p = text; *p != '\0'; ++p) {
		if (*p == '.' || *p == ':') {
			sep = p;
			break;
		}
	}
	if (sep == nullptr) {
		return false;
	}
	const auto length = static_cast<std::size_t>(sep - text);
	if (length == 0 || length >= keySize) {
		return false;
	}
	std::memcpy(key, text, length);
	key[length] = '\0';
	*value      = static_cast<int>(std::strtol(sep + 1, nullptr, 10));
	return true;
}

auto __cdecl SyncShapeMarkersOnUiThread(const D2RL::PluginContext*, void*) noexcept -> void {
	if (g_widgets == nullptr || g_context == nullptr) {
		return;
	}

	D2RL::Widgets::WidgetHandle panel = D2RL::Widgets::InvalidHandle;
	const D2RL::Widgets::Result found = g_widgets->findPanel(g_context, kLogicalName, &panel);
	if (found != D2RL::Widgets::Result::Success || panel == D2RL::Widgets::InvalidHandle) {
		char line[200] {};
		std::snprintf(line, sizeof(line),
			"loot-map panel: findPanel('%s') -> %u; shape highlight skipped.",
			kLogicalName, static_cast<unsigned>(found));
		LogInfo(line);
		return;
	}

	unsigned lit    = 0;
	unsigned missed = 0;
	for (int i = 0; i < kSlotCount; ++i) {
		// 配置里的值是 -1..5，按钮下标是 0..6，差一个 1。
		const int active = g_settings.shape[i] + 1;
		for (int n = 0; n < kShapeOptionCount; ++n) {
			char name[64] {};
			std::snprintf(name, sizeof(name), "%s%s_%d", kMarkerPrefix, kRows[i].key, n);

			D2RL::Widgets::WidgetHandle handle = D2RL::Widgets::InvalidHandle;
			if (g_widgets->findWidget(g_context, panel, name, &handle) != D2RL::Widgets::Result::Success ||
			    handle == D2RL::Widgets::InvalidHandle) {
				++missed;
				continue;
			}
			const bool want = (n == active);
			if (g_widgets->setWidgetVisible(g_context, handle, want) == D2RL::Widgets::Result::Success && want) {
				++lit;
			}
		}
	}

	char line[220] {};
	std::snprintf(line, sizeof(line),
		"loot-map panel: shape highlight synced (%u lit, %u not found).", lit, missed);
	LogInfo(line);
}

auto QueueShapeMarkerSync() noexcept -> void {
	if (g_widgets == nullptr || g_context == nullptr) {
		return;
	}
	if (g_threads != nullptr &&
	    g_threads->runOnUiThread(g_context, SyncShapeMarkersOnUiThread, nullptr) == D2RL::Threads::Result::Success) {
		return;
	}
	SyncShapeMarkersOnUiThread(g_context, nullptr);   // 兜底：本来就在 UI 线程
}

// ── 按钮消息回调（加载器在 UI 更新期间调用，即 UI 线程）──
auto __cdecl OnUiMessage(const D2RL::PluginContext* context,
                         const D2RL::SharedEvents::UiMessageEvent* event,
                         void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
	if (context == nullptr || event == nullptr ||
	    event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}
	if (event->target == nullptr || event->command == nullptr ||
	    std::strcmp(event->target, kMsgTarget) != 0) {
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}

	const char* text = (event->text != nullptr) ? event->text : "";
	char        line[220] {};

	if (std::strcmp(event->command, "show") == 0) {
		const int slot = SlotFromKey(text);
		if (slot < 0) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		g_settings.show[slot] = !g_settings.show[slot];
		SaveSettings(g_context);
		std::snprintf(line, sizeof(line), "loot-map panel: show[%s] -> %s",
			kRows[slot].key, g_settings.show[slot] ? "ON" : "OFF");
		LogInfo(line);
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}

	if (std::strcmp(event->command, "color") == 0) {
		// text 的格式是 "<品质键>.<调色板下标>"，例如 "unique.5"。
		// 为什么不用冒号：整条消息是"目标:命令:文本"三段式，而游戏自己的
		// 布局里从没出现过"文本里再带冒号"，所以冒号有可能被当成分隔符切掉，
		// 导致文本只剩 "<品质键>"。用 '.' 就没有这个歧义。
		// 这里两种分隔符都收，将来哪怕换回来也不会失灵。
		const char* sep = nullptr;
		for (const char* p = text; *p != '\0'; ++p) {
			if (*p == '.' || *p == ':') {
				sep = p;
				break;
			}
		}
		if (sep == nullptr) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		char        key[40] {};
		const auto  keyLength = static_cast<std::size_t>(sep - text);
		if (keyLength == 0 || keyLength >= sizeof(key)) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		std::memcpy(key, text, keyLength);
		key[keyLength] = '\0';
		const int index = static_cast<int>(std::strtol(sep + 1, nullptr, 10));
		const int slot  = SlotFromKey(key);
		if (slot < 0 || index < 0 || index >= kPaletteCount) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		ApplyPalette(slot, index);
		SaveSettings(g_context);
		std::snprintf(line, sizeof(line), "loot-map panel: rgb[%s] -> %s (%d,%d,%d)",
			kRows[slot].key, kPalette[index].name, kPalette[index].r, kPalette[index].g, kPalette[index].b);
		LogInfo(line);
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}

	if (std::strcmp(event->command, "shape") == 0) {
		// text 的格式是 "<品质键>.<按钮下标>"，例如 "unique.3"。
		// 下标 0..6 对应引擎的 shape 值 -1..5（kShapeValue）。
		char key[40] {};
		int  n = -1;
		if (!SplitKeyValue(text, key, sizeof(key), &n)) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		const int slot = SlotFromKey(key);
		if (slot < 0 || n < 0 || n >= kShapeOptionCount) {
			return D2RL::SharedEvents::UiMessageAction::Continue;
		}
		g_settings.shape[slot] = kShapeValue[n];
		SaveSettings(g_context);
		std::snprintf(line, sizeof(line), "loot-map panel: shape[%s] -> %d (%s)",
			kRows[slot].key, g_settings.shape[slot], kShapeOptionLabel[n]);
		LogInfo(line);
		QueueShapeMarkerSync();   // 让黄色高亮跟着走
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}

	if (std::strcmp(event->command, "mode") == 0) {
		g_settings.observeOnly = !g_settings.observeOnly;
		SaveSettings(g_context);
		std::snprintf(line, sizeof(line), "loot-map panel: mode -> %s",
			g_settings.observeOnly ? "observe" : "color");
		LogInfo(line);
		return D2RL::SharedEvents::UiMessageAction::Continue;
	}

	return D2RL::SharedEvents::UiMessageAction::Continue;
}

// ── 开关面板（必须在 UI 线程上做，所以统一丢给 ThreadService 排队）──
auto __cdecl ToggleOnUiThread(const D2RL::PluginContext*, void*) noexcept -> void {
	if (g_panels == nullptr || g_panel == D2RL::Panels::InvalidHandle) {
		return;
	}
	const D2RL::Panels::Result r = g_panels->togglePanel(g_context, g_panel);
	char                       line[200] {};
	std::snprintf(line, sizeof(line), "loot-map panel: togglePanel -> %s", ResultText(r));
	LogInfo(line);

	// 面板刚打开（或刚关掉）时同步一次形状高亮。
	// 排队一个"下一个 UI 更新"的任务，这样面板的控件已经建好了，
	// findWidget 才找得到东西。
	if (r == D2RL::Panels::Result::Success) {
		QueueShapeMarkerSync();
	}
}

auto ToggleFromCommand() noexcept -> bool {
	if (!g_installed.load(std::memory_order_relaxed) ||
	    g_panels == nullptr || g_panel == D2RL::Panels::InvalidHandle) {
		return false;
	}
	if (g_threads != nullptr &&
	    g_threads->runOnUiThread(g_context, ToggleOnUiThread, nullptr) == D2RL::Threads::Result::Success) {
		return true;
	}
	ToggleOnUiThread(g_context, nullptr);   // 兜底：调用点本来就在 UI 线程时直接做
	return true;
}

auto PanelCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	if (!ToggleFromCommand()) {
		command->plugin->WriteConsoleMessage("loot-map: native panel is not available in this build/session.");
		return D2RL::ConsoleCommandResult::Failed;
	}
	command->plugin->WriteConsoleMessage("loot-map: native panel toggled (details in logs/loot-map.log).");
	return D2RL::ConsoleCommandResult::Handled;
}

// ── 注册（在 D2RLoaderLoadPlugin 里调用）──
auto Install(const D2RL::PluginContext* ctx) noexcept -> bool {
	if (ctx == nullptr) {
		return false;
	}

	const D2RL::ResourceService* resources = nullptr;
	if (ctx->QueryService(&resources) != D2RL::ServiceQueryResult::Success || resources == nullptr ||
	    !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize)) {
		LogWarn("loot-map panel: ResourceService unavailable; native panel skipped.");
		return false;
	}

	const D2RL::PanelService* panels = nullptr;
	if (ctx->QueryService(&panels) != D2RL::ServiceQueryResult::Success || panels == nullptr ||
	    !D2RL::HasPanelServiceField(panels, D2RL::PanelServiceRequiredSize)) {
		LogWarn("loot-map panel: PanelService unavailable; native panel skipped.");
		return false;
	}

	const D2RL::SharedEventService* events = nullptr;
	if (ctx->QueryService(&events) != D2RL::ServiceQueryResult::Success || events == nullptr ||
	    !D2RL::HasSharedEventServiceField(events, D2RL::SharedEventServiceRequiredSize)) {
		LogWarn("loot-map panel: SharedEventService unavailable; native panel skipped.");
		return false;
	}

	std::string layout;
	BuildLayout(layout);
	if (layout.size() < 128) {
		LogWarn("loot-map panel: layout build produced nothing; native panel skipped.");
		return false;
	}

	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.flags      = 0,
		.path       = kLayoutPath,
		.bytes      = layout.data(),
		.byteCount  = static_cast<std::uint64_t>(layout.size()),
	};
	D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
	const D2RL::Resources::Result resourceResult = resources->registerResource(ctx, &resource, &resourceHandle);
	if (resourceResult != D2RL::Resources::Result::Success) {
		char line[240] {};
		std::snprintf(line, sizeof(line), "loot-map panel: registerResource failed (%u) path=%s bytes=%u",
			static_cast<unsigned>(resourceResult), kLayoutPath, static_cast<unsigned>(layout.size()));
		LogWarn(line);
		return false;
	}

	const D2RL::Panels::PanelRegistration registration {
		.structSize = D2RL::Panels::PanelRegistrationSize,
		.flags      = D2RL::Panels::PanelFlags::CloseOnEscape,
		.localId    = kLocalId,
	};
	D2RL::Panels::RegistrationHandle handle = D2RL::Panels::InvalidHandle;
	const D2RL::Panels::Result panelResult = panels->registerPanel(ctx, &registration, &handle);
	if (panelResult != D2RL::Panels::Result::Success) {
		char line[240] {};
		std::snprintf(line, sizeof(line), "loot-map panel: registerPanel failed (%s)",
			ResultText(panelResult));
		LogWarn(line);
		return false;
	}

	const D2RL::SharedEvents::UiMessageListener listener {
		.structSize = D2RL::SharedEvents::UiMessageListenerSize,
		.flags      = 0,
		.priority   = 0,
		.reserved   = 0,
		.callback   = OnUiMessage,
		.userData   = nullptr,
	};
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	const D2RL::SharedEvents::Result listenerResult = events->registerUiMessageListener(ctx, &listener, &listenerHandle);
	if (listenerResult != D2RL::SharedEvents::Result::Success) {
		char line[240] {};
		std::snprintf(line, sizeof(line), "loot-map panel: registerUiMessageListener failed (%u)",
			static_cast<unsigned>(listenerResult));
		LogWarn(line);
		return false;
	}

	const D2RL::ThreadService* threads = nullptr;
	if (ctx->QueryService(&threads) == D2RL::ServiceQueryResult::Success && threads != nullptr &&
	    D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize)) {
		g_threads = threads;
	}

	// WidgetService 只用来做「形状高亮」：把当前选中的那个高亮块设成可见、
	// 其余设成不可见。拿不到也不影响面板本身，所以只是警告一下。
	const D2RL::WidgetService* widgets = nullptr;
	if (ctx->QueryService(&widgets) == D2RL::ServiceQueryResult::Success && widgets != nullptr &&
	    D2RL::HasWidgetServiceField(widgets, D2RL::WidgetServiceRequiredSize)) {
		g_widgets = widgets;
	} else {
		LogWarn("loot-map panel: WidgetService unavailable; the shape highlight will not follow your choice.");
	}

	g_resources = resources;
	g_panels    = panels;
	g_events    = events;
	g_panel     = handle;
	g_resource  = resourceHandle;
	g_listener  = listenerHandle;
	g_installed.store(true, std::memory_order_relaxed);

	char line[240] {};
	std::snprintf(line, sizeof(line),
		"loot-map panel: registered native SDK panel '%s' (layout %u bytes, ui-thread service %s).",
		kLogicalName, static_cast<unsigned>(layout.size()), (g_threads != nullptr) ? "yes" : "no");
	LogInfo(line);

	ctx->RegisterConsoleCommand("lootmap-ui", PanelCommand, "Open / close the native Loot Map settings panel.");
	return true;
}

// 卸载时不主动反注册：D2RCore 会在插件卸载时自动收拾面板与资源，
// 而这两个调用从卸载线程走很容易撞上 Busy。这里只清自己的状态。
auto Uninstall() noexcept -> void {
	g_installed.store(false, std::memory_order_relaxed);
	g_panel    = D2RL::Panels::InvalidHandle;
	g_resource = D2RL::Resources::InvalidHandle;
	g_listener = D2RL::SharedEvents::InvalidHandle;
	g_panels    = nullptr;
	g_resources = nullptr;
	g_events    = nullptr;
	g_threads   = nullptr;
	g_widgets   = nullptr;
}

}   // namespace NativePanel

// ─────── 叠加层面板的开关注口（实现在文件后半的 OverlayPanel 里）───────
//  InputActions / 控制台命令都在这之前，所以先把这两个函数声明出来。
//  · 宿主（MapSense）在场时，面板画在它那份 ImGui 上 —— 玩家看到的
//    就是"跟地图插件一样"的窗口；
//  · 宿主不在场时退回到游戏原生面板。
auto ToggleOverlayPanel() noexcept -> bool;   // false = 叠加层面板这次用不了
auto OverlayPanelReady() noexcept -> bool;
namespace OverlayPanel {
auto IsOpen() noexcept -> bool;
auto StateText() noexcept -> const char*;
// ★ v0.18.5：退回原生面板时把"为什么"一次讲清楚（日志只打一行）。
auto ExplainFallback() noexcept -> void;
}

// ────────────── 游戏「控制」菜单里的按键（v0.9.0 新增）──────────────
//  这是玩家唯一能自己改键的入口。把"打开面板"注册成一条游戏原生动作后：
//    · 它会出现在 设置 → 控制 的按键列表里（分类名 = 本插件）；
//    · 玩家的绑定存在 d2rloader/config/input-bindings.toml 的
//      [bindings."loot-map/toggle-panel"] 下，插件暂时不在也不会丢；
//    · 与别的动作的按键冲突由游戏自己处理。
//  注意：这个回调在游戏的输入处理线程上跑，所以开关面板必须排队到 UI 线程。
namespace InputActions {

constexpr const char* kTogglePanelAction = "toggle-panel";

// 注册成功后拿到的句柄，卸载时要用它把动作注销掉。
// 不注销的话，插件一旦被卸载，这条动作的回调就指向已释放的内存 ——
// 之后按一次 F7 就会跳进野地址。宁可卸载时多一步。
D2RL::Input::ActionHandle g_action = D2RL::Input::InvalidHandle;

auto __cdecl OnTogglePanel(const D2RL::PluginContext* /*context*/,
                           const D2RL::Input::ActionEvent* event,
                           void* /*userData*/) noexcept -> D2RL::Input::ActionResult {
	if (event == nullptr ||
	    !D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) ||
	    event->kind != D2RL::Input::ActionEventKind::Pressed) {
		return D2RL::Input::ActionResult::Ignored;   // 抬手事件不处理
	}
	// ★ 先试叠加层面板（跟地图插件同一种 ImGui 窗口，好看得多）；
	//   宿主不在场时才退回游戏原生面板。
	if (!ToggleOverlayPanel()) {
		// ★ v0.18.5：把"为什么退回"写进日志 —— 别人用这个插件时最常撞到的
		//   就是"没装地图插件（MapSense）"，以前这里只有一行 native panel 提示，
		//   看不出根因。
		OverlayPanel::ExplainFallback();
		if (!NativePanel::ToggleFromCommand()) {
			LogWarn("loot-map: the panel hotkey was pressed but no panel backend is available.");
		}
	}
	return D2RL::Input::ActionResult::Handled;
}

auto Install(const D2RL::PluginContext* ctx) noexcept -> bool {
	if (ctx == nullptr) {
		return false;
	}

	const D2RL::InputService* input = nullptr;
	if (ctx->QueryService(&input) != D2RL::ServiceQueryResult::Success || input == nullptr ||
	    !D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		LogWarn("loot-map: InputService unavailable; the panel hotkey will not appear in Controls.");
		return false;
	}

	const D2RL::Input::ActionRegistration registration {
		.structSize       = D2RL::Input::ActionRegistrationSize,
		.flags            = 0,
		.logicalId        = kTogglePanelAction,
		.displayName      = "打开 / 关闭掉落物地图面板",
		.category         = "Loot Map 掉落物地图",
		.defaultPrimary   = { D2RL::Input::Key::F7, D2RL::Input::Modifier::None },
		.defaultSecondary = { D2RL::Input::Key::None, D2RL::Input::Modifier::None },
		.callback         = OnTogglePanel,
		.userData         = nullptr,
	};
	D2RL::Input::ActionHandle action = D2RL::Input::InvalidHandle;
	const D2RL::Input::Result result  = input->registerAction(ctx, &registration, &action);

	char line[240] {};
	if (result != D2RL::Input::Result::Success) {
		std::snprintf(line, sizeof(line),
			"loot-map: registerAction('toggle-panel') failed (%u); no Controls entry this session.",
			static_cast<unsigned>(result));
		LogWarn(line);
		return false;
	}

	std::snprintf(line, sizeof(line),
		"loot-map: Controls action 'loot-map/%s' registered, default F7, handle=%llu.",
		kTogglePanelAction, static_cast<unsigned long long>(action));
	LogInfo(line);
	g_action = action;
	return true;
}

// 卸载时把动作注销掉（与 Install 对称）。失败也无所谓 ——
// 加载器在游戏退出时会自己回收，这里只是防\"运行中卸载插件\"那一种情况。
auto Uninstall() noexcept -> void {
	if (g_action == D2RL::Input::InvalidHandle || g_context == nullptr) {
		return;
	}

	const D2RL::InputService* input = nullptr;
	if (g_context->QueryService(&input) == D2RL::ServiceQueryResult::Success && input != nullptr &&
	    D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize) &&
	    input->unregisterAction != nullptr) {
		const D2RL::Input::Result r = input->unregisterAction(g_context, g_action);
		if (r != D2RL::Input::Result::Success) {
			char line[200] {};
			std::snprintf(line, sizeof(line),
				"loot-map: unregisterAction('toggle-panel') -> %u (ignored).",
				static_cast<unsigned>(r));
			LogWarn(line);
		}
	}
	g_action = D2RL::Input::InvalidHandle;
}

}   // namespace InputActions

// ─────────────────── ImGui 面板 ───────────────────
//  在游戏的渲染线程上被调用（RenderHost 的 Present 钩子里）。

auto SaveSettingsNow() noexcept -> void {
	(void)SaveSettings(g_context);
}

auto DrawPanelUi() noexcept -> void {
	ImGui::SetNextWindowSize(ImVec2(470, 560), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Loot Map 物品地图标记", nullptr)) {
		ImGui::End();
		return;
	}

	// 工作模式
	int modeIdx = g_settings.observeOnly ? 0 : 1;
	if (ImGui::Combo("工作模式", &modeIdx, "观察模式（只记录数据，不上色）\0上色模式（按品质画星标）\0")) {
		g_settings.observeOnly = (modeIdx == 0);
		SaveSettingsNow();
	}
	if (g_settings.observeOnly) {
		ImGui::TextDisabled("观察模式：只把物品数据写进日志，不改变地图显示。");
	} else if (g_settings.qualityOffset <= 0) {
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
			"上色模式但品质偏移还没确定（quality_offset=0），暂不会上色。");
	} else {
		ImGui::TextDisabled("上色模式：物品按品质画在地图上。");
	}

	ImGui::SeparatorText("地图显示 —— 勾选 = 在地图上显示该品质");

	for (int i = 0; i < kSlotCount; ++i) {
		ImGui::PushID(i);
		bool shown = g_settings.show[i];
		if (ImGui::Checkbox(kRows[i].label, &shown)) {
			g_settings.show[i] = shown;
			SaveSettingsNow();
		}
		ImGui::SameLine(330.0f);
		int color = g_settings.color[i];
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::SliderInt("星标色号", &color, kColorMin, kColorMax, "色号 %d")) {
			g_settings.color[i] = ClampColor(color);
			SaveSettingsNow();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("游戏地图光点的色号。1 和 4 会被游戏跳过，\n插件会自动换成能显示的相邻色号。");
		}
		ImGui::PopID();
	}

	ImGui::Spacing();
	if (ImGui::Button("全部显示", ImVec2(120, 0))) {
		for (int i = 0; i < kSlotCount; ++i) {
			g_settings.show[i] = true;
		}
		SaveSettingsNow();
	}
	ImGui::SameLine();
	if (ImGui::Button("全部隐藏", ImVec2(120, 0))) {
		for (int i = 0; i < kSlotCount; ++i) {
			g_settings.show[i] = false;
		}
		SaveSettingsNow();
	}

	ImGui::SeparatorText("状态");
	ImGui::Text("渲染宿主: %s", RenderHost::RendererStatusText());
	ImGui::Text("物品: 见过 %u 个不同单位（共命中 %u 次）",
		unsigned(g_itemUnits), unsigned(g_itemCalls));
	ImGui::Text("品质偏移: %d（0 = 物品数据第 0 字节，已确认）", g_settings.qualityOffset);

	ImGui::Spacing();
	ImGui::TextDisabled("按 F7 开关本面板；设置改动立即保存。");
	ImGui::TextDisabled("观察模式的数据在 logs/loot-map.log。");

	ImGui::End();
}

// ─────────────────── 控制台命令 ───────────────────

auto TogglePanelCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	// v0.11.0：面板优先画在宿主（MapSense）那份 ImGui 上 —— 那是"跟地图插件
	// 一样"的窗口；只有在宿主接口拿不到时才退回游戏原生面板。
	if (ToggleOverlayPanel()) {
		command->plugin->WriteConsoleMessage(OverlayPanel::IsOpen()
			? "loot-map: panel opened (overlay layer, same as the map plugin)."
			: "loot-map: panel closed.");
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (NativePanel::ToggleFromCommand()) {
		OverlayPanel::ExplainFallback();   // ★ v0.18.5：把根因写进日志（只打一次）
		command->plugin->WriteConsoleMessage("loot-map: native panel toggled (details in logs/loot-map.log).");
		return D2RL::ConsoleCommandResult::Handled;
	}
	command->plugin->WriteConsoleMessage("loot-map: no panel backend is available in this session.");
	return D2RL::ConsoleCommandResult::Failed;
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	char line[320] {};
	std::snprintf(line, sizeof(line),
		"loot-map: mode=%s hook=%s quality_offset=%d item_units=%u item_calls=%u renderer=%s",
		g_settings.observeOnly ? "observe" : "color",
		g_originalGetUnitColorIndex != nullptr ? "installed" : "MISSING",
		g_settings.qualityOffset,
		unsigned(g_itemUnits),
		unsigned(g_itemCalls),
		RenderHost::RendererStatusText());
	command->plugin->WriteConsoleMessage(line);

	// 面板后端的状态（排查"按了键没反应"时最先要看的一行）
	std::snprintf(line, sizeof(line),
		"loot-map: panel=overlay-layer [%s] open=%s  (fallback: native panel)",
		OverlayPanel::StateText(), OverlayPanel::IsOpen() ? "yes" : "no");
	command->plugin->WriteConsoleMessage(line);

	for (int i = 0; i < kSlotCount; ++i) {
		char rgbText[24] {};
		if (g_settings.rgbOn[i]) {
			std::snprintf(rgbText, sizeof(rgbText), "rgb(%d,%d,%d)",
				Rgb255(g_settings.rgb[i][0]), Rgb255(g_settings.rgb[i][1]), Rgb255(g_settings.rgb[i][2]));
		} else {
			std::snprintf(rgbText, sizeof(rgbText), "index");
		}
		// 括号里是"离引擎那 13 色表里哪一个最近"，和游戏内面板上看到的一致
		std::snprintf(line, sizeof(line), "  %-9s show=%-5s color=%d shape=%d %s [%s]",
			kRows[i].key, g_settings.show[i] ? "true" : "false",
			g_settings.color[i], g_settings.shape[i], rgbText,
			NativePanel::kPalette[NativePanel::PaletteIndexOf(i)].name);
		command->plugin->WriteConsoleMessage(line);
	}
	std::snprintf(line, sizeof(line),
		"loot-map: true-RGB draw patch=%s, markers recoloured=%u, colour-query overwrites=%u",
		g_blobIconHookInstalled.load(std::memory_order_relaxed) ? "installed" : "not installed",
		static_cast<unsigned>(g_rgbDraws.load(std::memory_order_relaxed)),
		static_cast<unsigned>(g_pendingOverwrites.load(std::memory_order_relaxed)));
	command->plugin->WriteConsoleMessage(line);
	if (g_pendingOverwrites.load(std::memory_order_relaxed) != 0) {
		command->plugin->WriteConsoleMessage(
			"loot-map: NOTE - colour-query overwrites > 0 means the game asks for a whole batch of "
			"unit colours before drawing any of them, so one marker may borrow another's colour.");
	}
	return D2RL::ConsoleCommandResult::Handled;
}

auto ReloadCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr || g_context == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	Settings fresh;
	if (!LoadSettings(g_context, fresh)) {
		command->plugin->WriteConsoleError("loot-map: could not read the config file.");
		return D2RL::ConsoleCommandResult::Failed;
	}
	g_settings = fresh;
	g_blockGameMouse.store(g_settings.blockGameMouse, std::memory_order_relaxed);
	command->plugin->WriteConsoleMessage("loot-map: config reloaded.");
	return D2RL::ConsoleCommandResult::Handled;
}

auto SetCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr || command->args == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	char args[128] {};
	std::strncpy(args, command->args, sizeof(args) - 1);

	char* key = TrimInPlace(args);
	char* sep = std::strchr(key, ' ');
	if (sep == nullptr) {
		command->plugin->WriteConsoleError("loot-map: usage: lootmap-set <key> on|off");
		return D2RL::ConsoleCommandResult::InvalidArguments;
	}
	*sep = '\0';
	char* value = TrimInPlace(sep + 1);

	if (std::strcmp(key, "mode") == 0) {
		if (std::strcmp(value, "observe") == 0 || std::strcmp(value, "color") == 0) {
			g_settings.observeOnly = (std::strcmp(value, "observe") == 0);
			(void)SaveSettings(g_context);
			command->plugin->WriteConsoleMessage("loot-map: mode saved.");
			return D2RL::ConsoleCommandResult::Handled;
		}
		command->plugin->WriteConsoleError("loot-map: mode must be observe or color.");
		return D2RL::ConsoleCommandResult::InvalidArguments;
	}

	// ★ v0.12.0：lootmap-set stars on|off
	if (std::strcmp(key, "stars") == 0) {
		if (std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0) {
			g_settings.stars = true;
		} else if (std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0) {
			g_settings.stars = false;
		} else {
			command->plugin->WriteConsoleError("loot-map: value must be on or off.");
			return D2RL::ConsoleCommandResult::InvalidArguments;
		}
		(void)SaveSettings(g_context);
		command->plugin->WriteConsoleMessage("loot-map: stars saved.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (std::strcmp(key, "__all__") == 0 || std::strcmp(key, "all") == 0) {
		const bool on = (std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0);
		for (int i = 0; i < kSlotCount; ++i) {
			g_settings.show[i] = on;
		}
	} else {
		const int slot = FindRow(key);
		if (slot < 0) {
			command->plugin->WriteConsoleError("loot-map: unknown key. Run lootmap-status to list them.");
			return D2RL::ConsoleCommandResult::InvalidArguments;
		}
		if (std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0) {
			g_settings.show[slot] = true;
		} else if (std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0) {
			g_settings.show[slot] = false;
		} else {
			command->plugin->WriteConsoleError("loot-map: value must be on or off.");
			return D2RL::ConsoleCommandResult::InvalidArguments;
		}
	}

	(void)SaveSettings(g_context);
	command->plugin->WriteConsoleMessage("loot-map: setting saved.");
	return D2RL::ConsoleCommandResult::Handled;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SDK 服务 / 数据探测（v0.4.0）
//
//  目的：确认现役 D2RLoader 到底开放了官方 SDK 的哪些服务，以及
//        「一件地面物品」的全部字段到底能不能读到。
//  只读：不改游戏行为、不动数据表、不碰别的插件。
// ═══════════════════════════════════════════════════════════════════════════

struct ProbeServiceEntry {
	D2RL::ServiceId id;
	const char*     name;
	std::uint32_t   abiVersion;
};

constexpr ProbeServiceEntry kProbeServices[] {
	{ D2RL::ServiceId::Lifecycle,       "Lifecycle",       1 },
	{ D2RL::ServiceId::Resource,        "Resource",        1 },
	{ D2RL::ServiceId::CustomTable,     "CustomTable",     1 },
	{ D2RL::ServiceId::Panel,           "Panel",           1 },
	{ D2RL::ServiceId::Inventory,       "Inventory",       1 },
	{ D2RL::ServiceId::Network,         "Network",         1 },
	{ D2RL::ServiceId::Input,           "Input",           1 },
	{ D2RL::ServiceId::DataTable,       "DataTable",       1 },
	{ D2RL::ServiceId::SharedEvent,     "SharedEvent",     1 },
	{ D2RL::ServiceId::Diagnostics,     "Diagnostics",     1 },
	{ D2RL::ServiceId::GameRule,        "GameRule",        1 },
	{ D2RL::ServiceId::Widget,          "Widget",          1 },
	{ D2RL::ServiceId::Thread,          "Thread",          1 },
	{ D2RL::ServiceId::Localization,    "Localization",    2 },
	{ D2RL::ServiceId::Item,            "Item",            1 },
	{ D2RL::ServiceId::Http,            "Http",            1 },
	{ D2RL::ServiceId::ItemInteraction, "ItemInteraction", 1 },
	{ D2RL::ServiceId::Encounter,       "Encounter",       1 },
};

auto QueryResultText(D2RL::ServiceQueryResult result) noexcept -> const char* {
	switch (result) {
	case D2RL::ServiceQueryResult::Success:            return "OK";
	case D2RL::ServiceQueryResult::InvalidArgument:    return "InvalidArgument";
	case D2RL::ServiceQueryResult::UnknownService:     return "UnknownService";
	case D2RL::ServiceQueryResult::UnsupportedVersion: return "UnsupportedVersion";
	case D2RL::ServiceQueryResult::Unavailable:        return "Unavailable";
	case D2RL::ServiceQueryResult::OwnerInactive:      return "OwnerInactive";
	default:                                           return "?";
	}
}

auto ProbeServiceScan(const D2RL::PluginContext* ctx, const char* stage) noexcept -> void {
	if (ctx == nullptr) {
		return;
	}
	const char* tag = (stage != nullptr) ? stage : "?";
	char line[256] {};

	std::snprintf(line, sizeof(line), "PROBE[%s] SDK service scan (%u services):",
		tag, static_cast<unsigned>(sizeof(kProbeServices) / sizeof(kProbeServices[0])));
	LogInfo(line);

	std::uint32_t available = 0;
	for (const ProbeServiceEntry& entry : kProbeServices) {
		const void* service = nullptr;
		const D2RL::ServiceQueryResult result = ctx->QueryService(entry.id, entry.abiVersion, &service);
		std::uint32_t serviceSize    = 0;
		std::uint32_t serviceVersion = 0;
		if (result == D2RL::ServiceQueryResult::Success && service != nullptr) {
			serviceSize    = *static_cast<const std::uint32_t*>(service);
			serviceVersion = *(static_cast<const std::uint32_t*>(service) + 1);
			++available;
		}
		std::snprintf(line, sizeof(line), "PROBE[%s]   %-16s want=v%u -> %-18s size=%u ver=%u",
			tag, entry.name, static_cast<unsigned>(entry.abiVersion),
			QueryResultText(result), static_cast<unsigned>(serviceSize), static_cast<unsigned>(serviceVersion));
		LogInfo(line);
	}
	std::snprintf(line, sizeof(line), "PROBE[%s] SDK service scan done: %u/%u available.",
		tag, static_cast<unsigned>(available),
		static_cast<unsigned>(sizeof(kProbeServices) / sizeof(kProbeServices[0])));
	LogInfo(line);
}

// ── 地面物品枚举 ────────────────────────────────────────────────

constexpr int kProbeMaxItems = 40;

struct ProbeItemSink {
	int total  = 0;
	int dumped = 0;
};

std::atomic<bool> g_probeFullDump   { false };
std::atomic<bool> g_probeDumpedOnce { false };

auto FourCodeText(std::uint32_t code, char* out, std::size_t outSize) noexcept -> void {
	if (out == nullptr || outSize < 6) {
		return;
	}
	char raw[5] {};
	for (int index = 0; index < 4; ++index) {
		const char value = static_cast<char>((code >> (8 * index)) & 0xFF);
		raw[index] = (value >= 32 && value < 127) ? value : '.';
	}
	int length = 4;
	while (length > 0 && raw[length - 1] == ' ') {
		--length;
	}
	raw[length] = '\0';
	std::snprintf(out, outSize, "%s", raw);
}

auto ProbeItemCallback(const D2RL::PluginContext* /*ctx*/, const D2RL::Items::ItemInfo* item, void* userData) noexcept
	-> D2RL::Inventory::IterationAction {
	auto* sink = static_cast<ProbeItemSink*>(userData);
	if (item == nullptr) {
		return D2RL::Inventory::IterationAction::Stop;
	}
	if (sink != nullptr) {
		sink->total += 1;
	}

	const bool verbose = g_probeFullDump.load(std::memory_order_relaxed)
	                  || !g_probeDumpedOnce.load(std::memory_order_relaxed);
	if (sink == nullptr || !verbose || sink->dumped >= kProbeMaxItems) {
		return D2RL::Inventory::IterationAction::Continue;
	}
	sink->dumped += 1;

	char code[8] {};
	FourCodeText(item->code, code, sizeof(code));

	char line[520] {};
	std::snprintf(line, sizeof(line),
		"PROBE item #%d cont=%u code=%s(0x%08X) class=%u q=%u rec=%d lvl=%u bpos=%d xy=(%d,%d) "
		"sock=%u/%u pre=[%u,%u,%u] suf=[%u,%u,%u] state=0x%X qty=%d dur=%d/%d rtid=%u handle=%llu",
		sink->dumped, static_cast<unsigned>(item->container),
		code, static_cast<unsigned>(item->code), static_cast<unsigned>(item->classId),
		static_cast<unsigned>(item->quality), item->qualityRecordId,
		static_cast<unsigned>(item->itemLevel), item->bodyLocation, item->x, item->y,
		static_cast<unsigned>(item->socketCount), static_cast<unsigned>(item->socketedItemCount),
		static_cast<unsigned>(item->prefixIds[0]), static_cast<unsigned>(item->prefixIds[1]),
		static_cast<unsigned>(item->prefixIds[2]),
		static_cast<unsigned>(item->suffixIds[0]), static_cast<unsigned>(item->suffixIds[1]),
		static_cast<unsigned>(item->suffixIds[2]),
		static_cast<unsigned>(item->stateFlags), item->quantity, item->durability,
		item->maximumDurability, static_cast<unsigned>(item->runtimeId),
		static_cast<unsigned long long>(item->handle));
	LogInfo(line);
	return D2RL::Inventory::IterationAction::Continue;
}

auto ProbeGroundItems(const D2RL::PluginContext* ctx) noexcept -> bool {
	if (ctx == nullptr) {
		return false;
	}

	const D2RL::InventoryService* inventory = nullptr;
	if (ctx->QueryService(&inventory) != D2RL::ServiceQueryResult::Success || inventory == nullptr) {
		LogWarn("PROBE item: the Inventory service is not available.");
		return false;
	}
	if (!D2RL::HasInventoryServiceField(inventory, D2RL::InventoryServiceRequiredSize)) {
		LogWarn("PROBE item: the Inventory service is smaller than this SDK expects.");
		return false;
	}
	if (inventory->getLocalPlayer == nullptr || inventory->forEachInventoryItem == nullptr) {
		LogWarn("PROBE item: getLocalPlayer / forEachInventoryItem are missing.");
		return false;
	}

	D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
	const D2RL::Inventory::Result playerResult = inventory->getLocalPlayer(ctx, &player);
	if (playerResult != D2RL::Inventory::Result::Success || player == D2RL::InvalidPlayerHandle) {
		char line[200] {};
		std::snprintf(line, sizeof(line), "PROBE item: getLocalPlayer -> %u (no local player yet).",
			static_cast<unsigned>(playerResult));
		LogInfo(line);
		return false;
	}

	D2RL::Inventory::ItemFilter filter {};
	filter.structSize    = D2RL::Inventory::ItemFilterSize;
	filter.flags         = 0;
	filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Ground);
	filter.reserved      = 0;

	ProbeItemSink sink {};
	const D2RL::Inventory::Result iterResult =
		inventory->forEachInventoryItem(ctx, player, &filter, &ProbeItemCallback, &sink);

	char line[240] {};
	std::snprintf(line, sizeof(line), "PROBE item: ground enumeration -> %u, %d item(s) on the ground, %d logged.",
		static_cast<unsigned>(iterResult), sink.total, sink.dumped);
	LogInfo(line);

	// 对照组：不带容器过滤再走一遍。用来判断"接口本身能不能返回物品"，
	// 还是"只是不包含地面容器"。
	ProbeItemSink all {};
	filter.containerMask = D2RL::Items::AllItemContainers;
	const D2RL::Inventory::Result allResult =
		inventory->forEachInventoryItem(ctx, player, &filter, &ProbeItemCallback, &all);
	char line2[240] {};
	std::snprintf(line2, sizeof(line2), "PROBE item: all-container enumeration -> %u, %d item(s), %d logged.",
		static_cast<unsigned>(allResult), all.total, all.dumped);
	LogInfo(line2);

	if (all.total > 0) {
		g_probeDumpedOnce.store(true, std::memory_order_relaxed);
		g_probeFullDump.store(false, std::memory_order_relaxed);
	}
	return sink.total > 0;
}

// ── 数据表探测（必须在游戏线程上调用）────────────────────────────

struct ProbeTableEntry {
	D2RL::DataTables::TableId id;
	const char*               name;
};

constexpr ProbeTableEntry kProbeTables[] {
	{ D2RL::DataTables::TableId::Items,            "Items" },
	{ D2RL::DataTables::TableId::ItemTypes,        "ItemTypes" },
	{ D2RL::DataTables::TableId::MagicAffixes,     "MagicAffixes" },
	{ D2RL::DataTables::TableId::Runes,            "Runes" },
	{ D2RL::DataTables::TableId::BodyLocs,         "BodyLocs" },
	{ D2RL::DataTables::TableId::SetItems,         "SetItems" },
	{ D2RL::DataTables::TableId::UniqueItems,      "UniqueItems" },
	{ D2RL::DataTables::TableId::Gems,             "Gems" },
	{ D2RL::DataTables::TableId::ItemUiCategories, "ItemUiCategories" },
};

constexpr const char* BankName(D2RL::DataTables::Bank bank) noexcept {
	switch (bank) {
	case D2RL::DataTables::Bank::Classic: return "Classic";
	case D2RL::DataTables::Bank::Lod:     return "Lod";
	case D2RL::DataTables::Bank::Rotw:    return "Rotw";
	default:                              return "?";
	}
}

auto ProbeRowsHex(const D2RL::DataTableService* tables, const D2RL::PluginContext* ctx,
                  D2RL::DataTables::Bank bank, D2RL::DataTables::TableId tableId,
                  const char* name, std::uint32_t maxBytes) noexcept -> void {
	D2RL::DataTables::TableView view {};
	view.structSize = D2RL::DataTables::TableViewSize;
	if (tables->getTable(ctx, bank, tableId, &view) != D2RL::DataTables::Result::Success) {
		return;
	}
	if (view.rows == nullptr || view.rowSize == 0 || view.rowCount == 0) {
		return;
	}

	const std::uint32_t bytes = (maxBytes < view.rowSize) ? maxBytes : view.rowSize;
	const auto* rows = static_cast<const std::uint8_t*>(view.rows);
	char line[420] {};

	for (std::uint32_t row = 0; row < 4 && row < view.rowCount; ++row) {
		char hex[300] {};
		HexDump(rows + static_cast<std::size_t>(row) * view.rowSize, bytes, hex, sizeof(hex));
		std::snprintf(line, sizeof(line), "PROBE tables: %s[%s] row %u/%u (%u of %u bytes): %s",
			name, BankName(bank), static_cast<unsigned>(row), static_cast<unsigned>(view.rowCount),
			static_cast<unsigned>(bytes), static_cast<unsigned>(view.rowSize), hex);
		LogInfo(line);
	}
}

auto ProbeDataTables(const D2RL::PluginContext* ctx) noexcept -> void {
	if (ctx == nullptr) {
		return;
	}

	const D2RL::DataTableService* tables = nullptr;
	if (ctx->QueryService(&tables) != D2RL::ServiceQueryResult::Success || tables == nullptr) {
		LogWarn("PROBE tables: the DataTable service is not available.");
		return;
	}
	if (!D2RL::HasDataTableServiceField(tables, D2RL::DataTableServiceRequiredSize)
	    || tables->getTable == nullptr) {
		LogWarn("PROBE tables: the DataTable service is smaller than this SDK expects.");
		return;
	}

	char line[260] {};
	const D2RL::DataTables::Bank banks[] {
		D2RL::DataTables::Bank::Classic,
		D2RL::DataTables::Bank::Lod,
		D2RL::DataTables::Bank::Rotw,
	};

	for (const ProbeTableEntry& entry : kProbeTables) {
		for (const D2RL::DataTables::Bank bank : banks) {
			D2RL::DataTables::TableView view {};
			view.structSize = D2RL::DataTables::TableViewSize;
			const D2RL::DataTables::Result result = tables->getTable(ctx, bank, entry.id, &view);
			std::snprintf(line, sizeof(line), "PROBE tables: %-16s [%-7s] -> %-12s rows=%u rowSize=%u rev=%llu",
				entry.name, BankName(bank),
				(result == D2RL::DataTables::Result::Success) ? "OK" : "unavailable",
				static_cast<unsigned>(view.rowCount), static_cast<unsigned>(view.rowSize),
				static_cast<unsigned long long>(view.revision));
			LogInfo(line);
		}
	}

	LogInfo("PROBE tables: raw row image for the two tables needed to name items and classify them:");
	ProbeRowsHex(tables, ctx, D2RL::DataTables::Bank::Rotw, D2RL::DataTables::TableId::Items,    "Items",    96);
	ProbeRowsHex(tables, ctx, D2RL::DataTables::Bank::Rotw, D2RL::DataTables::TableId::ItemTypes, "ItemTypes", 96);
}

// ── 触发器：控制台命令 + 游戏内自动跑一次 ─────────────────────────

constexpr ULONGLONG kProbeWatchMs = 30000;

std::atomic<ULONGLONG> g_probeWatchUntil { 0 };
std::atomic<ULONGLONG> g_probeLastRun    { 0 };

auto ProbeUiTick(const D2RL::PluginContext* ctx, void* /*userData*/) noexcept -> void {
	if (ctx == nullptr) {
		return;
	}

	const ULONGLONG now = GetTickCount64();
	if (now > g_probeWatchUntil.load(std::memory_order_relaxed)) {
		return;   // 观察窗口结束，不再排队
	}

	if (now - g_probeLastRun.load(std::memory_order_relaxed) >= 250) {
		g_probeLastRun.store(now, std::memory_order_relaxed);
		(void)ProbeGroundItems(ctx);
	}

	const D2RL::ThreadService* threads = nullptr;
	if (ctx->QueryService(&threads) == D2RL::ServiceQueryResult::Success && threads != nullptr
	    && threads->runOnUiThread != nullptr) {
		(void)threads->runOnUiThread(ctx, &ProbeUiTick, nullptr);
	}
}

auto ProbeGameTick(const D2RL::PluginContext* ctx, void* /*userData*/) noexcept -> void {
	ProbeDataTables(ctx);
}

auto StartProbe(const D2RL::PluginContext* ctx, bool verbose) noexcept -> void {
	if (ctx == nullptr) {
		return;
	}
	if (verbose) {
		g_probeFullDump.store(true, std::memory_order_relaxed);
		g_probeDumpedOnce.store(false, std::memory_order_relaxed);
	}
	g_probeLastRun.store(0, std::memory_order_relaxed);
	g_probeWatchUntil.store(GetTickCount64() + kProbeWatchMs, std::memory_order_relaxed);

	const D2RL::ThreadService* threads = nullptr;
	const bool haveThreads = (ctx->QueryService(&threads) == D2RL::ServiceQueryResult::Success
	                          && threads != nullptr
	                          && D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize));
	const bool queuedUi   = haveThreads && threads->runOnUiThread != nullptr
	                     && threads->runOnUiThread(ctx, &ProbeUiTick, nullptr) == D2RL::Threads::Result::Success;
	const bool queuedGame = haveThreads && threads->runOnGameThread != nullptr
	                     && threads->runOnGameThread(ctx, &ProbeGameTick, nullptr) == D2RL::Threads::Result::Success;

	if (!queuedUi) {
		ProbeUiTick(ctx, nullptr);
	}
	if (!queuedGame) {
		ProbeGameTick(ctx, nullptr);
	}
}

auto ProbeCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	command->plugin->WriteConsoleMessage("loot-map: probing SDK services, ground items and data tables ...");
	ProbeServiceScan(command->plugin, "cmd");
	StartProbe(command->plugin, true);
	command->plugin->WriteConsoleMessage("loot-map: report is being written to loot-map.log.");
	return D2RL::ConsoleCommandResult::Handled;
}

auto ProbeDataTablesLoadedCallback(const D2RL::PluginContext* ctx,
                                   const D2RL::Lifecycle::DataTablesLoadedEvent* event,
                                   void* /*userData*/) noexcept -> void {
	char line[200] {};
	std::snprintf(line, sizeof(line), "PROBE tables: data tables loaded (revision=%llu).",
		static_cast<unsigned long long>((event != nullptr) ? event->revision : 0));
	LogInfo(line);
	ProbeDataTables(ctx);
}

auto ProbeLocalPlayerReadyCallback(const D2RL::PluginContext* ctx,
                                   const D2RL::Lifecycle::GameplayEvent* /*event*/,
                                   void* /*userData*/) noexcept -> void {
	LogInfo("PROBE item: local player is ready.");
	ProbeServiceScan(ctx, "ingame");

	// 注意：这里以前会自动开一个 30 秒的"地面物品监视"，每 250 ms 枚举一次。
	// 实测这个 loader（1.2.1-beta）的 forEachInventoryItem 用 Ground 掩码恒返回
	// 0 条（不带掩码能列出 1086 条），于是每轮都白写两行日志，30 秒能刷出几万行、
	// 把日志顶到几十 MB。改成不自动跑，需要时用 lootmap-probe 手动来一次。
}

D2RL::Lifecycle::ListenerHandle g_probeTablesListener = D2RL::Lifecycle::InvalidHandle;
D2RL::Lifecycle::ListenerHandle g_probePlayerListener = D2RL::Lifecycle::InvalidHandle;

auto InstallProbeListeners(const D2RL::PluginContext* ctx) noexcept -> void {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (ctx->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success || lifecycle == nullptr) {
		LogWarn("PROBE: the Lifecycle service is not available; only the manual command will work.");
		return;
	}
	if (!D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		LogWarn("PROBE: the Lifecycle service is smaller than this SDK expects.");
		return;
	}

	if (lifecycle->registerDataTablesLoadedListener != nullptr) {
		D2RL::Lifecycle::DataTablesLoadedListener listener {};
		listener.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize;
		listener.callback   = &ProbeDataTablesLoadedCallback;
		if (lifecycle->registerDataTablesLoadedListener(ctx, &listener, &g_probeTablesListener)
		    == D2RL::Lifecycle::Result::Success) {
			LogInfo("PROBE: data-table listener registered.");
		}
	}

	if (lifecycle->registerGameplayEventListener != nullptr) {
		D2RL::Lifecycle::GameplayEventListener listener {};
		listener.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
		listener.kind       = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady;
		listener.callback   = &ProbeLocalPlayerReadyCallback;
		if (lifecycle->registerGameplayEventListener(ctx, &listener, &g_probePlayerListener)
		    == D2RL::Lifecycle::Result::Success) {
			LogInfo("PROBE: local-player-ready listener registered.");
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  Overlay host 探测（v0.6.0）
//
//  背景：用户要的是"跟地图插件（MapSense）一样"的效果——任意颜色 + 任意图标。
//  游戏自带的地图光点只有 6 个色号、形状由游戏定死，做不到。MapSense 能
//  做到，是因为它自己在游戏画面上叠了一层 Dear ImGui。我们之前也试过自己
//  叠一层，两次把游戏搞崩——原因是它明确拒绝第二个 DirectX 12 拥有者。
//
//  但静态逆向 d2rl-ruffneckk-mapsense.dll 发现：它**主动开放了一个接入接口**，
//  让别的插件把内容画进它那一层里（这样只有一个渲染主人，不会冲突）。
//
//    · 导出函数 RuffnecKkMapSenseGetOverlayHostApi(abi, structSize)
//        必须 abi == 2 且 structSize >= 0x20，否则返回 nullptr。
//        返回一个 32 字节的宿主接口块：
//          +0x00 u32 结构大小 = 0x20
//          +0x04 u32 版本     = 2
//          +0x08 u64 口令 magic = 0xF401021D19150002
//          +0x10 fn  registerClient        （下面第 2 步用）
//          +0x18 fn  unregisterClient
//
//    · registerClient 收一个"客户端描述块"（结构大小 >= 0x48）：
//          +0x00 u32 结构大小
//          +0x04 u32 版本 = 2
//          +0x08 名字指针（非空字符串）
//          +0x10 u64 同一个 magic（相当于口令，写错就不让进）
//          +0x18 ~ +0x38 五个回调函数指针（至少一个非空）
//          +0x40 用户数据指针（宿主会原样回传给回调）
//       注意：宿主会先检查"当前是否允许新客户端加入"（GPU 是否空闲），
//       太晚注册会被拒并打印 "a late overlay client could not join"。
//       所以本插件开机即注册，并且反复重试到成功为止。
//
//    · 宿主回调我们的函数时，给的上下文（构造在栈上，只在回调期间有效）：
//          +0x00 u32 结构大小 = 0x20
//          +0x04 u32 版本     = 2
//          +0x08 void* 宿主自己的 Dear ImGui 1.91.5 上下文
//          +0x10 void* 宿主单例对象
//          +0x18 f32 视口宽   +0x1C f32 视口高
//
//  本版本只做"敲门"：注册一个什么都不画的客户端，把宿主交回的上下文
//  与回调触发情况写进日志。不绘制、不改任何游戏文件、不动别的插件。
//  拿到的信息足以决定第二步怎么把物品图标画进那一层。
// ═══════════════════════════════════════════════════════════════════════════

constexpr std::uint64_t kOverlayMagic = 0xF401021D19150002ull;

using OverlayHostGetFn    = const void*(__fastcall*)(std::uint32_t abi, std::uint32_t structSize) noexcept;
using OverlayRegisterFn   = bool(__fastcall*)(const void* clientDesc) noexcept;
using OverlayUnregisterFn = bool(__fastcall*)(const char* name) noexcept;
using OverlayCallbackFn   = void(__fastcall*)(const void* ctx, void* userData) noexcept;

struct OverlayHostApi {
	std::uint32_t       structSize;
	std::uint32_t       version;
	std::uint64_t       magic;
	OverlayRegisterFn   registerClient;
	OverlayUnregisterFn unregisterClient;
};

struct OverlayClientDesc {
	std::uint32_t     structSize;   // 0x48
	std::uint32_t     version;      // 2
	const char*       name;
	std::uint64_t     magic;
	OverlayCallbackFn callback[5];  // +0x18 / 0x20 / 0x28 / 0x30 / 0x38
	void*             userData;     // +0x40
};

std::atomic<std::uint32_t> g_ovlHits[5] {};
std::atomic<std::uint32_t> g_ovlState { 0 };        // 0 未注册 / 1 已注册 / 2 确定不可用
std::atomic<std::uint32_t> g_ovlAttempts { 0 };
const OverlayHostApi*      g_ovlApi = nullptr;
int                        g_ovlUserTag = 0x4C4D;

// 上下文是宿主临时构造的，先确认这 32 字节确实可读，再去解析，
// 避免把"宿主换了 ABI"这类意外变成一次访问违例。
auto AddressReadable(const void* p, std::size_t n) noexcept -> bool {
	if (p == nullptr) {
		return false;
	}
	MEMORY_BASIC_INFORMATION info {};
	if (::VirtualQuery(p, &info, sizeof(info)) == 0) {
		return false;
	}
	if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
		return false;
	}
	const auto start = reinterpret_cast<std::uintptr_t>(p);
	const auto base  = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
	return start + n <= base + info.RegionSize;
}

auto OverlayProbeHit(int index, const void* ctx, void* userData) noexcept -> void {
	const std::uint32_t hit = g_ovlHits[index].fetch_add(1, std::memory_order_relaxed) + 1;
	if (hit > 3) {
		return;   // 限流：每个回调最多记 3 条，日志别被它淹了
	}

	char line[320] {};
	std::snprintf(line, sizeof(line), "OVL cb%d hit #%u ctx=%p user=%p",
		index, static_cast<unsigned>(hit), ctx, userData);
	LogInfo(line);

	if (!AddressReadable(ctx, 0x20)) {
		LogInfo("OVL: host context not readable as 32 bytes; nothing else logged.");
		return;
	}

	const auto*       bytes = static_cast<const std::uint8_t*>(ctx);
	const std::uint32_t size = *reinterpret_cast<const std::uint32_t*>(bytes);
	const std::uint32_t ver  = *reinterpret_cast<const std::uint32_t*>(bytes + 4);
	if (size != 0x20u) {
		char odd[200] {};
		std::snprintf(odd, sizeof(odd), "OVL: host context size=%u (expected 32) - that is fine, continuing.", size);
		LogInfo(odd);
	}
	const void* imguiCtx = *reinterpret_cast<const void* const*>(bytes + 8);
	const void* hostObj  = *reinterpret_cast<const void* const*>(bytes + 0x10);
	const float vw       = *reinterpret_cast<const float*>(bytes + 0x18);
	const float vh       = *reinterpret_cast<const float*>(bytes + 0x1C);

	char detail[320] {};
	std::snprintf(detail, sizeof(detail),
		"OVL ctx: size=%u ver=%u imgui=%p host=%p viewport=%.1fx%.1f",
		static_cast<unsigned>(size), static_cast<unsigned>(ver),
		imguiCtx, hostObj, static_cast<double>(vw), static_cast<double>(vh));
	LogInfo(detail);
}

// ═══════════════════════════════════════════════════════════════════════════
//  OverlayPanel —— 设置面板（画在宿主 MapSense 那一层 ImGui 上，v0.11.0）
//
//  为什么换这条路：游戏自带的原生面板是"精灵图 + 固定坐标"那一套 ——
//  按钮多大由贴图决定、文字多大由 style 决定，实测摆出来又丑又叠。
//  用户要的是"跟地图插件一样的面板"，而地图插件的面板就是 Dear ImGui 画的。
//
//  宿主早在 registerClient 里把它的 ImGui 上下文（1.91.5）交给我们了，
//  我们带上同一版本的 imgui 源码，用
//      ImGui::SetCurrentContext(宿主上下文)
//  之后所有 ImGui 调用都作用在它那份上下文上，窗口由宿主的渲染管线画。
//  全程不碰 DirectX 交换链 → 结构上不可能再出现 v0.3.4 的"抢渲染主人"崩溃。
//
//  四道闸门（任何一道不过就永久放弃，只写日志、绝不影响游戏）：
//    ① 布局校验：按我们自己的结构体偏移去读宿主上下文里的
//       io.BackendRendererName / io.BackendPlatformName，必须正好是
//       "imgui_impl_dx12" / "imgui_impl_win32"（宿主后端写进去的字符串）。
//       只有两份 imgui 的结构体布局完全一致才可能同时满足。
//    ② 索引宽度自检：反推宿主 ImDrawIdx 的真实宽度，必须与我们的
//       sizeof(ImDrawIdx) 一致。这是唯一"对方能单方面改、改了我们就会
//       写越界"的错配 —— 对方 4 字节、我们 2 字节去写，它的渲染器按
//       4 字节读就会读到分配区外面。自检读不到数据时最多等
//       kVerifyAttemptLimit 次，之后放行（宁可画不成，也不永久卡死）。
//    ③ 帧内校验：WithinFrameScope 为真 + FrameCount 保证一帧只画一次。
//    ④ SEH：读取与绘制都包在 __try/__except 里，出一次异常就永久停用。
// ═══════════════════════════════════════════════════════════════════════════
//  ★ 编译期闸门：我们编出来的 ImGuiContext 布局必须和宿主**逐字节一致**。
//  下面这三个偏移是用反汇编从宿主里读出来的（MapSense 与 Floating Damage
//  两份 DLL 的 ImGuiContext 构造函数初始化序列完全相同）：
//      mov dword [rdi+0x12b0], 0      ; FrameCount = 0
//      mov qword [rdi+0x12b4], -1     ; FrameCountEnded = FrameCountRendered = -1
//      mov byte  [rdi+0x12bc], 0      ; WithinFrameScope = false
//      mov qword [rdi+0x1368], 0      ; CurrentWindow = NULL
//  只要有人给某个目标打开了 IMGUI_DISABLE_OBSOLETE_FUNCTIONS（+0x18 错位），
//  或者升级 Dear ImGui 改了结构体，这里就直接编译不过 ——
//  而不是又一次"装进游戏才发现面板打不开"。
//  复核工具：tools/fmt_layout_probe.py、tools/dump_layout_region.py
// ═══════════════════════════════════════════════════════════════════════════
static_assert(offsetof(ImGuiContext, FrameCount) == 0x12B0,
	"ImGuiContext layout no longer matches the MapSense overlay host "
	"(FrameCount should be 0x12B0, not 0x1298). Someone likely defined "
	"IMGUI_DISABLE_OBSOLETE_FUNCTIONS, which shifts every member after ImGuiIO by 24 bytes; "
	"or Dear ImGui was updated. Re-check with tools/fmt_layout_probe.py.");
static_assert(offsetof(ImGuiContext, WithinFrameScope) == 0x12BC,
	"ImGuiContext layout no longer matches the MapSense overlay host (WithinFrameScope should be 0x12BC).");
static_assert(offsetof(ImGuiContext, CurrentWindow) == 0x1368,
	"ImGuiContext layout no longer matches the MapSense overlay host (CurrentWindow should be 0x1368).");
static_assert(sizeof(ImDrawIdx) == 2,
	"The host renders with 16-bit ImDrawIdx; building with 32-bit indices would make its renderer read past our buffers.");

namespace OverlayPanel {

constexpr std::uint32_t kStateUnknown = 0;
constexpr std::uint32_t kStateReady   = 1;
constexpr std::uint32_t kStateDead    = 2;
// 后端名对上了，但还没拿宿主的索引缓冲验过"索引宽度"这一条 —— 先不下笔画。
constexpr std::uint32_t kStatePending = 3;

// 索引宽度自检最多等多少帧；等不到就放行（宁可不画也不永久卡死）
constexpr std::uint32_t kVerifyAttemptLimit = 900;
// 布局自检最多试多少次。这里必须"可重试"而不是"一次不过就死"：
//   宿主的第一个回调（cb0）是在它**刚开始初始化叠加层**的时候打过来的，
//   那一刻 ImGui_ImplDX12_Init / ImGui_ImplWin32_Init 还没执行，
//   io.BackendRendererName / BackendPlatformName 都还是 nullptr。
//   v0.11.0 第一版就是在这一下把状态钉成了 Dead，后面每帧都没机会再验。
//   实测：cb3 / cb4 都是每帧 ~2 次，2400 次约等于 20 秒。
constexpr std::uint32_t kValidateAttemptLimit = 2400;
// 读宿主上下文连异常都不该有；给几次机会后仍然异常才认定 ABI 真的不兼容。
constexpr std::uint32_t kValidateExceptionLimit = 30;

std::atomic<std::uint32_t> g_state { kStateUnknown };
std::atomic<bool>          g_open { false };
std::atomic<std::uint32_t> g_draws { 0 };
std::uint32_t              g_verifyTries = 0;
std::uint32_t              g_validateTries = 0;
std::uint32_t              g_validateExceptions = 0;
ImGuiContext*              g_ctx       = nullptr;
int                        g_lastFrame = -1;
ImFont*                    g_cjkFont   = nullptr;
bool                       g_cjkHasDigits = false;   // 选中字体里有没有 '0'（没有就不能渲染带数字的中文行）

// 诊断用：自检失败时把"实际读到的东西"留下来，好知道卡在哪一条。
// 只写定长缓冲，绝不持有宿主指针（那可能是一段会失效的内存）。
char  g_diagRenderer[48] {};
char  g_diagPlatform[48] {};
int   g_diagFonts  = -1;
int   g_diagFrame  = -1;
int   g_diagStep   = -1;   // 卡在第几条：1=后端名 2=平台名 3=字体图集 4=字体数 5=帧号

// 绘制进行到第几步。万一绘制抛异常，把它打进日志 —— 这样"是哪一次 ImGui
// 调用炸的"就不用再靠猜（v0.11.1 那次只能看到一句 drawing raised an exception）。
volatile int g_drawStage = 0;

// ── 鼠标接管（v0.11.3）────────────────────────────────────────────────────
// 宿主的叠加层是纯显示用的：它算悬停时把鼠标当成"不在窗口里"，而 ImGui 判定
// 控件能不能点的第一行就是 g.HoveredWindow == 当前窗口 → 我们画出来的窗口
// 点不动、拖不动。所以面板开着的时候，鼠标由我们自己喂：
//   ① 每帧采 Win32 光标位置 + 左/右/中键，直写宿主的 ImGuiIO；
//   ② 清掉 ImGuiConfigFlags_NoMouse（记下原值，关面板时原样还回去）；
//   ③ 鼠标在本窗口上时，把这一帧的 HoveredWindow 指向本窗口。
// 全部只写 ImGuiIO / ImGuiContext 里的**字段**，不调用会分配内存的 API ——
// 我们 /MT 静态 CRT、宿主 /MD 动态 CRT，跨 CRT 分配/释放会踩坏堆。
std::atomic<bool> g_inputTaken { false };
bool     g_hostHadNoMouse  = false;   // 打开面板时宿主是不是关着鼠标（关面板要还回去）
bool     g_rawValid        = false;   // 这一帧采到了没有（游戏不在前台就采不到）
ImVec2   g_rawPos          { -FLT_MAX, -FLT_MAX };   // 采样到的光标位置（宿主坐标系）
bool     g_rawDown[3]      { false, false, false };
ImVec2   g_mousePos        { -FLT_MAX, -FLT_MAX };   // 我们最后写进 ImGuiIO 的位置
bool     g_mouseDown[3]    { false, false, false };
bool     g_mouseDownPrev[3]{ false, false, false };
int      g_inputDiagTick   = 0;
std::atomic<std::uint32_t> g_forcedHoverFrames { 0 };
// 上一帧那扇窗口的指针（拖动要用它的位置算偏移；ImGui 在它每帧都有 Begin 时
// 这个对象是稳定的，用之前仍然会在宿主的窗口表里核一遍）
ImGuiWindow* g_lastWindow = nullptr;

// 自己实现拖动：ImGui 原生拖动靠 io.MouseDelta，而直写鼠标时它恒为 0。
bool  g_dragActive  = false;
bool  g_dragHavePos = false;
float g_dragGrabDX  = 0.0f;
float g_dragGrabDY  = 0.0f;
ImVec2 g_dragPos    { 0.0f, 0.0f };

// 阶段号 → 人能看懂的名字（只用于日志）
auto StageText(int stage) noexcept -> const char* {
	switch (stage) {
	case 0:  return "(before the first draw call)";
	case 1:  return "SetCurrentContext / GetIO";
	case 2:  return "PushFont";
	case 3:  return "Begin(window)";
	case 4:  return "BringWindowToDisplayFront(GetCurrentWindow)";
	case 5:  return "header TextUnformatted";
	case 6:  return "top Checkbox pair";
	case 7:  return "SeparatorText(quality)";
	case 8:  return "BeginTable";
	case 9:  return "TableSetupColumn / TableHeadersRow";
	case 10: return "table body rows";
	case 11: return "EndTable";
	case 12: return "bottom Buttons";
	case 13: return "status section";
	case 14: return "End(window)";
	case 15: return "PopFont";
	default: return "(unknown)";
	}
}

// 只记第一次，避免刷屏。
std::atomic<bool> g_warnedNoFrame  { false };
std::atomic<bool> g_warnedNoWindow { false };
std::atomic<bool> g_warnedVersion  { false };
// 宿主到底来没来叫过我们。用来区分两种情况：
//   · 从没被叫过  → MapSense 不在场，按键该退回游戏原生面板
//   · 叫过了但自检还没过 → 宿主在，稍等一两秒窗口就出来，不该退回
std::atomic<bool> g_hostSeen { false };

// 图形下拉框的选项：下标 → 引擎的 shape 值（NativePanel::kShapeValue）。
// ImGui 的 Combo 要的是"用 \0 分隔的一整串"。
constexpr char kShapeItemsCn[] = "不画\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0";
constexpr char kShapeItemsEn[] = "None\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0";

constexpr const char* kShortLabelEn[kSlotCount] {
	"Normal", "Superior", "Magic", "Rare", "Set", "Unique", "Crafted", "Unknown",
};

auto Tr(const char* chinese, const char* english) noexcept -> const char* {
	return (g_cjkFont != nullptr) ? chinese : english;
}

// 带 %u 数字之类的行专用：宿主字体如果带 CJK 却不带阿拉伯数字
// （实测就会出现"数字渲染成方块/菱形"），这类行退回英文，保证可读。
auto TrNum(const char* chinese, const char* english) noexcept -> const char* {
	return (g_cjkFont != nullptr && g_cjkHasDigits) ? chinese : english;
}

// 逐字节比较（不用 strcmp：指针可能来自别的模块，先自己走一遍更可控）
auto StrEqual(const char* text, const char* want) noexcept -> bool {
	if (text == nullptr || want == nullptr) {
		return false;
	}
	for (std::size_t i = 0; i <= 64; ++i) {
		if (text[i] != want[i]) {
			return false;
		}
		if (want[i] == '\0') {
			return true;
		}
	}
	return false;
}

auto PaletteVec4(int index) noexcept -> ImVec4 {
	if (index < 0 || index >= NativePanel::kPaletteCount) {
		index = 0;
	}
	const NativePanel::PaletteEntry& e = NativePanel::kPalette[index];
	return ImVec4(static_cast<float>(e.r) / 255.0f,
	              static_cast<float>(e.g) / 255.0f,
	              static_cast<float>(e.b) / 255.0f,
	              1.0f);
}

auto ApplyPalette(int slot, int index) noexcept -> void {
	if (slot < 0 || slot >= kSlotCount || index < 0 || index >= NativePanel::kPaletteCount) {
		return;
	}
	const NativePanel::PaletteEntry& e = NativePanel::kPalette[index];
	g_settings.rgbOn[slot] = true;
	// +0.25 再除 255：引擎那边是 (int)(v*255) **截断**取整，
	// 直接写 208/255.0f 可能算出 207.99998 → 截成 207 偏一档。
	g_settings.rgb[slot][0] = (static_cast<float>(e.r) + 0.25f) / 255.0f;
	g_settings.rgb[slot][1] = (static_cast<float>(e.g) + 0.25f) / 255.0f;
	g_settings.rgb[slot][2] = (static_cast<float>(e.b) + 0.25f) / 255.0f;
	SaveSettingsNow();
}

auto ResetColoursToDefault() noexcept -> void {
	const int def[kSlotCount][3] {
		{ 255, 255, 255 },   // 普通   白
		{ 255, 255, 255 },   // 超强   白
		{ 105, 105, 255 },   // 魔法   蓝
		{ 255, 255, 100 },   // 稀有   黄
		{   0, 255,   0 },   // 套装   亮绿（v0.12.0）
		{ 199, 179, 119 },   // 暗金   暗金（v0.12.0）
		{ 255, 168,   0 },   // 手工   橙
		{ 105, 105, 105 },   // 未知   灰
	};
	for (int i = 0; i < kSlotCount; ++i) {
		g_settings.rgbOn[i] = true;
		for (int c = 0; c < 3; ++c) {
			g_settings.rgb[i][c] = (static_cast<float>(def[i][c]) + 0.25f) / 255.0f;
		}
	}
	SaveSettingsNow();
}

// 把宿主那边的一个字符串搬进我们自己的定长缓冲（不保留宿主指针）。
// 只读 cap-1 个字节，来源指针若是垃圾值会在这里触发异常，由外层 __try 兜住。
auto CopyBounded(const char* src, char* dst, std::size_t cap) noexcept -> void {
	if (dst == nullptr || cap == 0) {
		return;
	}
	dst[0] = '\0';
	if (src == nullptr) {
		return;
	}
	std::size_t i = 0;
	for (; i + 1 < cap; ++i) {
		const char ch = src[i];
		if (ch == '\0') {
			break;
		}
		dst[i] = (ch >= 32 && ch < 127) ? ch : '?';
	}
	dst[i] = '\0';
}

// ── 鼠标接管的具体实现 ──
//  这几段整段包 SEH：读宿主上下文 / 拿 Win32 句柄都可能出意外，
//  但**绝不能**因为一次意外就把面板判死（自检阶段已经有那个教训）。

// 游戏那扇窗口的句柄。优先用 imgui_impl_win32 自己存的那个
// （BackendPlatformUserData 指向 ImGui_ImplWin32_Data，第一个成员就是 HWND），
// 拿不到再退回前台窗口 / 光标底下的窗口。
auto ResolveWindow(ImGuiContext* c) noexcept -> HWND {
	HWND hwnd = nullptr;
	__try {
		const void* userData = c->IO.BackendPlatformUserData;
		if (userData != nullptr) {
			std::memcpy(&hwnd, userData, sizeof(hwnd));
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		hwnd = nullptr;
	}
	if (hwnd != nullptr && ::IsWindow(hwnd) != FALSE) {
		return hwnd;
	}
	hwnd = ::GetForegroundWindow();
	if (hwnd != nullptr) {
		const HWND root = ::GetAncestor(hwnd, GA_ROOT);
		return (root != nullptr) ? root : hwnd;
	}
	POINT cursor {};
	if (::GetCursorPos(&cursor) != FALSE) {
		hwnd = ::WindowFromPoint(cursor);
	}
	return hwnd;
}

// 采一次鼠标：填 g_raw*。游戏不在前台就当作"采不到"（别把点击喂进面板）。
auto SampleMouse(ImGuiContext* c) noexcept -> bool {
	g_rawValid = false;
	const HWND hwnd = ResolveWindow(c);
	if (hwnd == nullptr) {
		return false;
	}
	// ★ v0.13.0：记下游戏窗口（面板挡鼠标要拿它的线程装 WH_MOUSE 钩子）。
	g_gameHwnd = hwnd;
	const HWND foreground = ::GetForegroundWindow();
	const HWND rootOfGame = ::GetAncestor(hwnd, GA_ROOT);
	const bool focused = (foreground == hwnd)
	                  || (rootOfGame != nullptr && foreground == rootOfGame);
	if (!focused) {
		return false;
	}
	POINT cursor {};
	if (::GetCursorPos(&cursor) == FALSE) {
		return false;
	}
	RECT client {};
	if (::GetClientRect(hwnd, &client) == FALSE) {
		return false;
	}
	const float cw = static_cast<float>(client.right - client.left);
	const float ch = static_cast<float>(client.bottom - client.top);
	if (cw < 2.0f || ch < 2.0f) {
		return false;
	}
	if (::ScreenToClient(hwnd, &cursor) == FALSE) {
		return false;
	}
	float x = static_cast<float>(cursor.x);
	float y = static_cast<float>(cursor.y);
	// 宿主的 DisplaySize 和客户区不一致（缩放/DPI）时按比例换算，
	// 这样"看到的位置"和"点的位置"永远是同一个点。
	const ImVec2 display = c->IO.DisplaySize;
	if (display.x > 2.0f && display.y > 2.0f) {
		x *= display.x / cw;
		y *= display.y / ch;
	}
	g_rawPos   = ImVec2(x, y);
	g_rawDown[0] = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	g_rawDown[1] = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	g_rawDown[2] = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
	g_rawValid = true;
	return true;
}

// 把这一次采样写进宿主的 ImGuiIO，并维护"上一帧按键状态"（算按下/松开边沿用）。
auto FeedMouse(ImGuiContext* c) noexcept -> void {
	const bool ok = SampleMouse(c);
	for (int i = 0; i < 3; ++i) {
		g_mouseDownPrev[i] = g_mouseDown[i];
		g_mouseDown[i]     = ok ? g_rawDown[i] : false;   // 采不到 = 全部松开
	}
	if (ok) {
		g_mousePos = g_rawPos;
	} else {
		g_mousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	}
	// 直写字段（不用 AddMousePosEvent：那个会往宿主的 ImVector 里分配内存）。
	// 采不到时给 -FLT_MAX，ImGui 会认为鼠标离开了窗口并把 ActiveId 清掉，
	// 正好是我们想要的（乱点不会卡住）。
	c->IO.MousePos = g_mousePos;
	c->IO.MouseDown[0] = g_mouseDown[0];
	c->IO.MouseDown[1] = g_mouseDown[1];
	c->IO.MouseDown[2] = g_mouseDown[2];
}

// 面板打开时接管鼠标：清 NoMouse + 每帧喂一次。
auto OwnInput(ImGuiContext* c) noexcept -> void {
	__try {
		if (!g_inputTaken.load(std::memory_order_relaxed)) {
			g_hostHadNoMouse = (c->IO.ConfigFlags & ImGuiConfigFlags_NoMouse) != 0;
			g_inputTaken.store(true, std::memory_order_relaxed);
			char line[240] {};
			std::snprintf(line, sizeof(line),
				"Overlay panel: taking the mouse over so the window can be dragged and clicked "
				"(host had NoMouse=%s, display=%.0fx%.0f).",
				g_hostHadNoMouse ? "yes" : "no",
				static_cast<double>(c->IO.DisplaySize.x),
				static_cast<double>(c->IO.DisplaySize.y));
			LogInfo(line);
		}
		// ImGui 只要看到这个标志就彻底无视鼠标（连"鼠标在谁身上"都不算），
		// 于是所有控件永远点不到 —— 必须先清掉。
		c->IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		FeedMouse(c);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// 这一帧读/写宿主上下文出了意外：算了，下次再来（不判死）
	}
}

// 面板关掉：鼠标交还宿主，它原有的设置原样还回去。
auto ReleaseInput(ImGuiContext* c) noexcept -> void {
	if (!g_inputTaken.load(std::memory_order_relaxed)) {
		return;
	}
	g_inputTaken.store(false, std::memory_order_relaxed);
	g_dragActive  = false;
	g_rawValid    = false;
	g_mousePos    = ImVec2(-FLT_MAX, -FLT_MAX);
	for (int i = 0; i < 3; ++i) {
		g_mouseDown[i]     = false;
		g_mouseDownPrev[i] = false;
	}
	__try {
		if (c != nullptr && g_hostHadNoMouse) {
			c->IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	LogInfo("Overlay panel: closed -- the mouse was handed back and the host's original "
	        "ImGuiConfigFlags were restored.");
}

// ───────── ★ v0.13.0：面板挡鼠标（WH_MOUSE 钩子，装成钩子链的第一节）─────────
//  跑在**游戏的 UI 线程**里：必须极轻、不许分配内存、不许抛异常。
//  code < 0 时按规矩原样往下传；只有"面板矩形内的按键消息"才吞掉。
auto CALLBACK GameMouseHookProc(int code, WPARAM message, LPARAM lp) noexcept -> LRESULT {
	if (code == HC_ACTION && lp != 0
	    && g_blockGameMouse.load(std::memory_order_relaxed)
	    && g_panelScrValid.load(std::memory_order_relaxed)) {
		bool isButton = false;
		switch (message) {
		case WM_LBUTTONDOWN: case WM_LBUTTONUP:
		case WM_RBUTTONDOWN: case WM_RBUTTONUP:
		case WM_MBUTTONDOWN: case WM_MBUTTONUP:
		case WM_XBUTTONDOWN: case WM_XBUTTONUP:
		case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
			isButton = true;
			break;
		default:
			break;
		}
		if (isButton) {
			// WH_MOUSE 的 lParam = MOUSEHOOKSTRUCT*，pt 是**屏幕**坐标。
			const auto* info = reinterpret_cast<const MOUSEHOOKSTRUCT*>(lp);
			const int x = static_cast<int>(info->pt.x);
			const int y = static_cast<int>(info->pt.y);
			if (x >= g_panelScrX0.load(std::memory_order_relaxed)
			    && x <= g_panelScrX1.load(std::memory_order_relaxed)
			    && y >= g_panelScrY0.load(std::memory_order_relaxed)
			    && y <= g_panelScrY1.load(std::memory_order_relaxed)) {
				if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
					// ★ v0.18.2：一次性诊断 —— 游戏线程队列里有没有滚轮消息。
					static bool s_wheelLogged = false;
					if (!s_wheelLogged) {
						s_wheelLogged = true;
						LogInfo("Panel wheel: WH_MOUSE thread hook also sees WM_MOUSEWHEEL over the panel.");
					}
				}
				g_swallowedClicks.fetch_add(1, std::memory_order_relaxed);
				return 1;   // 吞掉：后面的钩子与窗口过程都收不到这一下
			}
		}
	}
	return CallNextHookEx(nullptr, code, message, lp);
}

// ★ v0.18.1：面板滚轮 —— WH_MOUSE 线程钩子拿不到滚轮幅度（MOUSEHOOKSTRUCT 里
//  没有 delta），所以这里挂一个**低级鼠标钩子（WH_MOUSE_LL）+ 专用消息泵线程**：
//  光标在面板矩形内时把滚轮格数记进原子量、并吞掉（不让游戏同时滚）；
//  面板绘制线程再把格数喂给 ImGui 的 io.MouseWheel，窗口就能正常滚动了。
std::atomic<int>           g_panelWheelSteps { 0 };
std::atomic<unsigned long> g_wheelHookThread  { 0 };
std::atomic<bool>          g_wheelHookQuit    { false };
static HHOOK               g_wheelLlHook      = nullptr;

auto CALLBACK PanelWheelLlProc(int code, WPARAM wp, LPARAM lp) noexcept -> LRESULT {
	if (code == HC_ACTION && wp == WM_MOUSEWHEEL && lp != 0) {
		// ★ v0.18.2：一次性诊断 —— 低级钩子到底有没有收到滚轮、光标在哪、
		//   "光标在面板内"标志是什么状态。有这行就能定位卡在哪一环。
		static bool s_diagLogged = false;
		const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
		if (!s_diagLogged) {
			s_diagLogged = true;
			char line[260] {};
			std::snprintf(line, sizeof(line),
				"Panel wheel: LL hook sees WM_MOUSEWHEEL at (%ld,%ld), panelVisible=%d mouseInPanel=%d",
				info->pt.x, info->pt.y,
				g_panelVisible.load(std::memory_order_relaxed) ? 1 : 0,
				g_mouseInPanel.load(std::memory_order_relaxed) ? 1 : 0);
			LogInfo(line);
		}
		if (g_panelVisible.load(std::memory_order_relaxed)
		    && g_mouseInPanel.load(std::memory_order_relaxed)) {
			const short delta = static_cast<short>((info->mouseData >> 16) & 0xFFFFu);
			g_panelWheelSteps.fetch_add(delta / WHEEL_DELTA, std::memory_order_relaxed);
			return 1;   // 吞掉：面板自己滚动，别让游戏同时收到
		}
	}
	return CallNextHookEx(nullptr, code, wp, lp);
}

auto WINAPI PanelWheelHookThreadMain(void*) noexcept -> unsigned long {
	g_wheelLlHook = ::SetWindowsHookExW(WH_MOUSE_LL, &PanelWheelLlProc, nullptr, 0);
	if (g_wheelLlHook == nullptr) {
		LogInfo("Loot Map: WH_MOUSE_LL install failed; panel wheel scrolling unavailable.");
		return 0;
	}
	LogInfo("Loot Map: panel wheel hook (WH_MOUSE_LL) installed.");
	MSG msg {};
	while (g_wheelHookQuit.load(std::memory_order_relaxed) == false) {
		const BOOL r = ::GetMessageW(&msg, nullptr, 0, 0);
		if (r <= 0) {
			break;   // WM_QUIT
		}
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}
	::UnhookWindowsHookEx(g_wheelLlHook);
	g_wheelLlHook = nullptr;
	LogInfo("Loot Map: panel wheel hook removed.");
	return 0;
}

auto EnsurePanelWheelHookThread() noexcept -> void {
	if (g_wheelHookThread.load(std::memory_order_relaxed) != 0) {
		return;
	}
	unsigned long tid = 0;
	const HANDLE th = ::CreateThread(nullptr, 0,
		reinterpret_cast<unsigned long(__stdcall*)(void*)>(&PanelWheelHookThreadMain),
		nullptr, 0, &tid);
	if (th != nullptr) {
		g_wheelHookThread.store(tid, std::memory_order_relaxed);
		::CloseHandle(th);
	}
}

auto StopPanelWheelHookThread() noexcept -> void {
	const unsigned long tid = g_wheelHookThread.load(std::memory_order_relaxed);
	if (tid == 0) {
		return;
	}
	g_wheelHookQuit.store(true, std::memory_order_relaxed);
	::PostThreadMessageW(tid, WM_QUIT, 0, 0);
	for (int i = 0; i < 40 && g_wheelHookThread.load(std::memory_order_relaxed) != 0; ++i) {
		::Sleep(10);   // 最多等 0.4 秒，务必等钩子摘完再卸 DLL
	}
}

// 把"面板矩形（ImGui 显示坐标）"换算成"屏幕像素"，写进给钩子读的那几个原子量。
// 换算规则与 SampleMouse 完全一致（显示坐标 = 客户区坐标 × display/客户区），
// 否则钩子里的比较会和眼睛看到的差一截。
auto UpdatePanelScreenRect(ImGuiContext* c, HWND hwnd) noexcept -> void {
	__try {
		if (c == nullptr || hwnd == nullptr || !::IsWindow(hwnd)
		    || !g_panelVisible.load(std::memory_order_relaxed)) {
			g_panelScrValid.store(false, std::memory_order_relaxed);
			return;
		}
		RECT client {};
		if (::GetClientRect(hwnd, &client) == FALSE) {
			g_panelScrValid.store(false, std::memory_order_relaxed);
			return;
		}
		const float cw = static_cast<float>(client.right - client.left);
		const float ch = static_cast<float>(client.bottom - client.top);
		const ImVec2 display = c->IO.DisplaySize;
		if (cw < 2.0f || ch < 2.0f || display.x < 2.0f || display.y < 2.0f) {
			g_panelScrValid.store(false, std::memory_order_relaxed);
			return;
		}
		POINT origin { 0, 0 };
		if (::ClientToScreen(hwnd, &origin) == FALSE) {
			g_panelScrValid.store(false, std::memory_order_relaxed);
			return;
		}
		const float kx = cw / display.x;
		const float ky = ch / display.y;
		g_panelScrX0.store(origin.x + static_cast<int>(g_panelX0 * kx), std::memory_order_relaxed);
		g_panelScrY0.store(origin.y + static_cast<int>(g_panelY0 * ky), std::memory_order_relaxed);
		g_panelScrX1.store(origin.x + static_cast<int>(g_panelX1 * kx), std::memory_order_relaxed);
		g_panelScrY1.store(origin.y + static_cast<int>(g_panelY1 * ky), std::memory_order_relaxed);
		g_panelScrValid.store(true, std::memory_order_relaxed);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_panelScrValid.store(false, std::memory_order_relaxed);
	}
}

// 面板关掉 / 卸载：把每个槽位上的钩子都摘掉，钩子链恢复原样。
auto ReleaseGameMouseHook() noexcept -> void {
	bool any = false;
	for (int i = 0; i < kMouseHookSlots; ++i) {
		if (g_gameMouseHooks[i].handle != nullptr) {
			::UnhookWindowsHookEx(static_cast<HHOOK>(g_gameMouseHooks[i].handle));
			g_gameMouseHooks[i].handle = nullptr;
			g_gameMouseHooks[i].tid    = 0;
			any = true;
		}
	}
	g_gameMouseHookCount.store(0, std::memory_order_relaxed);
	if (any) {
		LogInfo("Loot Map: panel mouse hook removed.");
	}
}

// 装钩子（面板可见时每帧都会调用；只有"目标线程变了"才真的摘/装）。
//   槽位 0 = 游戏窗口所在线程（正常情况下就是它）；
//   槽位 1 = 当前前台顶层窗口线程（游戏窗口指针万一失效时的兜底）。
// 只允许挂**我们自己进程里的**线程：跨进程挂 WH_MOUSE 需要把本 DLL 注入对方
// 进程去，那是完全另一码事，这里不做（会 log 一行 warn 说明为什么没挂上）。
auto EnsureGameMouseHook() noexcept -> void {
	if (!g_blockGameMouse.load(std::memory_order_relaxed)) {
		ReleaseGameMouseHook();
		return;
	}
	const DWORD selfPid = ::GetCurrentProcessId();

	std::uint32_t want[kMouseHookSlots] {};
	int  wantCount  = 0;
	bool sawForeign = false;
	auto consider = [&](HWND h) {
		if (h == nullptr || wantCount >= kMouseHookSlots || !::IsWindow(h)) {
			return;
		}
		DWORD pid = 0;
		const DWORD t = ::GetWindowThreadProcessId(h, &pid);
		if (t == 0) {
			return;
		}
		if (pid != selfPid) {
			sawForeign = true;   // 别的进程的窗口：从这里挂不了
			return;
		}
		for (int i = 0; i < wantCount; ++i) {
			if (want[i] == t) {
				return;   // 同一个线程，去重
			}
		}
		want[wantCount++] = t;
	};
	consider(g_gameHwnd);
	consider(::GetForegroundWindow());

	if (wantCount == 0) {
		// 还没拿到游戏窗口（还在菜单/加载），下一帧再试；"窗口在别的进程"只提示一次。
		if (sawForeign) {
			const std::uint64_t nowMs = ::GetTickCount64();
			if (nowMs >= g_mouseHookWarnNextMs) {
				g_mouseHookWarnNextMs = nowMs + 5000;
				LogWarn("Loot Map: the game window lives in another process, so the panel "
				        "cannot swallow clicks from here (clicks will reach the game).");
			}
		}
		return;
	}

	// 先摘掉"已经挂错线程"的；对上的留着（避免每帧重装出输入缺口）。
	int have = 0;
	for (int i = 0; i < kMouseHookSlots; ++i) {
		if (g_gameMouseHooks[i].handle == nullptr) {
			continue;
		}
		bool keep = false;
		for (int j = 0; j < wantCount; ++j) {
			if (g_gameMouseHooks[i].tid == want[j]) {
				keep = true;
			}
		}
		if (keep) {
			++have;
		} else {
			::UnhookWindowsHookEx(static_cast<HHOOK>(g_gameMouseHooks[i].handle));
			g_gameMouseHooks[i].handle = nullptr;
			g_gameMouseHooks[i].tid    = 0;
		}
	}
	g_gameMouseHookCount.store(have, std::memory_order_relaxed);
	if (have == wantCount) {
		return;   // 该挂的都挂上了：什么都不做
	}

	HINSTANCE self = nullptr;
	if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
	                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                         reinterpret_cast<LPCWSTR>(&GameMouseHookProc), &self) == FALSE
	    || self == nullptr) {
		LogWarn("Loot Map: could not resolve our own module for the mouse hook; "
		        "clicks on the panel will still reach the game.");
		return;
	}
	for (int j = 0; j < wantCount; ++j) {
		bool already = false;
		for (int i = 0; i < kMouseHookSlots; ++i) {
			if (g_gameMouseHooks[i].handle != nullptr && g_gameMouseHooks[i].tid == want[j]) {
				already = true;
			}
		}
		if (already) {
			continue;
		}
		int slot = -1;
		for (int i = 0; i < kMouseHookSlots; ++i) {
			if (g_gameMouseHooks[i].handle == nullptr) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			break;
		}
		const HHOOK hook = ::SetWindowsHookExW(WH_MOUSE, &GameMouseHookProc, self,
		                                       static_cast<DWORD>(want[j]));
		if (hook == nullptr) {
			char line[240] {};
			std::snprintf(line, sizeof(line),
				"Loot Map: SetWindowsHookExW(WH_MOUSE) failed for thread %lu "
				"(GetLastError=%lu); clicks on the panel will still reach the game.",
				static_cast<unsigned long>(want[j]),
				static_cast<unsigned long>(::GetLastError()));
			LogWarn(line);
			continue;
		}
		g_gameMouseHooks[slot].handle = hook;
		g_gameMouseHooks[slot].tid    = want[j];
		++have;
		char line[220] {};
		std::snprintf(line, sizeof(line),
			"Loot Map: panel mouse hook installed on thread %lu "
			"(clicks inside the panel are now swallowed before the game sees them).",
			static_cast<unsigned long>(want[j]));
		LogInfo(line);
	}
	g_gameMouseHookCount.store(have, std::memory_order_relaxed);
}

// 上一帧的窗口指针；先在宿主的窗口表里核一遍，避免用一个已经失效的指针。
auto LastWindowAlive(ImGuiContext* c) noexcept -> ImGuiWindow* {
	ImGuiWindow* w = g_lastWindow;
	if (w == nullptr) {
		return nullptr;
	}
	__try {
		const int count = c->Windows.Size;
		if (count > 0 && count <= 4096 && c->Windows.Data != nullptr) {
			for (int i = 0; i < count; ++i) {
				if (c->Windows.Data[i] == w) {
					return w;
				}
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	g_lastWindow = nullptr;
	return nullptr;
}

// 把 Hosted 那边的一扇窗口名字搬进我们自己的缓冲（只用于日志，不留指针）。
auto ReadWindowName(ImGuiWindow* w, char* out, std::size_t cap) noexcept -> void {
	if (out == nullptr || cap == 0) {
		return;
	}
	out[0] = '\0';
	if (w == nullptr) {
		std::snprintf(out, cap, "(none)");
		return;
	}
	__try {
		const char* name = nullptr;
		std::memcpy(&name, reinterpret_cast<const std::uint8_t*>(w) + 8, sizeof(name));
		CopyBounded(name, out, cap);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		std::snprintf(out, cap, "(unreadable)");
	}
}

// 让"本窗口这一帧能被鼠标碰到"。
// ImGui 判定控件能不能点的第一行就是 g.HoveredWindow == 当前窗口
// （imgui.cpp ItemHoverable @ 4545）。宿主每帧算悬停时可能因为 NoMouse、
// 或者它自己的窗口挡在前面，把悬停判给了别人/留空。
// 鼠标落在窗口范围内时，我们直接把这两个指针指过去 ——
// 只影响**我们这一帧**：宿主下一个 NewFrame 自己会重算，它不受影响。
// 注意：色板是**另一个** ImGui 窗口（弹窗），有弹窗时只认弹窗 ——
// 否则弹窗里那些色块会因为"悬停被判给主窗口"而点不动；
// 而弹窗开着时主窗口的控件本来也会被 ImGui 挡住
// （ItemHoverable 里 IsWindowContentHoverable 那一条），指过去也没用。
auto ForceHover(ImGuiContext* c, ImGuiWindow* w) noexcept -> void {
	if (w == nullptr || !g_rawValid) {
		return;
	}
	ImGuiWindow* target = w;
	__try {
		const int popupCount = c->OpenPopupStack.Size;
		if (popupCount > 0 && c->OpenPopupStack.Data != nullptr) {
			target = c->OpenPopupStack.Data[popupCount - 1].Window;   // 可能是 nullptr（还没解析出来）
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return;
	}
	if (target == nullptr) {
		return;
	}
	const ImVec2 mp = g_mousePos;
	const ImVec2 p  = target->Pos;
	const ImVec2 s  = target->Size;
	if (mp.x < p.x || mp.x > p.x + s.x || mp.y < p.y || mp.y > p.y + s.y) {
		return;   // 光标不在这个窗口上：不抢，宿主的判断保持原样
	}
	if (c->HoveredWindow != target) {
		c->HoveredWindow = target;
		c->HoveredWindowUnderMovingWindow = target;
		g_forcedHoverFrames.fetch_add(1, std::memory_order_relaxed);
	}
}

// ── 校验宿主的 ImGui 上下文（读完可能崩，所以整段包 SEH）──
//  返回值：1 = 通过；0 = 这一帧还不满足（可重试，不是错误）；-1 = 读的时候出异常。
//  注意这里**绝不能**因为 0 就永久放弃 —— 宿主的第一个回调来得比它自己的
//  渲染后端初始化还早，那时后端名字段还是空的。
auto SafeValidate(ImGuiContext* c) noexcept -> int {
	__try {
		g_diagRenderer[0] = '\0';
		g_diagPlatform[0] = '\0';
		g_diagFonts = -1;
		g_diagFrame = -1;
		g_diagStep  = 1;

		g_diagFonts = (c->IO.Fonts != nullptr) ? c->IO.Fonts->Fonts.Size : -1;
		g_diagFrame = c->FrameCount;
		CopyBounded(c->IO.BackendRendererName, g_diagRenderer, sizeof(g_diagRenderer));
		CopyBounded(c->IO.BackendPlatformName, g_diagPlatform, sizeof(g_diagPlatform));

		if (!StrEqual(c->IO.BackendRendererName, "imgui_impl_dx12")) {
			return 0;
		}
		g_diagStep = 2;
		if (!StrEqual(c->IO.BackendPlatformName, "imgui_impl_win32")) {
			return 0;
		}
		g_diagStep = 3;
		if (c->IO.Fonts == nullptr) {
			return 0;
		}
		g_diagStep = 4;
		if (g_diagFonts < 1 || g_diagFonts > 64) {
			return 0;
		}
		g_diagStep = 5;
		if (g_diagFrame < 0 || g_diagFrame > 0x0FFFFFF) {
			return 0;
		}
		g_diagStep = 0;
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

// 宿主的字体图集里有没有带中文的字体（MapSense 自己会加载 msyh.ttc 之类）。
// 有就直接用它的 —— 我们绝不能自己往宿主的图集里加字体，
// 那会让字形 UV 指到还没上传的纹理上，画面会花。
auto SafeFindCjkFont(ImGuiContext* c) noexcept -> ImFont* {
	__try {
		ImFontAtlas* atlas = c->IO.Fonts;
		ImFont*      cjkOnly = nullptr;
		for (int i = 0; i < atlas->Fonts.Size; ++i) {
			ImFont* font = atlas->Fonts[i];
			if (font == nullptr) {
				continue;
			}
			// U+54C1「品」：只要有它就说明这个字体带 CJK。
			// v0.12.1：再查 '0' —— 实测有的字体带 CJK 不带数字，
			// 数字会渲染成乱块；带数字的优先，只有 CJK 的做备胎。
			if (font->FindGlyphNoFallback(static_cast<ImWchar>(0x54C1)) != nullptr) {
				if (font->FindGlyphNoFallback(static_cast<ImWchar>('0')) != nullptr) {
					g_cjkHasDigits = true;
					return font;
				}
				if (cjkOnly == nullptr) {
					cjkOnly = font;
				}
			}
		}
		if (cjkOnly != nullptr) {
			LogWarn("Loot Map: the CJK-capable font has no digit glyphs; "
			        "number-bearing lines will fall back to English.");
		}
		return cjkOnly;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

// ── 索引宽度自检（最要命的一条错配）──
//  两边各自编译了一份 ImGui，绝大多数结构体布局天然一致；但有一个东西
//  是可以单方面改的：imconfig.h 里的 `ImDrawIdx`（默认 unsigned short，
//  有人为了支持 6.5 万以上顶点会改成 unsigned int）。
//  万一对方是 32 位、我们是 16 位：我们按 2 字节往它的索引缓冲里写、
//  按 2 字节申请内存，它的渲染器却按 4 字节去读 —— 直接读越界，游戏崩。
//
//  所以下笔之前先反推对方的真实宽度，用的是它上一帧已经画好的索引缓冲：
//    · 把同一段字节按 4 字节解读。如果是 16 位索引，(idx[2k+1]<<16)|idx[2k]
//      里那个高位几乎不可能是 0，于是解出来的数会**大于**顶点数；
//    · 反过来，如果按 4 字节读出来的值**全都**小于顶点数，那它就是 32 位。
//  只在真的读到过数据时才下结论；读不到返回 0，由调用方限次后放行。
//  返回：1 = 宽度一致；0 = 这次没数据可判；-1 = **确认**宽度不一致；
//        -2 = 读的时候出异常（≠ 不一致，别当成结论用）。
auto SafeIndexWidthCheck(ImGuiContext* c) noexcept -> int {
	__try {
		if (c->Viewports.Size < 1 || c->Viewports.Data == nullptr) {
			return 0;
		}
		ImGuiViewportP* vp = c->Viewports.Data[0];
		if (vp == nullptr) {
			return 0;
		}
		const ImDrawData& dd = vp->DrawDataP;
		const int listCount = dd.CmdLists.Size;
		if (listCount <= 0 || listCount > 512 || dd.CmdLists.Data == nullptr) {
			return 0;   // 宿主这一帧还没产出绘制数据，下次再看
		}

		int checked = 0;
		for (int i = 0; i < listCount && checked < 4; ++i) {
			ImDrawList* dl = dd.CmdLists.Data[i];
			if (dl == nullptr) {
				continue;
			}
			const int idxCount = dl->IdxBuffer.Size;
			const int vtxCount = dl->VtxBuffer.Size;
			if (idxCount < 16 || idxCount > (1 << 22)) {
				continue;   // 空列表没什么可判的
			}
			if (vtxCount <= 0 || vtxCount > (1 << 22)) {
				continue;   // 值不对劲 —— 当"这次读不出来"，绝不当成"确认错配"
			}
			if (dl->IdxBuffer.Data == nullptr || dl->VtxBuffer.Data == nullptr) {
				continue;
			}

			// 只探前半段字节：即便对方真是 16 位，也绝不会读到分配区外面去。
			int pairs = idxCount / 2;
			if (pairs > 16) {
				pairs = 16;
			}
			const std::uint32_t* wide = reinterpret_cast<const std::uint32_t*>(dl->IdxBuffer.Data);
			bool allWide  = true;
			bool anyValid = false;
			for (int k = 0; k < pairs; ++k) {
				const std::uint32_t raw = wide[k];
				if (raw == 0u) {
					continue;   // 0 在两种宽度下都成立，不参与判定
				}
				anyValid = true;
				if (raw >= static_cast<std::uint32_t>(vtxCount)) {
					allWide = false;   // 4 字节解读不成立 → 对方是 16 位
					break;
				}
			}
			if (!anyValid) {
				continue;   // 一整段全是 0（缓冲区刚分配还没填）：判不出来，换下一个
			}

			const std::size_t detected = allWide ? 4u : 2u;
			if (detected != sizeof(ImDrawIdx)) {
				return -1;   // 只有"确认"才回 -1
			}
			++checked;
		}
		return (checked > 0) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;   // 读的时候出异常 ≠ 宽度不一致
	}
}

// ★ v0.13.0：挑一块"笔画统一"的面板字体。
//   ★ v0.17.5：彻底翻案 —— 之前用"品"字当中文探针，把**地图插件自己的字体**
//   误杀了（它的界面里不会出现"品"字，但面板实际用到的字它基本都有，而且是
//   单一来源 = 地图插件那种统一观感）。这版：
//     ① 把 18 块字体**全部**登记进日志（名字/字号/来源数/数字/探针字覆盖数）；
//     ② 挑选判据：来源数最少优先 → 面板探针字覆盖最多 → 字号接近 17。
//   探针字 = 面板文案实际用到的汉字全集的采样。
ImFont* PickPanelFont(ImGuiContext* c) noexcept {
	if (c == nullptr || c->IO.Fonts == nullptr) {
		return nullptr;
	}
	// 面板文案实际用到的汉字采样（覆盖数越多，"一种字型"的概率越大）。
	static const ImWchar kProbes[] {
		0x6389, 0x843D, 0x7269, 0x5730, 0x56FE, 0x6807, 0x8BB0,   // 掉落物地图标记
		0x663E, 0x793A, 0x603B, 0x5F00, 0x5173,                   // 显示总开关
		0x54C1, 0x8D28, 0x5957, 0x88C5, 0x89C4, 0x5219,           // 品质套装规则
		0x661F, 0x6837, 0x5F0F, 0x5927, 0x5C0F, 0x63CF, 0x8FB9,   // 星星样式大小描边
		0x84DD, 0x73E0, 0x5B9D, 0x5320, 0x7B80, 0x7EA6, 0x5173,   // 蓝色珠宝匠简约
	};
	constexpr int kProbeCount = static_cast<int>(sizeof(kProbes) / sizeof(kProbes[0]));
	ImFont* best      = nullptr;
	int     bestScore = -1000000;
	int     bestSources = 0;
	int     bestProbe = 0;
	// ★ v0.17.5：一次性把字体库里**所有**字体登记进日志，以后挑字体对着这张清单。
	static bool loggedAll = false;
	for (int i = 0; i < c->IO.Fonts->Fonts.Size; ++i) {
		ImFont* f = c->IO.Fonts->Fonts[i];
		if (f == nullptr || f->FontSize <= 1.0f) {
			continue;
		}
		const bool hasDigits = (f->FindGlyphNoFallback('0') != nullptr);
		int probe = 0;
		for (int k = 0; k < kProbeCount; ++k) {
			if (f->FindGlyphNoFallback(kProbes[k]) != nullptr) {
				++probe;
			}
		}
		const int   sources = (f->ConfigDataCount > 0) ? f->ConfigDataCount : 1;
		const char* name    = f->GetDebugName();
		if (!loggedAll) {
			char line[300] {};
			std::snprintf(line, sizeof(line),
				"Font inventory: [%d] '%s' size=%.0f sources=%d digits=%d probe=%d/%d",
				i, (name != nullptr) ? name : "<unknown>",
				static_cast<double>(f->FontSize), sources,
				hasDigits ? 1 : 0, probe, kProbeCount);
			LogInfo(line);
		}
		// ★ v0.17.6：**全覆盖硬条件** —— v0.17.5 挑中了残缺的"地图插件字体"
		//   （只含它界面用过的字），面板大多数汉字变问号。差一个字都不行：
		//   没有全覆盖的单源字体时，宁可退回全覆盖合并字体（可读优先），
		//   自带雅黑图集（上面的 dx12 路线）才是治本。
		if (!hasDigits || probe != kProbeCount) {
			continue;
		}
		// 来源数最少（每少一源 +1000）→ 探针覆盖最多（每字 +10）→ 字号近 17。
		int         score   = -sources * 1000 + probe * 10;
		const float diff    = f->FontSize - 17.0f;
		score -= static_cast<int>((diff < 0.0f ? -diff : diff) * 2.0f);
		if (score > bestScore) {
			bestScore   = score;
			best        = f;
			bestSources = sources;
			bestProbe   = probe;
		}
	}
	loggedAll = true;
	static bool logged = false;
	if (!logged) {
		logged = true;
		char line[300] {};
		if (best != nullptr) {
			const char* name = best->GetDebugName();
			std::snprintf(line, sizeof(line),
				"Overlay panel: font [%s] size=%.0f sources=%d probe=%d/%d -> %s",
				(name != nullptr) ? name : "<unknown>",
				static_cast<double>(best->FontSize), bestSources, bestProbe, kProbeCount,
				(bestSources <= 1) ? "single-source (uniform)" : "merged atlas");
		} else {
			std::snprintf(line, sizeof(line),
				"Overlay panel: no usable font found; using fallback.");
		}
		LogInfo(line);
	}
	return best;
}

// ─────────── ★ v0.17.6：自建字体图集（面板字体统一的最终方案，DX12 版）───────────
//  实测（2026-09-22 23:05 日志）：宿主渲染器 = imgui_impl_dx12（D2R 是 DX12）。
//  共享字体库里全覆盖的字体全是多源合并（中文分散在多个源 ⇒ 混排），残缺源又会
//  把面板变问号 ⇒ 唯一出路：插件**自己带一块单源中文字体**：
//    ① 自建 ImFontAtlas，从 Windows 自带字体（微软雅黑等）载入常用汉字；
//    ② 用宿主的 D3D12 设备（imgui_impl_dx12 数据区第 1 个指针）建纹理，
//       SRV 描述符写进宿主自己的 SRV 堆（第 4 个指针）的最后一个槽 —— dx12 后端
//       每帧绑定的就是这个堆，TexID = 堆内 GPU 句柄 ⇒ 自建纹理能被直接画出来；
//       像素上传用同一设备上**自建的命令队列**（不碰游戏的队列）， fence 等待完成；
//    ③ 堆太小 / 任何一步失败 → 退回"全覆盖合并字体"（可读优先），绝不带崩游戏。
static ImFontAtlas                 g_ownAtlas     {};
static ImFont*                     g_ownFont      = nullptr;
static bool                        g_ownFontTried = false;
static ID3D12Resource*             g_ownFontTex   = nullptr;
static D3D12_GPU_DESCRIPTOR_HANDLE g_ownFontGpu   {};

auto RendererNameContainsDx12(const char* n) noexcept -> bool {
	if (n == nullptr) {
		return false;
	}
	for (int i = 0; n[i] != '\0'; ++i) {
		const char a = (n[i] >= 'A' && n[i] <= 'Z') ? static_cast<char>(n[i] - 'A' + 'a') : n[i];
		if (a == 'd' && (n[i + 1] == 'x' || n[i + 1] == 'X') && n[i + 2] == '1' && n[i + 3] == '2') {
			return true;
		}
	}
	return false;
}

// ★ v0.17.7：安全探针 —— 上一版盲读布局踩到坏指针，异常把面板+星星一起带停了。
//   现在任何宿主内部指针都先用探针试：坏指针的异常**只会在探针里被抓住**
//   （返回 -1），绝不会再窜到渲染链上。
auto ProbeDevice(void* p) noexcept -> int {
	if (p == nullptr) {
		return -1;
	}
	__try {
		auto* d = static_cast<ID3D12Device*>(p);
		const UINT inc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return (inc >= 1 && inc <= 1024) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

auto ProbeHeap(void* p) noexcept -> int {
	if (p == nullptr) {
		return -1;
	}
	__try {
		auto* h = static_cast<ID3D12DescriptorHeap*>(p);
		const D3D12_DESCRIPTOR_HEAP_DESC d = h->GetDesc();
		if (d.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		    && (d.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0
		    && d.NumDescriptors >= 8) {
			return 1;
		}
		return 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

auto TryBuildOwnFontDx12(void* rendererUserData) noexcept -> ImFont* {
	// imgui_impl_dx12 私有数据布局（1.88~1.91 稳定）：
	//   [0]=ID3D12Device*  [1]=ID3D12GraphicsCommandList*  [2]=RTV堆  [3]=SRV堆
	auto** u = static_cast<void**>(rendererUserData);
	// ★ v0.17.7：设备/堆全部走 SEH 探针，绝不盲读。
	ID3D12Device* dev = nullptr;
	if (ProbeDevice(u[0]) == 1) {
		dev = static_cast<ID3D12Device*>(u[0]);
	}
	if (dev == nullptr) {
		LogInfo("Own font: device probe failed.");
		return nullptr;
	}
	ID3D12DescriptorHeap* srvHeap = nullptr;
	{
		const int kCandidates[] { 3, 4, 5, 2, 6, 7 };
		for (const int idx : kCandidates) {
			if (ProbeHeap(u[idx]) == 1) {
				srvHeap = static_cast<ID3D12DescriptorHeap*>(u[idx]);
				char line[160] {};
				std::snprintf(line, sizeof(line), "Own font: srv heap found at data[%d].", idx);
				LogInfo(line);
				break;
			}
		}
	}
	if (srvHeap == nullptr) {
		LogInfo("Own font: no usable srv heap in backend data.");
		return nullptr;
	}
	const D3D12_DESCRIPTOR_HEAP_DESC hd = srvHeap->GetDesc();
	if (hd.NumDescriptors < 8) {
		LogInfo("Own font: srv heap too small.");
		return nullptr;
	}

	// ① 图集：常用汉字 + ASCII + 全角/标点区，一处不落（面板文案全在这几个区里）。
	static const ImWchar kRanges[] {
		0x0020, 0x00FF,   // ASCII + 拉丁补充
		0x2010, 0x2027,   // 破折号 / 省略号
		0x2460, 0x24FF,   // 带圈数字
		0x3000, 0x30FF,   // CJK 标点
		0x4E00, 0x9FFF,   // 常用汉字
		0xFF00, 0xFFEF,   // 全角形式
		0
	};
	static const char* const kFontFiles[] {
		"msyh.ttc", "msyhl.ttc", "msyhbd.ttc",   // 微软雅黑
		"simhei.ttf", "simsun.ttc", "Deng.ttf",  // 黑体 / 宋体 / 等线
	};
	ImFont* loaded = nullptr;
	for (const char* name : kFontFiles) {
		if (loaded != nullptr) {
			break;
		}
		char path[MAX_PATH] {};
		std::snprintf(path, sizeof(path), "C:\\Windows\\Fonts\\%s", name);
		loaded = g_ownAtlas.AddFontFromFileTTF(path, 18.0f, nullptr, kRanges);
		if (loaded != nullptr) {
			char line[200] {};
			std::snprintf(line, sizeof(line), "Own font: loaded %s", name);
			LogInfo(line);
		}
	}
	if (loaded == nullptr) {
		LogInfo("Own font: no system CJK font found.");
		return nullptr;
	}
	if (!g_ownAtlas.Build()) {
		LogInfo("Own font: atlas build failed.");
		return nullptr;
	}
	unsigned char* pixels = nullptr;
	int w = 0, h = 0;
	g_ownAtlas.GetTexDataAsRGBA32(&pixels, &w, &h);
	if (pixels == nullptr || w <= 0 || h <= 0) {
		LogInfo("Own font: atlas pixels unavailable.");
		return nullptr;
	}

	// ② 建纹理 + 上传（自建命令队列执行拷贝，fence 等完成）。
	const UINT  rowPitch = ((static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
	                        / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	const UINT64 bufSize = static_cast<UINT64>(rowPitch) * static_cast<UINT>(h);
	ID3D12CommandQueue*    q     = nullptr;
	ID3D12CommandAllocator* alloc = nullptr;
	ID3D12GraphicsCommandList* cl = nullptr;
	ID3D12Resource*        upload = nullptr;
	ID3D12Resource*        tex    = nullptr;
	ID3D12Fence*           fence  = nullptr;
	HANDLE                 ev     = nullptr;
	ImFont*                result = nullptr;

	D3D12_COMMAND_QUEUE_DESC qd {};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	do {
		if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) || q == nullptr) {
			LogInfo("Own font: CreateCommandQueue failed.");
			break;
		}
		if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) || alloc == nullptr) {
			LogInfo("Own font: CreateCommandAllocator failed.");
			break;
		}
		// 上传缓冲（手填结构，不引 d3dx12 头）：
		D3D12_HEAP_PROPERTIES hp {};
		hp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC rd {};
		rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Alignment          = 0;
		rd.Width              = bufSize;
		rd.Height             = 1;
		rd.DepthOrArraySize   = 1;
		rd.MipLevels          = 1;
		rd.Format             = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count   = 1;
		rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))) || upload == nullptr) {
			LogInfo("Own font: upload buffer failed.");
			break;
		}
		D3D12_RANGE mapRange { 0, static_cast<SIZE_T>(bufSize) };
		void* mapped = nullptr;
		if (FAILED(upload->Map(0, &mapRange, &mapped)) || mapped == nullptr) {
			LogInfo("Own font: map upload buffer failed.");
			break;
		}
		for (int y = 0; y < h; ++y) {
			std::memcpy(static_cast<unsigned char*>(mapped) + static_cast<std::size_t>(y) * rowPitch,
			            pixels + static_cast<std::size_t>(y) * w * 4,
			            static_cast<std::size_t>(w) * 4);
		}
		D3D12_RANGE written { 0, static_cast<SIZE_T>(bufSize) };
		upload->Unmap(0, &written);

		D3D12_HEAP_PROPERTIES dhp {};
		dhp.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC trd {};
		trd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		trd.Width            = static_cast<UINT64>(w);
		trd.Height           = static_cast<UINT64>(h);
		trd.DepthOrArraySize = 1;
		trd.MipLevels        = 1;
		trd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		trd.SampleDesc.Count = 1;
		trd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		if (FAILED(dev->CreateCommittedResource(&dhp, D3D12_HEAP_FLAG_NONE, &trd,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))) || tex == nullptr) {
			LogInfo("Own font: texture create failed.");
			break;
		}
		if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl))) || cl == nullptr) {
			LogInfo("Own font: command list failed.");
			break;
		}
		D3D12_TEXTURE_COPY_LOCATION dst {};
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		D3D12_TEXTURE_COPY_LOCATION src {};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		src.PlacedFootprint.Footprint.Width  = static_cast<UINT>(w);
		src.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
		src.PlacedFootprint.Footprint.Depth  = 1;
		src.PlacedFootprint.Footprint.RowPitch = rowPitch;
		cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		D3D12_RESOURCE_BARRIER rb {};
		rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		rb.Transition.pResource   = tex;
		rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		cl->ResourceBarrier(1, &rb);
		if (FAILED(cl->Close())) {
			LogInfo("Own font: command list close failed.");
			break;
		}
		ID3D12CommandList* lists[] { cl };
		q->ExecuteCommandLists(1, lists);
		if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || fence == nullptr) {
			LogInfo("Own font: fence create failed.");
			break;
		}
		ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (ev == nullptr) {
			LogInfo("Own font: event create failed.");
			break;
		}
		q->Signal(fence, 1);
		fence->SetEventOnCompletion(1, ev);
		WaitForSingleObject(ev, 3000);
		// ↑ 纹理已在 GPU 上就绪（像素着色器资源状态）。

		// ③ SRV 写进宿主 SRV 堆的**最后一个槽**（字体在 0 号，后面的槽是预留/空位）。
		const UINT slot = hd.NumDescriptors - 1;
		const UINT inc  = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
		cpu.ptr += static_cast<SIZE_T>(slot) * static_cast<SIZE_T>(inc);
		D3D12_SHADER_RESOURCE_VIEW_DESC sd {};
		sd.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
		sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels       = 1;
		dev->CreateShaderResourceView(tex, &sd, cpu);
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
		gpu.ptr += static_cast<UINT64>(slot) * static_cast<UINT64>(inc);
		g_ownFontTex = tex;
		g_ownFontGpu = gpu;
		g_ownAtlas.SetTexID(static_cast<ImTextureID>(gpu.ptr));
		LogInfo("Own font: dx12 texture uploaded and bound in the host srv heap (panel font unified).");
		result = loaded;
	} while (false);

	if (q != nullptr)     { q->Release(); }
	if (alloc != nullptr) { alloc->Release(); }
	if (cl != nullptr)    { cl->Release(); }
	if (upload != nullptr){ upload->Release(); }
	if (fence != nullptr) { fence->Release(); }
	if (ev != nullptr)    { CloseHandle(ev); }
	// tex 保留（被 SRV 引用）；g_ownFontTex 持有，插件卸载时释放。
	return result;
}

// ★ v0.17.7：整个构建流程再套一层 SEH 防护罩 —— 里面任何一步踩雷
//   （包括Build/上传/写描述符堆）都只会被这里接住并退回宿主字体，
//   **永远不可能再把面板和星星一起停用**（上一版的事故就是没这层）。
auto TryBuildOwnFontDx12Seh(void* rendererUserData) noexcept -> ImFont* {
	__try {
		return TryBuildOwnFontDx12(rendererUserData);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		LogInfo("Own font: exception during dx12 build; fallback to host fonts.");
		return nullptr;
	}
}

auto GetOwnPanelFont(ImGuiIO& io) noexcept -> ImFont* {
	if (!g_settings.ownFont) {
		return nullptr;   // ★ v0.17.7：配置开关 own_font = false 直接跳过
	}
	if (g_ownFont != nullptr) {
		return g_ownFont;
	}
	if (g_ownFontTried) {
		return nullptr;   // 试过且失败：永远走宿主字体老路，不再折腾
	}
	g_ownFontTried = true;
	if (!RendererNameContainsDx12(io.BackendRendererName) || io.BackendRendererUserData == nullptr) {
		char line[220] {};
		std::snprintf(line, sizeof(line),
			"Own font: renderer '%s' has no dx12 user data; keep host fonts.",
			(io.BackendRendererName != nullptr) ? io.BackendRendererName : "<null>");
		LogInfo(line);
		return nullptr;
	}
	return TryBuildOwnFontDx12Seh(io.BackendRendererUserData);
}

// ─────────── ★ v0.18.0：早注册字体（真正的一劳永逸）───────────
//  飘字插件的 12 块字体能进共享图集并完美显示，靠的就是**插件加载早于游戏
//  第一次构建字体库**。我们照做：第一次宿主回调（frame=0、图集未烘焙）就把
//  微软雅黑注册进 c->IO.Fonts —— 之后游戏建库时自然带上我们这块，
//  面板从此只有一种字型，不再需要任何 DX12 纹理黑魔法。
//  安全性：只在"图集未烘焙"时加（晚了就放弃），整个过程 SEH 包裹，
//  own_font=false 可关；失败了就退回 dx12 自建/宿主字体两条老路。
static ImFont* g_earlyFont     = nullptr;
static bool    g_earlyFontTried = false;

auto TryAddEarlyFont(ImGuiContext* c) noexcept -> void {
	if (g_earlyFontTried || !g_settings.ownFont) {
		return;
	}
	g_earlyFontTried = true;
	if (c == nullptr || c->IO.Fonts == nullptr) {
		return;
	}
	// 图集已经烘焙过（纹理已上传）= 现在加也赶不上 ⇒ 不折腾（走后面的老路）。
	if (c->IO.Fonts->TexPixelsAlpha8 != nullptr || c->IO.Fonts->TexPixelsRGBA32 != nullptr) {
		LogInfo("Early font: host atlas already built; skip (keep host fonts).");
		return;
	}
	__try {
		static const ImWchar kRanges[] {
			0x0020, 0x00FF,   // ASCII + 拉丁补充
			0x2010, 0x2027,   // 破折号 / 省略号
			0x2460, 0x24FF,   // 带圈数字
			0x3000, 0x30FF,   // CJK 标点
			0x4E00, 0x9FFF,   // 常用汉字
			0xFF00, 0xFFEF,   // 全角形式
			0
		};
		static const char* const kFontFiles[] {
			"msyh.ttc", "msyhl.ttc", "msyhbd.ttc",   // 微软雅黑
			"simhei.ttf", "simsun.ttc", "Deng.ttf",  // 黑体 / 宋体 / 等线
		};
		for (const char* name : kFontFiles) {
			char path[MAX_PATH] {};
			std::snprintf(path, sizeof(path), "C:\\Windows\\Fonts\\%s", name);
			ImFont* f = c->IO.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, kRanges);
			if (f != nullptr) {
				g_earlyFont = f;
				char line[200] {};
				std::snprintf(line, sizeof(line),
					"Early font: %s registered into the host atlas BEFORE first build "
					"(the panel will use this single-source font).", name);
				LogInfo(line);
				return;
			}
		}
		LogInfo("Early font: no system CJK font found.");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_earlyFont = nullptr;
		LogInfo("Early font: exception during registration; fallback to host fonts.");
	}
}

// ── 面板本体 ──
// 注意：这个函数里不能出现需要析构的 C++ 对象，否则外层 __try 会被
// MSVC 拒绝（C2712）。所以一律用固定缓冲 + ImGui 自己的类型。
auto DrawPanel(ImGuiContext* c) noexcept -> void {
	g_drawStage = 1;
	ImGui::SetCurrentContext(c);
	ImGuiIO& io = ImGui::GetIO();

	// ★ v0.18.3：把钩子线程记下的滚轮格数喂给 ImGui。
	//   ⚠️ 实测教训（v0.18.2）：这个版本里滚轮在 **NewFrame 阶段的 UpdateMouseWheel()**
	//   统一处理，而我们的回调跑在帧中间 —— 直接写 io.MouseWheel 是马后炮（当帧已处理、
	//   下帧开头又被清零）。必须走事件队列：AddMouseWheelEvent 排队，下一帧 NewFrame
	//   开头统一应用，正好赶上下一次 UpdateMouseWheel。
	const int wheelSteps = g_panelWheelSteps.exchange(0, std::memory_order_relaxed);
	if (wheelSteps != 0) {
		io.AddMouseWheelEvent(0.0f, static_cast<float>(wheelSteps));
	}

	// 输入诊断：每 ~2 秒一行。看这一行就能知道鼠标到底进没进来、
	// 宿主自己有没有在喂鼠标（hostMouse 和我们的写法不一致 = 它在喂）。
	if ((g_inputDiagTick++ % 120) == 0) {
		char hovered[40] {};
		char wouldHover[40] {};
		ReadWindowName(c->HoveredWindow, hovered, sizeof(hovered));
		ReadWindowName(c->HoveredWindowBeforeClear, wouldHover, sizeof(wouldHover));
		char line[430] {};
		std::snprintf(line, sizeof(line),
			"Overlay panel [input]: flags=0x%X noMouse=%s accepting=%s "
			"hostMouse=(%.0f,%.0f) ourMouse=(%.0f,%.0f) left=%s hovered='%s' "
			"hostWouldHover='%s' forcedHover=%u",
			static_cast<unsigned>(c->IO.ConfigFlags),
			(c->IO.ConfigFlags & ImGuiConfigFlags_NoMouse) ? "yes" : "no",
			c->IO.AppAcceptingEvents ? "yes" : "no",
			static_cast<double>(c->IO.MousePos.x), static_cast<double>(c->IO.MousePos.y),
			static_cast<double>(g_mousePos.x), static_cast<double>(g_mousePos.y),
			g_mouseDown[0] ? "down" : "up",
			hovered, wouldHover,
			static_cast<unsigned>(g_forcedHoverFrames.load(std::memory_order_relaxed)));
		LogInfo(line);
	}

	// 鼠标归我们管：清 NoMouse + 每帧把 Win32 的光标状态写进宿主的 ImGuiIO。
	OwnInput(c);
	const bool leftDown    = g_mouseDown[0];
	const bool leftPressed = leftDown && !g_mouseDownPrev[0];
	if (!leftDown) {
		g_dragActive = false;   // 松手 → 拖动结束
	}

	g_drawStage = 2;
	// ★ v0.18.0：字体优先级——
	//   ① 早注册的雅黑（游戏建库前塞进去的，单一来源、全覆盖）：仍在图集里才用；
	//   ② dx12 自建纹理路线（v0.17.7，探针+防护罩齐全）；
	//   ③ 宿主全覆盖合并字体（可读保底）。
	ImFont* pushFont = nullptr;
	if (g_earlyFont != nullptr) {
		bool stillThere = false;
		for (int i = 0; i < c->IO.Fonts->Fonts.Size; ++i) {
			if (c->IO.Fonts->Fonts[i] == g_earlyFont) {
				stillThere = true;
				break;
			}
		}
		if (stillThere) {
			pushFont = g_earlyFont;
		} else {
			g_earlyFont = nullptr;   // 图集被重建过，指针失效
		}
	}
	if (pushFont == nullptr) {
		pushFont = GetOwnPanelFont(io);
	}
	if (pushFont == nullptr) {
		pushFont = PickPanelFont(c);   // 兜底：全覆盖合并字体（可读）
	}
	if (pushFont != nullptr) {
		ImGui::PushFont(pushFont);
	}

	// ── 窗口位置：拖动由我们自己实现 ──
	// （ImGui 原生拖动靠 io.MouseDelta，而直写鼠标时它恒为 0，见文件头说明）
	ImGuiWindow* prev = LastWindowAlive(c);
	if (g_dragActive) {
		const float width = (prev != nullptr) ? prev->Size.x : 470.0f;
		const float minX  = -(width - 80.0f);
		const float maxX  = io.DisplaySize.x - 80.0f;
		const float maxY  = io.DisplaySize.y - 24.0f;
		float nx = g_mousePos.x - g_dragGrabDX;
		float ny = g_mousePos.y - g_dragGrabDY;
		nx = (nx < minX) ? minX : ((nx > maxX) ? maxX : nx);   // 标题栏不许被拖出屏幕
		ny = (ny < 0.0f) ? 0.0f : ((ny > maxY) ? maxY : ny);
		g_dragPos     = ImVec2(nx, ny);
		g_dragHavePos = true;
	} else if (prev != nullptr && g_rawValid && leftPressed) {
		// 在标题栏按下（右上角关闭按钮那块除外）→ 开始拖
		const ImVec2 p = prev->Pos;
		const ImVec2 s = prev->Size;
		const float  titleHeight = ImGui::GetFrameHeight();
		const bool inTitleBar = (g_mousePos.x >= p.x && g_mousePos.x <= p.x + s.x - 26.0f
		                      && g_mousePos.y >= p.y && g_mousePos.y <= p.y + titleHeight);
		if (inTitleBar) {
			g_dragActive = true;
			g_dragGrabDX = g_mousePos.x - p.x;
			g_dragGrabDY = g_mousePos.y - p.y;
		}
	}

	const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowSize(ImVec2(470.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (g_dragHavePos) {
		ImGui::SetNextWindowPos(g_dragPos, ImGuiCond_Always);
	} else {
		ImGui::SetNextWindowPos(centre, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	}
	ForceHover(c, prev);

	g_drawStage = 3;
	bool open = g_open.load(std::memory_order_relaxed);
	const bool visible = ImGui::Begin(Tr("掉落物地图标记", "Loot Map"), &open,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

	g_drawStage = 4;
	// 一直压在最上层：地图插件的覆盖层很可能每帧都把自己挪到最前，
	// 我们不主动抬一次的话窗口会被它盖住。这个调用只调整绘制顺序，
	// **不会抢焦点**，也就不会把键盘输入从游戏那边截走。
	// ★ v0.17.2：调色板弹窗打开时**不要**抢最前 —— 之前这里每帧都把面板
	//   挪到显示层最前，把弹出来的调色板压在了面板底下（用户报"色板被
	//   UI 面板覆盖"）。有弹窗时弹窗本来就画在面板之上，跳过这一句即可。
	if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
	}
	g_lastWindow = ImGui::GetCurrentWindow();
	ForceHover(c, g_lastWindow);

	// ★ v0.14.0：面板"算不算挡着地图"一律以 g_open 为准 —— 面板关掉后立刻
	//   不再影响星标绘制。v0.12.8 就是这里没弄干净，才让用户以为
	//   "调完星星大小星就不见了"（其实是被面板那一块吃掉）。
	g_panelVisible.store(visible && open, std::memory_order_relaxed);
	if (visible) {
		const ImVec2 wp = ImGui::GetWindowPos();
		const ImVec2 ws = ImGui::GetWindowSize();
		g_panelX0 = wp.x;
		g_panelY0 = wp.y;
		g_panelX1 = wp.x + ws.x;
		g_panelY1 = wp.y + ws.y;

		// 面板挡鼠标（WH_MOUSE 钩子，详见文件头）：只在"面板开着 + 光标在
		// 面板矩形内"时吞点击，其余一律放行。
		g_mouseInPanel.store(g_rawValid
			&& g_mousePos.x >= g_panelX0 && g_mousePos.x <= g_panelX1
			&& g_mousePos.y >= g_panelY0 && g_mousePos.y <= g_panelY1, std::memory_order_relaxed);
		UpdatePanelScreenRect(c, g_gameHwnd);
		EnsureGameMouseHook();
		EnsurePanelWheelHookThread();   // ★ v0.18.1：面板滚轮（低级钩子线程）
	} else {
		g_mouseInPanel.store(false, std::memory_order_relaxed);
		UpdatePanelScreenRect(c, nullptr);
		ReleaseGameMouseHook();
	}

	if (visible) {
		g_drawStage = 5;
		// 一行颜色按钮组：点一下就把这一行的星改成那个颜色。
		// 星是我们自己画的，所以颜色一定生效（不像引擎的标记只认色号）。
		auto colourRow = [&](float rgb[3]) noexcept -> bool {
			bool changed = false;
			for (int p = 0; p < kPresetCount; ++p) {
				if (p > 0) {
					ImGui::SameLine(0.0f, 2.0f);
				}
				ImGui::PushID(1000 + p);
				const ImVec4 pc(kPresets[p].r / 255.0f, kPresets[p].g / 255.0f,
				                kPresets[p].b / 255.0f, 1.0f);
				const bool sel = (Rgb255(rgb[0]) == kPresets[p].r
				               && Rgb255(rgb[1]) == kPresets[p].g
				               && Rgb255(rgb[2]) == kPresets[p].b);
				if (sel) {
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
				}
				if (ImGui::ColorButton("##c", pc,
				        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				        ImVec2(19.0f, 19.0f))) {
					rgb[0] = (kPresets[p].r + 0.25f) / 255.0f;
					rgb[1] = (kPresets[p].g + 0.25f) / 255.0f;
					rgb[2] = (kPresets[p].b + 0.25f) / 255.0f;
					changed = true;
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("R%d G%d B%d", kPresets[p].r, kPresets[p].g, kPresets[p].b);
				}
				if (sel) {
					ImGui::PopStyleVar();
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}
			// ★ v0.17.0：末尾补一个"自定义色"按钮 —— 点开是完整调色板，
			//   14 个预设不够用时任意选色（ID 继承调用方的行作用域，天然不冲突）。
			ImGui::SameLine(0.0f, 4.0f);
			if (ImGui::ColorEdit3("##custom", rgb,
			        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel
			        | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoOptions)) {
				changed = true;
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(Tr("自定义颜色", "Custom colour"));
			}
			return changed;
		};

		// ★ v0.17.0：面板照 MapSense 重做 —— 深色可折叠分区条 + 极简内容。
		//   只保留：总开关 / 品质（暗金、套装）/ 规则（珠宝匠·工匠）/ 星星（样式·大小·描边）。
		//   其余设置（偏移、驻留、挡鼠标、雷达倍率…）不再出现在面板上，
		//   但配置键全部保留，改配置文件依然生效。
		ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.16f, 0.13f, 0.10f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.19f, 0.14f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.28f, 0.22f, 0.16f, 1.00f));

		// ── 总开关：一个开关控制地图上所有标记的显示/隐藏 ──
		g_drawStage = 6;
		bool stars = g_settings.stars;
		if (ImGui::Checkbox(Tr("显示标记（总开关）", "Show markers (master switch)"), &stars)) {
			g_settings.stars = stars;
			SaveSettingsNow();
		}
		ImGui::Spacing();

		// ── 品质：面板只放「暗金」「套装」两行 ──
		//   ★ v0.17.1：照 MapSense 改成**上下样式** —— 勾选+名字一行，
		//   色板独占下一整行（横排塞不下的截断问题就此根治）。
		//   其余六类槽位/配置键保留（接口在），改配置文件依然生效。
		g_drawStage = 7;
		if (ImGui::CollapsingHeader(Tr("品质", "Qualities"), ImGuiTreeNodeFlags_DefaultOpen)) {
			g_drawStage = 8;
			for (const int i : { kSlotUnique, kSlotSet }) {
				g_drawStage = 9;
				ImGui::PushID(100 + i);
				bool show = g_settings.show[i];
				if (ImGui::Checkbox("##show", &show)) {
					g_settings.show[i] = show;
					SaveSettingsNow();
				}
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(g_cjkFont != nullptr ? NativePanel::kShortLabel[i]
				                                           : kShortLabelEn[i]);
				if (colourRow(g_settings.rgb[i])) {
					g_settings.rgbOn[i] = true;
					SaveSettingsNow();
				}
				ImGui::Spacing();
				ImGui::PopID();
			}
		}

		// ── 规则：只放已接入的 rule0（rule1~3 内部保留、配置文件可调）──
		//   ★ v0.17.1：同样改上下样式，色板独占一整行（不再截断）。
		g_drawStage = 12;
		if (ImGui::CollapsingHeader(Tr("规则", "Rules"), ImGuiTreeNodeFlags_DefaultOpen)) {
			g_drawStage = 13;
			ImGui::PushID(3000);
			bool on = g_settings.ruleOn[0];
			if (ImGui::Checkbox("##on", &on)) {
				g_settings.ruleOn[0] = on;
				SaveSettingsNow();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			const char* name = (g_settings.ruleName[0][0] != '\0')
				? g_settings.ruleName[0]
				: (g_cjkFont != nullptr ? kRuleRows[0].label : kRuleRows[0].labelEn);
			ImGui::TextUnformatted(name);
			if (colourRow(g_settings.ruleRgb[0])) {
				SaveSettingsNow();
			}
			ImGui::PopID();
		}

		// ── 星星：样式 / 大小（12~36）/ 描边 ──
		//   ★ v0.17.0：只留这三样；微调、驻留、挡鼠标、变淡、兜底、雷达倍率
		//   全部移出面板（配置键保留，改配置文件依然生效）。
		g_drawStage = 14;
		if (ImGui::CollapsingHeader(Tr("星星", "Stars"), ImGuiTreeNodeFlags_DefaultOpen)) {
			for (int i = 0; i < 2; ++i) {
				const char* label = (i == 0) ? Tr("简约", "Flat") : Tr("暗黑3", "D3-like");
				const bool active = (g_settings.starStyle == i);
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.36f, 0.16f, 1.0f));
				}
				if (ImGui::Button(label, ImVec2(92.0f, 0.0f))) {
					g_settings.starStyle = i;
					SaveSettingsNow();
				}
				if (active) {
					ImGui::PopStyleColor();
				}
				if (i == 0) {
					ImGui::SameLine();
				}
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("样式", "Style"));

			// ★ v0.17.4：数字恢复半角正常显示（用户要求；v0.17.3 起面板用自己的
			//   雅黑图集，数字/中文本来就是一个源，不再需要全角绕路）。
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			ImGui::SliderFloat("##size", &g_settings.starSize, 12.0f, 36.0f, "%.0f px");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				SaveSettingsNow();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("大小", "Size"));

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			ImGui::SliderFloat("##outline", &g_settings.starOutline, 0.0f, 20.0f, "%.0f %%");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				SaveSettingsNow();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("描边", "Outline"));

			// ★ v0.18.4：位置微调（用户实测星有一点点偏移；对不同分辨率/缩放
			//   各对一次即可，改完自动存盘，之后永远生效）。
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			ImGui::SliderInt("##offx", &g_settings.starOffsetX, -50, 50, "%d px");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				SaveSettingsNow();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("左右微调", "X offset"));

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			ImGui::SliderInt("##offy", &g_settings.starOffsetY, -50, 50, "%d px");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				SaveSettingsNow();
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("上下微调", "Y offset"));
		}

		g_drawStage = 15;
		ImGui::PopStyleColor(3);
		if (ImGui::Button(Tr("关闭", "Close"), ImVec2(90.0f, 0.0f))) {
			g_open.store(false, std::memory_order_relaxed);
		}
	}

	g_drawStage = 17;
	ImGui::End();

	g_drawStage = 18;
	if (pushFont != nullptr) {
		ImGui::PopFont();
	}
	if (!open) {
		g_open.store(false, std::memory_order_relaxed);
	}
	g_drawStage = 0;
}


// ───────── ★ v0.12.0：星标绘制（叠加层的前台图层，画在所有窗口之上） ─────────
//  坐标来源：HookBlobIconPrep 记下的引擎标记屏幕偏移（point+0/+4）。
//  变换假设：屏幕位置 = 显示区中心 + 记录的偏移（+ star_offset_x/y 微调）。
//  为什么扇形三角形能画"凹"的五角星：星形关于中心是"星状凸"的，
//  从中心向 10 个顶点连三角形正好填满，不会越界到凹处之外。

auto DrawStarShape(ImDrawList* dl, float cx, float cy, float radius, ImU32 col,
                   ImU32 outlineCol, float outlineW) noexcept -> void {
	ImVec2 v[10];
	for (int k = 0; k < 5; ++k) {
		const float ao = -1.5707963f + static_cast<float>(k) * 1.2566371f;   // -90° + k·72°
		const float ai = ao + 0.6283185f;                                    // +36°（内角）
		v[k * 2]     = ImVec2(cx + radius * std::cos(ao), cy + radius * std::sin(ao));
		v[k * 2 + 1] = ImVec2(cx + radius * 0.45f * std::cos(ai), cy + radius * 0.45f * std::sin(ai));
	}
	//  ★ v0.14.4：一次画成整块（凹多边形）。
	//   以前是从中心拉 10 个三角形拼 —— 相邻三角形共边，而 ImGui 填充开了抗锯齿，
	//   同一条边会被混合两次 ⇒ 星上出现从中心射向五个角的"分界线"（用户截图里那几条线）。
	//   AddConcavePolyFilled 一次成型，没有共边、没有接缝。
	dl->AddConcavePolyFilled(v, 10, col);
	//  ★ v0.14.4：描边。只沿外圈画一圈闭合线（AddPolyline 不会画出内部接缝）。
	if (outlineW > 0.0f && (outlineCol & 0xFF000000u) != 0u) {
		dl->AddPolyline(v, 10, outlineCol, ImDrawFlags_Closed, outlineW);
	}
}

//  描边颜色：按填充色的亮度自动选黑或白 —— 保证任何颜色都看得清。
auto StarOutlineColour(ImU32 fill) noexcept -> ImU32 {
	const float r = static_cast<float>((fill >> IM_COL32_R_SHIFT) & 0xFFu);
	const float g = static_cast<float>((fill >> IM_COL32_G_SHIFT) & 0xFFu);
	const float b = static_cast<float>((fill >> IM_COL32_B_SHIFT) & 0xFFu);
	const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
	if (lum > 140.0f) {
		return IM_COL32(0, 0, 0, 235);        // 亮底 → 黑边
	}
	return IM_COL32(255, 255, 255, 235);      // 暗底 → 白边
}

// ★ v0.15.1：D3 星的色带。不是"单纯变暗"——单纯变暗会把黄色变成橄榄绿（用户截图那个）。
//   D3 的实际观感是：边缘深**橙棕**、中心**亮黄**。所以深端压绿/压蓝（往橙走），
//   亮端往亮黄白走；基色的"色相族"仍保留（绿色物品的深端还是深绿，只是偏暖）。
auto StarRampColour(ImU32 col, float t) noexcept -> ImU32 {
	// t: 0 = 最外（深） → 1 = 最内（亮）
	const float r = static_cast<float>((col >> IM_COL32_R_SHIFT) & 0xFFu);
	const float g = static_cast<float>((col >> IM_COL32_G_SHIFT) & 0xFFu);
	const float b = static_cast<float>((col >> IM_COL32_B_SHIFT) & 0xFFu);
	const float a = static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFFu);
	// 深端：橙棕
	const float dr = r * 0.55f;
	const float dg = g * 0.34f;
	const float db = b * 0.18f;
	// 亮端：往亮黄白走
	const float ur = r + (255.0f - r) * 0.35f;
	const float ug = g + (255.0f - g) * 0.30f;
	const float ub = b + (255.0f - b) * 0.10f + 40.0f;
	auto cl = [](float v) noexcept -> float {
		return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
	};
	const float k = t * t * (3.0f - 2.0f * t);   // smoothstep：外圈更深得更久
	return IM_COL32(static_cast<int>(cl(dr + (ur - dr) * k)),
	                static_cast<int>(cl(dg + (ug - dg) * k)),
	                static_cast<int>(cl(db + (ub - db) * k)),
	                static_cast<int>(a));
}

// ★ v0.15.0：暗黑3 风格的立体星（用户给的参考图：D3 地图上的金色任务星）。
//   ★ v0.15.2 按用户反馈定稿：**几何回到 v0.15.0 的胖乎乎样子**（内谷 0.50R + 边缘外鼓 10%），
//     色带保留 v0.15.1 的"深橙棕→亮黄"（单纯变暗会发绿）与 20 层平滑渐变。
//   ② 色带从"乘系数变暗"改成 StarRampColour（深端往橙棕走，不然黄色会变橄榄绿）；
//   ③ 层数 10→20、每层缩 4.2%（一圈圈的台阶感肉眼基本消失）。
//   保留：轮廓细分、分层渐变、最外圈深色描边。颜色全部基于用户给每一类选的 rgb_xxx。
auto DrawStarShapeD3(ImDrawList* dl, float cx, float cy, float radius, ImU32 col,
                     ImU32 outlineCol, float outlineW) noexcept -> void {
	ImVec2 v[10];
	for (int k = 0; k < 5; ++k) {
		const float ao = -1.5707963f + static_cast<float>(k) * 1.2566371f;
		const float ai = ao + 0.6283185f;
		v[k * 2]     = ImVec2(cx + radius * std::cos(ao), cy + radius * std::sin(ao));
		v[k * 2 + 1] = ImVec2(cx + radius * 0.50f * std::cos(ai), cy + radius * 0.50f * std::sin(ai));
	}
	constexpr int   kSeg    = 8;
	constexpr int   kPts    = 10 * kSeg;
	constexpr int   kLayers = 20;
	constexpr float kPi     = 3.14159265f;
	ImVec2 pts[kPts];
	{
		int n = 0;
		for (int i = 0; i < 10; ++i) {
			const ImVec2& a = v[i];
			const ImVec2& b = v[(i + 1) % 10];
			for (int s = 0; s < kSeg; ++s) {
				const float t = static_cast<float>(s) / static_cast<float>(kSeg);
				float px = a.x + (b.x - a.x) * t;
				float py = a.y + (b.y - a.y) * t;
				const float dx = px - cx;
				const float dy = py - cy;
				const float d  = std::sqrt(dx * dx + dy * dy);
				if (d > 0.0001f) {
					const float k = 1.0f + 0.10f * std::sin(kPi * t);   // 外鼓 10%：胖乎乎的圆臂（v0.15.0 的样子）
					px = cx + dx * k;
					py = cy + dy * k;
				}
				pts[n] = ImVec2(px, py);
				++n;
			}
		}
	}
	for (int L = 0; L < kLayers; ++L) {
		const float scale = 1.0f - 0.042f * static_cast<float>(L);   // 20 层、每层缩 4.2%：台阶感肉眼基本看不见
		ImVec2 lp[kPts];
		for (int i = 0; i < kPts; ++i) {
			lp[i].x = cx + (pts[i].x - cx) * scale;
			lp[i].y = cy + (pts[i].y - cy) * scale;
		}
		dl->AddConcavePolyFilled(lp, kPts,
			StarRampColour(col, static_cast<float>(L) / static_cast<float>(kLayers - 1)));
	}
	if (outlineW > 0.0f && (outlineCol & 0xFF000000u) != 0u) {
		dl->AddPolyline(pts, kPts, outlineCol, ImDrawFlags_Closed, outlineW);
	}
}

auto StarColour(int slot) noexcept -> ImU32 {
	// ★ v0.16.0：规则虚拟槽位（kSlotCount + 规则号）—— 颜色走 ruleRgb。
	if (slot >= kSlotCount && slot < kSlotCount + kRuleCount) {
		const int rIdx = slot - kSlotCount;
		return IM_COL32(Rgb255(g_settings.ruleRgb[rIdx][0]),
		                Rgb255(g_settings.ruleRgb[rIdx][1]),
		                Rgb255(g_settings.ruleRgb[rIdx][2]), 255);
	}
	float r = (slot == kSlotUnique) ? 255.0f / 255.0f : 0.0f;
	float g = (slot == kSlotUnique) ? 215.0f / 255.0f : 1.0f;
	float b = (slot == kSlotUnique) ?   0.0f / 255.0f : 0.0f;
	if (slot >= 0 && slot < kSlotCount && g_settings.rgbOn[slot]) {
		r = g_settings.rgb[slot][0];
		g = g_settings.rgb[slot][1];
		b = g_settings.rgb[slot][2];
	}
	return IM_COL32(Rgb255(r), Rgb255(g), Rgb255(b), 255);
}

// ── ★ v0.14.2：星标观测缓冲（只渲染线程用，纯 POD、不带析构）──
//   只存引擎这一帧亲口报出来的东西：坐标 + 品质槽位 + 判定可信度。
//   ⚠️ 实测结论：这个坐标**不能**当“这件物品是谁”的钥匙 —— 同一件物品的坐标
//   会随帧漂移（见上面 StarMemory 的注释）。它只能当“这一帧要画在哪儿”用。
struct StarObs {
	float rx;     // 引擎给的原始坐标（只用于当帧绘制；不再当身份钥匙）
	float ry;
	int   slot;
	int   conf;   // 2 = 品质直接读出；1 = 引擎色号兑底
	const void* unit;   // ★ v0.14.6：这件物品的单位指针（当帧去重靠它）
};
// ★ v0.14.2：驻留时间 = 引擎停止报物品之后，快照还留多久（秒）。
//   引擎不是每帧都在报（它只在自己重绘地图时报；日志里见过最长约 8 秒空档），
//   所以留一会儿，星星才不会一闪一闪。配置值会被钳到 [0, kStarHoldSecMax]。
//   注意：驻留只是“让上一帧那几颗星多显示一会儿”，**不会**因此多出任何一颗星。

//  读“相机偏移”：引擎在渲染上下文里放着的 (原点 - 摄像机) * 缩放。
//  v0.12.7 的活体反汇编确认了这四个字段的位置（0xc0 / 0xc4 / 0xc8 / 0xd0）。
//  它和我们手上的地图坐标相加，就是这颗星**当帧**该在的屏幕位置：
//      x_屏幕 = 地图x + offX        （和 StarScreenPos 完全是同一个公式）
//  每次画都现算一遍，不做预测 ⇒ 不可能漂、不可能错位。
auto ReadCameraOffset(const void* obj, float zoom, float& offX, float& offY) noexcept -> bool {
	offX = 0.0f;
	offY = 0.0f;
	if (obj == nullptr || zoom <= 0.0f || !AddressReadable(obj, sizeof(void*))) {
		return false;
	}
	const void* ctx = *static_cast<const void* const*>(obj);
	if (ctx == nullptr || !AddressReadable(ctx, 0xd4)) {
		return false;
	}
	const auto* f = static_cast<const std::int32_t*>(ctx);
	const float orgX = static_cast<float>(f[0xd0 / 4]);
	const float camX = static_cast<float>(f[0xc4 / 4]);
	const float orgY = static_cast<float>(f[0xc0 / 4]);
	const float camY = static_cast<float>(f[0xc8 / 4]);
	offX = (orgX - camX) * zoom;
	offY = (orgY - camY) * zoom;
	return true;
}

//  ★ v0.14.2：星标“快照 + 驻留”—— 第五版。前四版全翻车，教训就是一句话：
//  **同一件物品的“原始坐标”不是恒定值**（日志实测：同一件东西几十秒里漂过上百个
//  不同坐标），所以任何“凭坐标认物品”的写法都会把同一件东西当成新星、每帧多记
//  一颗 —— 这就是“满地幽灵星”的唯一根因，跟用屏幕坐标还是地图坐标无关：
//    · v0.13.0 存屏幕坐标 + 配对求“整体位移中位数” → 位置全错（那是猜）；
//    · v0.14.0 存屏幕坐标 + 相机偏移做差分预测 → 日志实锤 sticky 一步 2→4；
//    · v0.14.1 存地图坐标当钥匙 + 命中计数 → 日志实锤 sticky 涨到 512、画出 117 颗
//      （当帧 buffered 始终 = 1！）。
//  这一版**把“认物品”这件事整个删掉**，不给它出错的机会：
//    · 引擎这一帧报了几件，当前星表就**整表替换**成这几件（快照）；
//      ⇒ 表的条数上限 = 单帧最多报过的件数，**结构上不可能越攒越多**；
//    · 引擎偶尔几秒不报 → 快照按 star_persist_sec 先留着，超时才清空（不闪）；
//    · 画的时候每帧用**当帧**相机现算屏幕位置 ⇒ 不会错位。
//  一句话：星表永远只镜像“引擎最后一次报的那几件”，绝不自己长出新条目。
auto DrawStars(ImGuiContext* c) noexcept -> void {
	const int n = g_starCount.load(std::memory_order_acquire);
	g_starCount.store(0, std::memory_order_relaxed);

	ImGui::SetCurrentContext(c);
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const float hw     = c->IO.DisplaySize.x * 0.5f;
	const float hh     = c->IO.DisplaySize.y * 0.5f;
	const float radius = g_settings.starSize;
	const float dx     = static_cast<float>(g_settings.starOffsetX);
	const float dy     = static_cast<float>(g_settings.starOffsetY);
	const float maxX   = c->IO.DisplaySize.x + 40.0f;
	const float maxY   = c->IO.DisplaySize.y + 40.0f;
	const int   mode   = g_settings.starCoordMode;
	const std::int64_t nowMs = static_cast<std::int64_t>(::GetTickCount64());
	if (n > 0) {
		g_lastObservationMs.store(nowMs, std::memory_order_relaxed);
	}
	if (dl == nullptr) {
		g_starsLive.store(0, std::memory_order_relaxed);
		g_starsLiveSet.store(0, std::memory_order_relaxed);
		g_starsLiveUnique.store(0, std::memory_order_relaxed);
		return;
	}

	// 相机（当帧读一次；每颗星的屏幕位置都用它现算，绝不做预测）
	const void* camObj  = g_lastCtx.load(std::memory_order_relaxed);
	const float camZoom = g_lastZoom.load(std::memory_order_relaxed);
	float camOffX = 0.0f;
	float camOffY = 0.0f;
	const bool camOk = ReadCameraOffset(camObj, camZoom, camOffX, camOffY);

	// ① 本帧观测：引擎这一帧亲口报的位置（地图坐标）——最准，直接当钥匙用。
	//   ★ v0.14.6：**先按"物品单位指针"去重** —— 实测引擎对同一件物品一帧里会反复
	//   来取色（地上三四件东西能让 bu 冲到 40），不去重就会把一件东西画成一堆星
	//   （用户截图里那一簇绿星）。单位指针是精确身份，不需要猜坐标容差。
	//   指针拿不到的（老路径/兜底）再退回按坐标近似去重。
	const int rawObsCount = n;
	static StarObs s_obs[kStarMax];
	int obsCount = 0;
	for (int i = 0; i < n; ++i) {
		const StarPoint& sp = g_starPoints[i];
		bool dup = false;
		if (sp.unit != nullptr) {
			for (int k = 0; k < obsCount; ++k) {
				if (s_obs[k].unit == sp.unit) {
					dup = true;
					break;
				}
			}
		}
		if (!dup) {
			for (int k = 0; k < obsCount; ++k) {
				const float gx = s_obs[k].rx - sp.x;
				const float gy = s_obs[k].ry - sp.y;
				if (gx > -kStarMergeUnits && gx < kStarMergeUnits
				    && gy > -kStarMergeUnits && gy < kStarMergeUnits) {
					dup = true;
					break;
				}
			}
		}
		if (dup || obsCount >= kStarMax) {
			continue;
		}
		s_obs[obsCount].rx   = sp.x;
		s_obs[obsCount].ry   = sp.y;
		s_obs[obsCount].slot = sp.slot;
		s_obs[obsCount].conf = sp.conf;
		s_obs[obsCount].unit = sp.unit;
		++obsCount;
	}

	// ② 快照：引擎这一帧报了几件，当前星表就**整表替换**成这几件。
	//    没有匹配、没有预测、没有累计 ⇒ 结构上不可能多出一颗“幽灵星”。
	const std::int64_t lastObsMs = g_lastObservationMs.load(std::memory_order_relaxed);
	float holdSec = static_cast<float>(g_settings.starPersistSec);
	if (holdSec < 0.0f) {
		holdSec = 0.0f;
	} else if (holdSec > kStarHoldSecMax) {
		holdSec = kStarHoldSecMax;
	}
	const std::int64_t holdMs = static_cast<std::int64_t>(holdSec * 1000.0f);

	if (!g_settings.stars) {
		g_stickyCount = 0;      // 功能关：一颗都不留
	} else if (obsCount > 0) {
		// 引擎这一帧亲口报了 obsCount 件 → 整表替换（就地写，不分配内存）。
		int keep = obsCount;
		if (keep > kStickyMax) {
			keep = kStickyMax;
		}
		for (int i = 0; i < keep; ++i) {
			g_sticky[i].mx   = s_obs[i].rx;
			g_sticky[i].my   = s_obs[i].ry;
			g_sticky[i].slot = s_obs[i].slot;
			g_sticky[i].conf = s_obs[i].conf;
		}
		g_stickyCount    = keep;
		g_starSnapshotMs = nowMs;
	} else if (lastObsMs <= 0 || (nowMs - lastObsMs) > holdMs) {
		// 引擎已经超过“驻留时间”没再报物品（地图关了 / 换场景 / 回城）→ 清空快照。
		g_stickyCount = 0;
	}

	// ③ 画：快照里的星，位置每帧用当帧相机现算。
	int drawn       = 0;
	int setDrawn    = 0;
	int uniqueDrawn = 0;
	float firstSx   = 0.0f;
	float firstSy   = 0.0f;
	const bool panelOn = g_panelVisible.load(std::memory_order_relaxed);
	for (int k = 0; k < g_stickyCount; ++k) {
		const StarMemory& m = g_sticky[k];
		float sx = 0.0f;
		float sy = 0.0f;
		switch (mode) {
		case 0:    sx = m.mx + dx;        sy = m.my + dy;        break;
		case 2:    sx = hw + m.mx + dx;   sy = hh + m.my + dy;   break;
		default:
			if (!camOk) {
				continue;   // 读不到相机就一颗都不画：宁可少画，绝不错位
			}
			sx = m.mx + camOffX + dx;
			sy = m.my + camOffY + dy;
			break;
		}
		if (sx < -40.0f || sy < -40.0f || sx > maxX || sy > maxY) {
			continue;
		}
		ImU32 col = StarColour(m.slot);
		// ★ v0.14.3：面板底下"变淡"改成开关，**默认关**。
		//   关掉的理由：这个"变淡"会让星的亮度跟着"面板/背包开没开"变 ——
		//   一开别的窗口，宿主那一帧不画面板 → 我们以为面板关了 → 不变淡 → 星突然变亮。
		//   用户 v0.14.2 报的"开背包亮、关背包暗"就是它。默认全亮，亮度只跟品质有关。
		if (g_settings.dimStarUnderPanel && panelOn
		    && sx >= g_panelX0 && sx <= g_panelX1 && sy >= g_panelY0 && sy <= g_panelY1) {
			col = (col & 0x00FFFFFFu) | (72u << 24);
		}
		if (drawn == 0) {   // 诊断：第一颗实际画出来的星画在屏幕哪儿
			firstSx = sx;
			firstSy = sy;
		}
		// ★ v0.14.4：描边粗细 = radius × star_outline%（0 = 不描边，最少 1px）。
		const float outlineW = (g_settings.starOutline > 0.0f)
			? ((radius * g_settings.starOutline * 0.01f) > 1.0f ? (radius * g_settings.starOutline * 0.01f) : 1.0f)
			: 0.0f;
		if (g_settings.starStyle == 1) {
			DrawStarShapeD3(dl, sx, sy, radius, col, StarOutlineColour(col), outlineW);   // ★ v0.15.0 暗黑3 风
		} else {
			DrawStarShape(dl, sx, sy, radius, col, StarOutlineColour(col), outlineW);     // 简约（平板）
		}
		if (m.slot == kSlotUnique) {
			++uniqueDrawn;
		} else {
			++setDrawn;
		}
		++drawn;
	}
	// ★ v0.12.7 调试十字：画在“引擎原始 point”上（12px），用来对照三种坐标
	//   解释哪个落在真标记上。默认已关。
	if (g_settings.starCoordDebug) {
		const ImU32 dbg = IM_COL32(255, 255, 255, 180);
		for (int i = 0; i < n; ++i) {
			const float px = g_starPoints[i].x;
			const float py = g_starPoints[i].y;
			if (px > -40.0f && py > -40.0f && px < maxX && py < maxY) {
				dl->AddLine(ImVec2(px - 12.0f, py), ImVec2(px + 12.0f, py), dbg, 2.0f);
				dl->AddLine(ImVec2(px, py - 12.0f), ImVec2(px, py + 12.0f), dbg, 2.0f);
			}
		}
	}
	g_starSamples.fetch_add(static_cast<std::uint32_t>(drawn), std::memory_order_relaxed);
	g_starsLive.store(drawn, std::memory_order_relaxed);
	g_starsLiveSet.store(setDrawn, std::memory_order_relaxed);
	g_starsLiveUnique.store(uniqueDrawn, std::memory_order_relaxed);

	// 限流诊断（每 ~2 秒一行）。末尾几个括号是给“下一轮出问题”用的：
	//   o0 = 当帧第一个观测的坐标+槽位+可信度；h0 = 快照第一条的坐标+槽位；
	//   age = 快照多久了（秒，-1 = 还没有快照）；d0 = 第一颗实际画出来的星的屏幕位置；
	//   rad = d0 离屏幕中心多远（"星是不是贴在人物身上"就看它）。
	//   drawn 应该永远 <= obs（或 <= 上一帧的 obs）。
	static int s_diagTick = 0;
	if ((s_diagTick++ % 120) == 0) {
		// ★ v0.14.5：把"这一帧报了几件、各自是什么槽位"一次列全。
		//   症状"地上没有套装却多出一颗绿星"看 osl 就知道第二件被判成了什么。
		char osl[48] {};
		int  ow = 0;
		for (int i = 0; i < obsCount && i < 4 && ow < static_cast<int>(sizeof(osl)) - 8; ++i) {
			ow += std::snprintf(osl + ow, sizeof(osl) - static_cast<std::size_t>(ow), "%s%d",
			                    (i == 0) ? "" : ",", s_obs[i].slot);
		}
		char hsl[48] {};
		int  hw2 = 0;
		for (int i = 0; i < g_stickyCount && i < 4 && hw2 < static_cast<int>(sizeof(hsl)) - 8; ++i) {
			hw2 += std::snprintf(hsl + hw2, sizeof(hsl) - static_cast<std::size_t>(hw2), "%s%d",
			                     (i == 0) ? "" : ",", g_sticky[i].slot);
		}
		char line[640] {};
		const double d0x = static_cast<double>(firstSx - hw);
		const double d0y = static_cast<double>(firstSy - hh);
		std::snprintf(line, sizeof(line),
			"Overlay stars: buffered=%d obs=%d drawn=%d hold=%d age=%.1f display=%.0fx%.0f "
			"mode=%d zoom=%.5f cam=(%.0f,%.0f) camOk=%d size=%.0f radar=%d persist=%.0f "
			"raw=%d o0=(%.0f,%.0f,s%d,c%d) h0=(%.0f,%.0f,s%d) d0=(%.0f,%.0f,rad=%.0f) "
			"osl=%s hsl=%s eng=%d/%d",
			n, obsCount, drawn, g_stickyCount,
			g_starSnapshotMs > 0 ? static_cast<double>(nowMs - g_starSnapshotMs) / 1000.0 : -1.0,
			c->IO.DisplaySize.x, c->IO.DisplaySize.y, mode,
			static_cast<double>(camZoom),
			static_cast<double>(camOffX), static_cast<double>(camOffY),
			camOk ? 1 : 0,
			static_cast<double>(radius), g_settings.mapZoomDiv,
			static_cast<double>(g_settings.starPersistSec),
			rawObsCount,
			obsCount > 0 ? static_cast<double>(s_obs[0].rx) : 0.0,
			obsCount > 0 ? static_cast<double>(s_obs[0].ry) : 0.0,
			obsCount > 0 ? s_obs[0].slot : -1,
			obsCount > 0 ? s_obs[0].conf : 0,
			g_stickyCount > 0 ? static_cast<double>(g_sticky[0].mx) : 0.0,
			g_stickyCount > 0 ? static_cast<double>(g_sticky[0].my) : 0.0,
			g_stickyCount > 0 ? g_sticky[0].slot : -1,
			drawn > 0 ? static_cast<double>(firstSx) : 0.0,
			drawn > 0 ? static_cast<double>(firstSy) : 0.0,
			drawn > 0 ? std::sqrt(d0x * d0x + d0y * d0y) : 0.0,
			osl, hsl,
			g_engineSetColor.load(std::memory_order_relaxed),
			g_engineUniqueColor.load(std::memory_order_relaxed));
		LogInfo(line);
	}
}


// SEH 包装：星标只是锦上添花，出任何异常都不影响面板和地图（也不会判死）。
auto SafeStars(ImGuiContext* c) noexcept -> int {
	__try {
		if (c == nullptr || !c->WithinFrameScope) {
			return 0;
		}
		DrawStars(c);
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

// 帧内 + 每帧只画一次 + 必须有"当前窗口"；整段包 SEH。
// 说明：ImGui 的 PushFont / Begin 都会去解引用 g.CurrentWindow，
// 而 NewFrame 会开一个隐式的 Debug##Default 窗口并一直保持到 EndFrame，
// 所以只要 WithinFrameScope 为真它就不该是空的 —— 这里照样显式判一次，
// 免得万一碰上别人的定制流程就崩在 PushFont 里。
// 返回：1 = 画了；0 = 这一帧已经画过；-2 = 回调不在帧内；-3 = 没有当前窗口；-1 = 异常。
auto SafeTick(ImGuiContext* c) noexcept -> int {
	__try {
		if (!c->WithinFrameScope) {
			return -2;   // 不在 NewFrame..EndFrame 之间，ImGui 不允许建窗口
		}
		if (c->CurrentWindow == nullptr) {
			return -3;   // 没有当前窗口，PushFont / Begin 会解引用空指针
		}
		if (c->FrameCount == g_lastFrame) {
			return 0;   // 这一帧已经画过了
		}
		g_lastFrame = c->FrameCount;
		ApplyMapZoom();   // ★ v0.12.8：雷达倍率（倍率=1 时这个函数什么都不做）
		DrawPanel(c);
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}

// 宿主每帧回调我们 5 个回调里的某几个。哪个在"帧内"就在哪个里面画 ——
// 这样不需要猜回调编号的含义，宿主改了顺序也不影响。
auto OnHostCallback(const void* ctxBytes) noexcept -> void {
	if (g_state.load(std::memory_order_relaxed) == kStateDead || ctxBytes == nullptr) {
		return;
	}
	if (!AddressReadable(ctxBytes, 0x20)) {
		return;
	}
	const auto* bytes = static_cast<const std::uint8_t*>(ctxBytes);
	ImGuiContext* c = nullptr;
	std::memcpy(&c, bytes + 8, sizeof(c));   // 宿主上下文布局：+0x08 = ImGui 上下文
	if (c == nullptr) {
		return;
	}
	g_hostSeen.store(true, std::memory_order_relaxed);

	// ★ v0.18.0：游戏第一次构建字体库之前，把自带的微软雅黑注册进去
	//（飘字插件同款路数；内部有"图集已烘焙就跳过"+ SEH + own_font 开关三重保险）。
	TryAddEarlyFont(c);

	if (g_state.load(std::memory_order_relaxed) == kStateUnknown) {
		const int ok = SafeValidate(c);

		if (ok < 0) {
			// 读宿主上下文出异常。宿主的第一个回调（cb0）是在它初始化叠加层时打的，
			// 那一刻它自己的渲染后端还没起来，读到的可能是半成品上下文 —— 不能一次判死。
			if (++g_validateExceptions >= kValidateExceptionLimit) {
				g_state.store(kStateDead, std::memory_order_relaxed);
				LogError("Overlay panel: reading the host ImGui context keeps raising exceptions; "
				         "its ImGui build is not compatible with ours, so the ImGui panel is "
				         "permanently disabled. Map colours are unaffected.");
			}
			return;
		}

		if (ok == 0) {
			// ★ v0.11.1 修的就是这里 ★
			// 第一版把"这一次不满足"直接钉成永久 Dead，于是宿主的 cb0
			// （后端名还是 nullptr）一下就把面板判了死刑。现在改成可重试。
			++g_validateTries;
			if (g_validateTries == 1 || g_validateTries == 240
			    || g_validateTries >= kValidateAttemptLimit) {
				char line[340] {};
				std::snprintf(line, sizeof(line),
					"Overlay panel: host context not usable yet (try#%u, stuck at check %d): "
					"renderer='%s' platform='%s' fonts=%d frame=%d",
					static_cast<unsigned>(g_validateTries), g_diagStep,
					g_diagRenderer, g_diagPlatform, g_diagFonts, g_diagFrame);
				LogInfo(line);
			}
			if (g_validateTries >= kValidateAttemptLimit) {
				g_state.store(kStateDead, std::memory_order_relaxed);
				char line[420] {};
				std::snprintf(line, sizeof(line),
					"Overlay panel: gave up after %u tries -- the host ImGui context never matched "
					"the imgui build we shipped (stuck at check %d). Panel = native fallback only; "
					"map colours are unaffected.",
					static_cast<unsigned>(g_validateTries), g_diagStep);
				LogWarn(line);
			}
			return;
		}

		g_ctx         = c;
		g_lastFrame   = -1;
		g_cjkFont     = SafeFindCjkFont(c);
		g_verifyTries = 0;
		g_state.store(kStatePending, std::memory_order_relaxed);

		char line[300] {};
		std::snprintf(line, sizeof(line),
			"Overlay panel: host ImGui context ACCEPTED (fonts=%d, cjk=%s). "
			"Checking index-buffer width before drawing...",
			c->IO.Fonts->Fonts.Size, (g_cjkFont != nullptr) ? "yes" : "no");
		LogInfo(line);

		// 把我们这一版 DLL 编译进去的布局指纹留一条痕。以后再出问题，
		// 翻日志一眼就能确认装的是不是"和宿主同一套结构体"的那一版。
		char layout[360] {};
		std::snprintf(layout, sizeof(layout),
			"Overlay panel: our ImGui build layout: FrameCount=0x%zX WithinFrameScope=0x%zX "
			"CurrentWindow=0x%zX Viewports=0x%zX sizeof(ImGuiIO)=0x%zX ImDrawIdx=%zu "
			"(host expects FrameCount=0x12B0 / WithinFrameScope=0x12BC / CurrentWindow=0x1368).",
			static_cast<std::size_t>(offsetof(ImGuiContext, FrameCount)),
			static_cast<std::size_t>(offsetof(ImGuiContext, WithinFrameScope)),
			static_cast<std::size_t>(offsetof(ImGuiContext, CurrentWindow)),
			static_cast<std::size_t>(offsetof(ImGuiContext, Viewports)),
			static_cast<std::size_t>(sizeof(ImGuiIO)),
			static_cast<std::size_t>(sizeof(ImDrawIdx)));
		LogInfo(layout);
	} else if (c != g_ctx) {
		// 宿主换了上下文（理论上不会）：重新开始数帧
		g_ctx       = c;
		g_lastFrame = -1;
	}

	// 第二步门槛：索引宽度。这一关没过之前一笔都不画 ——
	// 画错的代价是宿主的渲染器读越界，那是会崩游戏的。
	// 注意只有"确认不一致"(-1) 才判死；读不出来 / 出异常只是再等一帧。
	if (g_state.load(std::memory_order_relaxed) == kStatePending) {
		const int w = SafeIndexWidthCheck(g_ctx);
		if (w == -1) {
			g_state.store(kStateDead, std::memory_order_relaxed);
			g_open.store(false, std::memory_order_relaxed);
			LogError("Overlay panel: the host uses a different index-buffer layout "
			         "(ImDrawIdx width mismatch); the ImGui panel is permanently disabled so we never "
			         "risk corrupting the map plugin's rendering. Map colours are unaffected.");
			return;
		}
		++g_verifyTries;
		if (w == 1) {
			g_state.store(kStateReady, std::memory_order_relaxed);
			LogInfo("Overlay panel: index-buffer layout verified; the ImGui panel is ready.");
		} else if (g_verifyTries >= kVerifyAttemptLimit) {
			g_state.store(kStateReady, std::memory_order_relaxed);
			LogWarn("Overlay panel: could not sample the host's index buffer in time; "
			        "enabling the panel without that check.");
		} else {
			return;   // 还在等宿主画出第一帧数据
		}
	}

	if (!g_open.load(std::memory_order_relaxed)) {
		// ★ 面板关着也要画星标（v0.12.0：星标独立于面板，只要宿主叠加层在就画）。
		if (g_state.load(std::memory_order_relaxed) == kStateReady && g_settings.stars) {
			if (SafeStars(g_ctx) == -1) {
				static bool s_starFaultLogged = false;
				if (!s_starFaultLogged) {
					s_starFaultLogged = true;
					LogWarn("Overlay stars: drawing raised an exception; stars are disabled for this "
					        "session (panel and map colours are unaffected).");
				}
			}
		}
		// 面板关着：鼠标交还宿主，并把它原有的 ImGuiConfigFlags 原样还回去。
		// （接管是在绘制里做的，所以只有在这里收尾才不会漏。）
		ReleaseInput(g_ctx);
		// ★ v0.13.0：顺手把"挡鼠标"的 WH_MOUSE 钩子摘掉（钩子常驻没好处，
		//   下次开面板重装时正好又排到游戏那 5 个钩子前面）。
		ReleaseGameMouseHook();
		g_mouseInPanel = false;
		g_panelScrValid.store(false, std::memory_order_relaxed);
		return;
	}

	// ★ 星标先画（前台图层本来就在所有窗口之上，顺序只影响不出错）。
	if (g_state.load(std::memory_order_relaxed) == kStateReady && g_settings.stars) {
		if (SafeStars(g_ctx) == -1) {
			static bool s_starFaultLogged2 = false;
			if (!s_starFaultLogged2) {
				s_starFaultLogged2 = true;
				LogWarn("Overlay stars: drawing raised an exception; stars are disabled for this "
				        "session (panel and map colours are unaffected).");
			}
		}
	}

	// 注意顺序：-1(异常) 必须单独判，别用 result<0 把 -2/-3 一起吞了
	// ——那两条只是"这一帧画不了"，判死就白瞎了。
	const int result = SafeTick(g_ctx);
	if (result == -1) {
		g_state.store(kStateDead, std::memory_order_relaxed);
		g_open.store(false, std::memory_order_relaxed);
		char line[420] {};
		std::snprintf(line, sizeof(line),
			"Overlay panel: drawing raised an exception at stage=%d (%s); the ImGui panel is "
			"permanently disabled from now on (the game itself is fine).",
			static_cast<int>(g_drawStage), StageText(g_drawStage));
		LogError(line);
		return;
	}
	if (result == -2) {
		// 这个回调不在宿主的 NewFrame..EndFrame 之间 → 在这里建窗口是非法的。
		// 别的回调里只要有一个在帧内就能画出来，所以只是等。
		if (!g_warnedNoFrame.exchange(true, std::memory_order_relaxed)) {
			LogWarn("Overlay panel: panel is OPEN, but this host callback fires OUTSIDE the ImGui "
			        "frame (NewFrame..EndFrame), so no window can be created from it. "
			        "Waiting for a callback that is inside the frame.");
		}
		return;
	}
	if (result == -3) {
		if (!g_warnedNoWindow.exchange(true, std::memory_order_relaxed)) {
			LogWarn("Overlay panel: in-frame but the host has no current window; retrying.");
		}
		return;
	}
	if (result > 0) {
		const std::uint32_t n = g_draws.fetch_add(1, std::memory_order_relaxed) + 1;
		if (n == 1) {
			LogInfo("Overlay panel: first frame drawn into the host's ImGui layer.");
		}
	}
}

auto Toggle() noexcept -> bool {
	const std::uint32_t s = g_state.load(std::memory_order_relaxed);
	if (s == kStateDead) {
		// 确认不可用 → 交给调用方退回游戏原生面板
		return false;
	}
	if (s == kStateUnknown && !g_hostSeen.load(std::memory_order_relaxed)) {
		// 宿主一次都没来叫过我们 —— MapSense 很可能根本不在场，
		// 这时候把开关打开只会开出一个永远不出现的窗口。退回原生面板。
		return false;
	}
	// 走到这里：宿主在（或者自检正在进行），照样把开关翻过来，
	// 自检一过窗口就出来。
	const bool now = !g_open.load(std::memory_order_relaxed);
	g_open.store(now, std::memory_order_relaxed);
	char line[240] {};
	std::snprintf(line, sizeof(line),
		"Overlay panel: %s (drawn in MapSense's overlay layer)%s.",
		now ? "opened" : "closed",
		(s != kStateReady) ? " -- will appear as soon as the self-check passes" : "");
	LogInfo(line);
	return true;
}

auto IsOpen() noexcept -> bool {
	return g_open.load(std::memory_order_relaxed);
}

auto IsReady() noexcept -> bool {
	return g_state.load(std::memory_order_relaxed) == kStateReady;
}

auto StateText() noexcept -> const char* {
	switch (g_state.load(std::memory_order_relaxed)) {
	case kStateReady:   return "ready";
	case kStatePending: return "checking layout";
	case kStateDead:    return "disabled(see log)";
	default:
		return (g_validateTries > 0) ? "checking host context" : "waiting for the host";
	}
}

// ★ v0.18.5：面板退回**游戏原生面板**时，把根因一次讲清楚（每次运行只记一行）。
//
//   绝大多数情况只有一个原因：**没装地图插件 RuffnecKk MapSense**。
//   本插件的漂亮面板、以及地图上的星形标记，都是画在 MapSense 那一层 Dear ImGui 里的
//   （它是往游戏画面上叠层的唯一渲染主人；自己再叠一层会跟它抢 DirectX 12 而崩，
//     这在 v0.3.x 时代已经实测过一次）。
//   MapSense 不在场 ⇒ 没有那一层可画 ⇒ 只剩游戏原生面板可用，星标也不会出现；
//   但"改游戏自己标记的颜色"那部分（取色钩子）照常生效，所以不是完全没效果。
auto ExplainFallback() noexcept -> void {
	static bool s_logged = false;
	if (s_logged) {
		return;
	}
	s_logged = true;

	const HMODULE host = ::GetModuleHandleW(L"d2rl-ruffneckk-mapsense.dll");
	const bool  hasApi = (host != nullptr) &&
		(::GetProcAddress(host, "RuffnecKkMapSenseGetOverlayHostApi") != nullptr);

	char line[560] {};
	std::snprintf(line, sizeof(line),
		"loot-map: panel fallback -- the legacy NATIVE panel is used this session. "
		"overlay_state='%s' host_callback_seen=%d mapsense_module=%d overlay_api=%d. "
		"WHY IT MATTERS: both the ImGui panel and the map stars are drawn inside RuffnecKk "
		"MapSense's overlay layer, so MapSense (d2rl-ruffneckk-mapsense.dll in "
		"d2rloader\\plugins) must be installed and loaded; without it only this legacy panel "
		"and the item COLOURS work (no stars).",
		StateText(),
		g_hostSeen.load(std::memory_order_relaxed) ? 1 : 0,
		(host != nullptr) ? 1 : 0,
		hasApi ? 1 : 0);
	LogWarn(line);
}

}   // namespace OverlayPanel

// 前面 InputActions / 控制台命令用到的两个入口
auto ToggleOverlayPanel() noexcept -> bool { return OverlayPanel::Toggle(); }
auto OverlayPanelReady() noexcept -> bool { return OverlayPanel::IsReady(); }

auto __fastcall OverlayCb0(const void* ctx, void* userData) noexcept -> void { OverlayProbeHit(0, ctx, userData); OverlayPanel::OnHostCallback(ctx); }
auto __fastcall OverlayCb1(const void* ctx, void* userData) noexcept -> void { OverlayProbeHit(1, ctx, userData); OverlayPanel::OnHostCallback(ctx); }
auto __fastcall OverlayCb2(const void* ctx, void* userData) noexcept -> void { OverlayProbeHit(2, ctx, userData); OverlayPanel::OnHostCallback(ctx); }
auto __fastcall OverlayCb3(const void* ctx, void* userData) noexcept -> void { OverlayProbeHit(3, ctx, userData); OverlayPanel::OnHostCallback(ctx); }
auto __fastcall OverlayCb4(const void* ctx, void* userData) noexcept -> void { OverlayProbeHit(4, ctx, userData); OverlayPanel::OnHostCallback(ctx); }

auto TryRegisterOverlayClient() noexcept -> bool {
	const HMODULE host = ::GetModuleHandleW(L"d2rl-ruffneckk-mapsense.dll");
	if (host == nullptr) {
		return false;   // 还没加载，下次再试
	}

	const auto getApi = reinterpret_cast<OverlayHostGetFn>(
		reinterpret_cast<void*>(::GetProcAddress(host, "RuffnecKkMapSenseGetOverlayHostApi")));
	if (getApi == nullptr) {
		LogWarn("OVL: MapSense is loaded but does not export the overlay host API; giving up.");
		g_ovlState.store(2, std::memory_order_relaxed);
		return false;
	}

	const auto* api = static_cast<const OverlayHostApi*>(getApi(2, 0x20));
	if (api == nullptr) {
		LogWarn("OVL: the overlay host API is not published yet (it answers nullptr); retrying.");
		return false;
	}

	const std::uint32_t apiSize = api->structSize;
	const std::uint32_t apiVer  = api->version;
	const std::uint64_t apiMag  = api->magic;
	const void* fnRegister      = reinterpret_cast<const void*>(api->registerClient);
	const void* fnUnregister    = reinterpret_cast<const void*>(api->unregisterClient);

	char head[360] {};
	std::snprintf(head, sizeof(head),
		"OVL host api: size=%u ver=%u magic=0x%016llX register=%p unregister=%p",
		static_cast<unsigned>(apiSize), static_cast<unsigned>(apiVer),
		static_cast<unsigned long long>(apiMag), fnRegister, fnUnregister);
	LogInfo(head);

	if (apiSize < 0x20u || apiVer != 2u || api->registerClient == nullptr) {
		LogWarn("OVL: the overlay host API header does not match what we reverse-engineered; not registering.");
		g_ovlState.store(2, std::memory_order_relaxed);
		return false;
	}

	OverlayClientDesc desc {};
	desc.structSize = static_cast<std::uint32_t>(sizeof(desc));
	desc.version    = 2;
	desc.name       = "d2rl-loot-map";
	desc.magic      = kOverlayMagic;
	// 抄的是另一个"确实在跑"的叠加层客户端的注册方式 ——
	// RuffnecKk Floating Damage 1.5.0（同一套 MapSense，同一版 Dear ImGui 1.91.5）。
	// 它填 cb0 / cb1 / cb2 / cb4，**cb3 留空**，真正画东西的是 cb4。
	// 我们原来 5 个全填，结果宿主也把 cb3 叫起来了（每帧一对 cb3+cb4），
	// 多出来的那一个只会让日志变吵。cb3 留空 = 和它完全一致。
	desc.callback[0] = &OverlayCb0;
	desc.callback[1] = &OverlayCb1;
	desc.callback[2] = &OverlayCb2;
	desc.callback[3] = nullptr;   // 见上：FD 就是这么留空的
	desc.callback[4] = &OverlayCb4;
	desc.userData    = &g_ovlUserTag;

	const bool accepted = api->registerClient(&desc);

	char result[240] {};
	std::snprintf(result, sizeof(result),
		"OVL registerClient(\"%s\", 0x%X bytes) -> %s",
		desc.name, static_cast<unsigned>(desc.structSize), accepted ? "ACCEPTED" : "refused");
	LogInfo(result);

	if (accepted) {
		g_ovlApi = api;
		g_ovlState.store(1, std::memory_order_relaxed);
	}
	return accepted;
}

constexpr ULONGLONG kOverlayRetryMs = 1500;
constexpr std::uint32_t kOverlayMaxAttempts = 160;

auto OverlayProbeTick(const D2RL::PluginContext* ctx, void* /*userData*/) noexcept -> void {
	static std::atomic<ULONGLONG> lastAttempt { 0 };

	const std::uint32_t state = g_ovlState.load(std::memory_order_relaxed);
	if (state == 0) {
		const ULONGLONG now = GetTickCount64();
		if (now - lastAttempt.load(std::memory_order_relaxed) >= kOverlayRetryMs) {
			lastAttempt.store(now, std::memory_order_relaxed);
			const std::uint32_t n = g_ovlAttempts.fetch_add(1, std::memory_order_relaxed) + 1;
			const bool ok = TryRegisterOverlayClient();
			if (!ok && (n == 1 || n % 20 == 0)) {
				char line[200] {};
				std::snprintf(line, sizeof(line), "OVL: join attempt #%u did not go through.", static_cast<unsigned>(n));
				LogInfo(line);
			}
			if (n >= kOverlayMaxAttempts) {
				LogWarn("OVL: gave up joining the MapSense overlay layer after many attempts.");
				OverlayPanel::ExplainFallback();   // ★ v0.18.5：顺带把"缺 MapSense"这个根因写清楚
				g_ovlState.store(2, std::memory_order_relaxed);
			}
		}
	}

	// 继续排队：注册成功后还要靠宿主回调把上下文送进来。
	if (ctx != nullptr) {
		const D2RL::ThreadService* threads = nullptr;
		if (ctx->QueryService(&threads) == D2RL::ServiceQueryResult::Success && threads != nullptr
		    && threads->runOnUiThread != nullptr) {
			(void)threads->runOnUiThread(ctx, &OverlayProbeTick, nullptr);
		}
	}
}

auto StartOverlayProbe(const D2RL::PluginContext* ctx) noexcept -> void {
	if (ctx == nullptr) {
		return;
	}
	LogInfo("OVL: knocking on the MapSense overlay host to see whether we may draw in its layer.");
	(void)TryRegisterOverlayClient();

	const D2RL::ThreadService* threads = nullptr;
	if (ctx->QueryService(&threads) == D2RL::ServiceQueryResult::Success && threads != nullptr
	    && threads->runOnUiThread != nullptr) {
		(void)threads->runOnUiThread(ctx, &OverlayProbeTick, nullptr);
	}
}

} // namespace

// ─────────────────────────── 插件入口 ───────────────────────────

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;

	context->LogInfo("Loot Map 0.18.8 loading ... (the ImGui panel and the map stars are drawn inside an overlay layer: RuffnecKk MapSense, or the standalone d2rl-loot-map-standalone.dll when MapSense is absent; if neither is present it falls back to the legacy native panel and logs exactly why)");

	// 1) 读配置
	(void)context->EnsureConfig();
	if (!LoadSettings(context, g_settings)) {
		context->LogWarn("Loot Map: config not readable, using built-in defaults.");
	}
	// ★ v0.13.0：把"面板挡鼠标"的开关同步给 WH_MOUSE 钩子读的那一份原子量。
	g_blockGameMouse.store(g_settings.blockGameMouse, std::memory_order_relaxed);
	if (!g_settings.enabled) {
		context->LogInfo("Loot Map: disabled in the config; nothing was installed.");
		return true;
	}

	// 1b) SDK 探测的接线（服务扫描 + 生命周期监听），放在钩子之前：
	//     即使取色钩子因游戏版本不匹配而失败，探测数据照样能拿到。
	ProbeServiceScan(context, "load");
	InstallProbeListeners(context);
	context->RegisterConsoleCommand("lootmap-probe", ProbeCommand,
		"Scan the plugin SDK services and dump ground-item fields into loot-map.log.");

	// 1c) 敲门：问 MapSense 的叠加层宿主，我们能不能把自定义图标画进它那一层。
	//     纯注册 + 记录，不绘制任何东西；失败也只是日志里几行字。
	StartOverlayProbe(context);

	// 2) 自绘面板宿主 —— v0.3.4 起整体停用。
	//    崩溃报告（d2r-crash-report 2026_09_20 15_31_35）证实：本插件的
	//    "事后抓取"方案会改写系统里所有交换链共用的跳板表，而 MapSense
	//    启动早期也在改同一张表并创建测试交换链，两者相撞 → 游戏启动 0.5 秒
	//    内访问违例崩溃（0xC0000005 in d3d12.dll，调用链 MapSense线程→dxgi→本插件）。
	//    结论：只要 MapSense 在场，任何全局 vtable 补丁都不安全。面板功能
	//    等找到不冲突的方案（例如只挂游戏自身交换链的 Present）再恢复。
	//    地图上色钩子（下方第 3 步）与渲染宿主完全独立，不受影响。
	// RenderHost::SetLogger([](const char* text) noexcept {
	// 	if (text != nullptr) {
	// 		LogInfo(text);
	// 	}
	// });
	// RenderHost::SetUiCallback(&DrawPanelUi);
	// RenderHost::Install();
	context->LogInfo("Loot Map: self-drawn (ImGui/swapchain) panel stays DISABLED on purpose "
	                 "(it would fight MapSense over the renderer). "
	                 "The settings panel is now a NATIVE loader panel instead.");

	// 3) 单位颜色钩子（字节校验通过才装）
	const std::uint32_t expectedSize = static_cast<std::uint32_t>(sizeof(kGetUnitColorIndexExpected));
	if (!context->CheckExpectedBytes(kGetUnitColorIndexRva, kGetUnitColorIndexExpected, expectedSize)) {
		context->LogError("Loot Map: the game bytes at the colour function do not match. "
		                  "This game build is not supported; the hook was NOT installed.");
		return false;
	}
	if (!context->InstallInlineHook(kGetUnitColorIndexRva,
	                                kGetUnitColorIndexExpected,
	                                expectedSize,
	                                reinterpret_cast<void*>(&HookGetUnitColorIndex),
	                                reinterpret_cast<void**>(&g_originalGetUnitColorIndex))) {
		context->LogError("Loot Map: could not install the colour hook.");
		return false;
	}
	context->LogInfo("Loot Map: colour hook installed.");

	// 3b) 任意颜色：把光点绘制里那条 call 接到我们这里
	//     （反汇编实锤：RVA 0xD6EE3 的 call sub_858510 就是画光点的那一步；
	//      我们改写它传入的样式结构里的 r/g/b，颜色就完全由我们说了算。）
	TryInstallBlobIconColourPatch();

	// 3c) 原生设置面板（v0.8.0）：注册 JSON 布局 + 面板 + 按钮消息监听。
	//     走游戏自己的 UI 系统，完全不碰 DirectX / ImGui，
	//     所以和 MapSense 这类在地图渲染上动手的插件结构上不可能冲突。
	//     失败也只是日志里一行，地图上色照常。
	(void)NativePanel::Install(context);

	// 3d) 「控制」菜单按键（v0.9.0）：把"打开面板"注册成游戏原生动作，
	//     这样玩家能在 设置 → 控制 里看到它并自己改键（默认 F7）。
	//     和面板一样是"非致命"的：注册不上只影响这个键，其余功能照常。
	(void)InputActions::Install(context);

	// 4) 控制台命令
	context->RegisterConsoleCommand("lootmap", TogglePanelCommand, "Open / close the Loot Map panel (same as the Controls hotkey).");
	context->RegisterConsoleCommand("lootmap-status", StatusCommand, "Show the Loot Map status and all quality keys.");
	context->RegisterConsoleCommand("lootmap-reload", ReloadCommand, "Re-read loot-map.toml from disk.");
	context->RegisterConsoleCommand("lootmap-set", SetCommand, "lootmap-set <key> on|off | lootmap-set mode observe|color");

	char summary[300] {};
	std::snprintf(summary, sizeof(summary),
		"Loot Map ready. mode=%s quality_offset=%d renderer=%s",
		g_settings.observeOnly ? "observe" : "color",
		g_settings.qualityOffset,
		RenderHost::RendererStatusText());
	context->LogInfo(summary);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	// 面板与消息监听交给加载器自动回收，这里只清插件自己的状态
	NativePanel::Uninstall();

	// 注销「控制」菜单里的那条按键 —— 必须赶在本模块卸载前做，
	// 否则留在游戏里的回调指针会指向已释放的内存。
	InputActions::Uninstall();

	// ★ v0.13.0：面板挡鼠标的 WH_MOUSE 钩子也必须先摘 —— 钩子过程就在本模块
	//   里，留着一个指向即将卸载内存的回调 = 游戏线程下次点鼠标就崩。
	OverlayPanel::ReleaseGameMouseHook();
	// ★ v0.18.1：面板滚轮的低级钩子线程同理 —— 先等它把钩子摘完再继续卸载。
	OverlayPanel::StopPanelWheelHookThread();

	// 先还原两个光点绘制的 call（它们直接改的是游戏本体代码）——
	// ★ v0.12.4：两个调用点逐一对称还原（还原字节 + 释放小桩页）。v0.12.5：B 站点换成 C。
	{
		auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
		for (int si = 0; si < kBlobPatchSiteCount; ++si) {
			BlobPatchState& st = g_blobPatchStates[si];
			if (!st.patched) {
				continue;
			}
			auto* site = base + kBlobPatchSites[si].callRva;
			DWORD oldProtect = 0;
			if (::VirtualProtect(site, sizeof(st.saved), PAGE_EXECUTE_READWRITE, &oldProtect)) {
				std::memcpy(site, st.saved, sizeof(st.saved));
				::FlushInstructionCache(::GetCurrentProcess(), site, sizeof(st.saved));
				::VirtualProtect(site, sizeof(st.saved), oldProtect, &oldProtect);
			}
			if (st.page != nullptr) {
				::VirtualFree(st.page, 0, MEM_RELEASE);
				st.page = nullptr;
			}
			st.patched = false;
		}
	}
	g_blobIconHookInstalled.store(false);
	g_pendingRgb.active = false;

	// 再还原 automap-blob 的取色函数指针（防止它先卸载、我们的函数变成悬空指针）
	if (g_blobPtrHookInstalled.load(std::memory_order_relaxed) && g_blobColorFnSlot != nullptr) {
		DWORD oldProtect = 0;
		if (::VirtualProtect(g_blobColorFnSlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
			*g_blobColorFnSlot = g_blobColorFnSaved;
			::VirtualProtect(g_blobColorFnSlot, sizeof(void*), oldProtect, &oldProtect);
		}
		{
			char line[200] {};
			std::snprintf(line, sizeof(line),
				"Loot Map unload: blob stub saw %u calls, %u of them items.",
				static_cast<unsigned>(g_blobStubCalls.load(std::memory_order_relaxed)),
				static_cast<unsigned>(g_blobItemSeen.load(std::memory_order_relaxed)));
			LogInfo(line);
		}
		g_blobColorFnSlot  = nullptr;
		g_blobColorFnSaved = nullptr;
		g_blobPtrHookInstalled.store(false);
	}
	g_originalGetUnitColorIndex = nullptr;
	g_context                   = nullptr;
	RenderHost::Shutdown();
}

// ─────────────────────────── 加载心跳诊断 ───────────────────────────
// D2RLoader 若报 "Windows error 1114"，说明 DllMain/静态初始化阶段失败、
// 插件逻辑根本没跑。这个心跳文件能区分两种情况：
//   · 没有 attach 记录  → DLL 连 DllMain 都没进去（依赖库/CRT 层面的失败）
//   · 有 attach 记录    → DllMain 成功，问题在插件加载逻辑之后
// 文件位置：%TEMP%\d2rl-loot-map.attach.log（追加写，不影响任何游戏文件）

auto WriteHeartbeat(const wchar_t* tag) noexcept -> void {
	wchar_t tempPath[MAX_PATH] {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0) {
		return;
	}
	wchar_t path[MAX_PATH] {};
	std::swprintf(path, MAX_PATH, L"%sd2rl-loot-map.attach.log", tempPath);
	HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}
	char line[128];
	const int n = std::snprintf(line, sizeof(line), "%ls pid=%lu tick=%lu\n",
		tag,
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount64() & 0xFFFFFFFFull));
	if (n > 0) {
		DWORD written = 0;
		WriteFile(file, line, static_cast<DWORD>(n), &written, nullptr);
	}
	CloseHandle(file);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(instance);
		WriteHeartbeat(L"attach");
		break;
	case DLL_PROCESS_DETACH:
		WriteHeartbeat(L"detach");
		break;
	default:
		break;
	}
	return TRUE;
}

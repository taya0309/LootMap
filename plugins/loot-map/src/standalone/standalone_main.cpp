// ===========================================================================
//  Loot Map —— 独立叠加层版（standalone）
//  v0.2.0  第 2 步：往游戏画面上画东西（证明"我们真的能往上写"）
//
//  为什么另做一个 DLL：
//    · 主插件（d2rl-loot-map.dll）的星标与面板都画在 RuffnecKk MapSense 的
//      叠加层里 —— 没装 MapSense 的人就只有颜色、没有星，F7 还是老式原生面板。
//    · 这一版**完全不依赖 MapSense**：自己往游戏画面上叠一层。
//    · 两个 DLL 各自独立：主插件照旧给装了 MapSense 的人用，这一版给没装的人用。
//
//  做法照抄 MapSense 验证过的路子（其叠加层宿主 d3d12_imgui_host.cpp，
//  开源 MIT：https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite ，
//  该文件注明改编自 locbones/D2RHUD-2.4，作者已授权）：
//    ① **用 MinHook 钩函数本身，绝不改虚表**（当年 v0.3.x 直接改虚表槽
//       + 自建第二个 D3D12 设备 ⇒ 与 MapSense 启动期相撞，启动 0.5 s 崩在 d3d12.dll）；
//    ② 钩 IDXGIFactory 的 CreateSwapChain / ForHwnd / ForCoreWindow / ForComposition
//       —— 游戏建交换链时我们就被叫到，于是**精确拿到"命令队列 + 交换链 + 窗口"三件套**；
//    ③ 从真交换链虚表读出 Present / ResizeBuffers 的**函数地址**，再用 MinHook 钩这两个函数；
//    ④ 绘制就在 Present 里同步做（本版开始）。
//
//  实测记录：
//    v0.1.0（2026-09-23 01:39）：工厂钩子 4/4 装上、交换链抓到，但"等交换链再补钩子"
//      那一步靠加载器 runOnUiThread 排队，**回调没被调度到** ⇒ 后续钩子没装上。
//    v0.1.1（2026-09-23 01:43）：改成**自带短命后台线程**轮询 ⇒ 全绿：
//      `swap-chain hooks armed` + `Present #1 … #9600`，游戏全程不崩。
//
//  v0.2.0（2026-09-23 01:52）：**真的能画了** —— 屏幕上出现整屏细蓝框 + 顶部中文
//    测试板 + 来回跑的小方块，`first overlay frame drawn … Font atlas = 1024x2048`，
//    `Present #9000 (drawn=8999)` 一帧不落，游戏全程不崩。
//
//  v0.3.0（本次）：**变成"叠加层宿主"**（不再是自己画测试板）。
//    读主插件源码发现：它的漂亮面板与地图星标**只依赖一个 4 函数的宿主接口**
//      RuffnecKkMapSenseGetOverlayHostApi(abi=2, structSize>=0x20)
//    真正干活的只有 cb4（每帧在 ImGui 帧内被叫一次）。
//    所以这一版把那个接口实现出来（见 host_api.h/.cpp）⇒ **主插件一个字节都不用改**，
//    开机就会把面板和星标画进我们这一层 —— 界面与显示效果和装了 MapSense 时完全一致，
//    因为画的还是同一份代码，只是换了块画布。
//    另外：主插件是按**模块名**找宿主的（GetModuleHandleW("d2rl-ruffneckk-mapsense.dll")），
//    我们给这个查询装了一个极窄的别名钩子（真实模块不存在时才代答），见 host_api.cpp。
//
//  安全约定（红线，别破）：
//    · **不创建 D3D12 设备**（设备直接从游戏交换链取：`swapChain->GetDevice`）；
//    · **不改任何虚表**；只让 MinHook 钩函数；
//    · 用**自己的**命令分配器 / 命令列表 / RTV 描述符堆 / 围栏，提交到**游戏自己的队列**；
//      绝不碰游戏的命令列表，所以不会破坏游戏渲染，也不会有"两个设备抢画面"的问题；
//    · 后台缓冲要画之前切 RENDER_TARGET、画完切回 PRESENT（D3D12 必需）；
//      并在 ResizeBuffers 里**先松开**我们对后台缓冲的引用（否则游戏那次改尺寸会失败）；
//    · **MapSense 在场时本版自动停用**（两个叠加层同时钩同一批函数会互相打架）；
//    · 绘制整段套 SEH 防护罩：万一出异常就地停用绘制，游戏继续跑（v0.17.6 的教训）；
//    · 卸载时：先让后台线程退出 → 摘钩子 → 等 GPU 空 → 再释放资源。
// ===========================================================================

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "host_api.h"

#include <process.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

// ─────────────────────────── 插件基本信息 ───────────────────────────

constexpr std::uint32_t kPluginAbiVersion = 3;

constexpr D2RL::PluginFlags kFlags =
	D2RL::PluginFlags::Shared |
	D2RL::PluginFlags::NativeHooks;

constexpr D2RL::PluginInfo kPluginInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = kPluginAbiVersion,
	.id          = "loot-map-standalone",
	.name        = "Loot Map (standalone overlay)",
	.version     = "0.3.1",
	.author      = "local build",
	.description = "Standalone build for machines WITHOUT RuffnecKk MapSense: it IS the overlay layer (implements the MapSense overlay host API), so the main plugin draws its panel and map stars here unchanged.",
	.flags       = kFlags,
};

const D2RL::PluginContext* g_context = nullptr;

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

// MinHook 状态码 → 人话（不用 MH_StatusToString：它要额外的编译宏才存在）
auto StatusText(MH_STATUS st) noexcept -> const char* {
	switch (st) {
	case MH_OK:                         return "OK";
	case MH_ERROR_ALREADY_INITIALIZED:  return "already initialized";
	case MH_ERROR_NOT_INITIALIZED:      return "not initialized";
	case MH_ERROR_ALREADY_CREATED:      return "already created";
	case MH_ERROR_NOT_CREATED:          return "not created";
	case MH_ERROR_ENABLED:              return "already enabled";
	case MH_ERROR_DISABLED:             return "already disabled";
	case MH_ERROR_NOT_EXECUTABLE:       return "target not executable";
	case MH_ERROR_UNSUPPORTED_FUNCTION: return "unsupported function";
	case MH_ERROR_MEMORY_ALLOC:         return "memory alloc failed";
	case MH_ERROR_MEMORY_PROTECT:       return "memory protect failed";
	case MH_ERROR_MODULE_NOT_FOUND:     return "module not found";
	case MH_ERROR_FUNCTION_NOT_FOUND:   return "function not found";
	default:                            return "unknown";
	}
}

// ─────────────────────── DXGI 虚表方法号（照抄 MapSense）───────────────────────
//  IDXGIFactory   : CreateSwapChain                = 10
//  IDXGIFactory2  : CreateSwapChainForHwnd         = 15
//                   CreateSwapChainForCoreWindow   = 16
//                   CreateSwapChainForComposition  = 24
//  IDXGISwapChain : Present                        = 8    （v0.1.x 实测参数正确）
//                   GetDesc                        = 12
//                   ResizeBuffers                  = 13   ← ★ v0.2.0 修正
//  IDXGISwapChain1: Present1                       = 22
//
//  ★ v0.2.0 修正：v0.1.x 把 ResizeBuffers 当成了槽 12 —— 其实 12 是 GetDesc
//    （只有一个参数），所以 v0.1.1 日志里那几行 `ResizeBuffers(1438816, 431662000x…)`
//    是拿错参数顺序读出来的垃圾（当时不影响行为，因为那一步什么都不做）。
constexpr std::size_t kFactoryCreateSwapChain               = 10;
constexpr std::size_t kFactoryCreateSwapChainForHwnd        = 15;
constexpr std::size_t kFactoryCreateSwapChainForCoreWindow  = 16;
constexpr std::size_t kFactoryCreateSwapChainForComposition = 24;
constexpr std::size_t kSwapChainPresent                     = 8;
constexpr std::size_t kSwapChainResizeBuffers               = 13;
constexpr std::size_t kSwapChainPresent1                    = 22;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(
	IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
	IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
	IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
	const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using CreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(
	IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
	IDXGIOutput*, IDXGISwapChain1**);
using CreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(
	IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);

CreateSwapChainFn               g_origCreateSwapChain { nullptr };
CreateSwapChainForHwndFn        g_origCreateSwapChainForHwnd { nullptr };
CreateSwapChainForCoreWindowFn  g_origCreateSwapChainForCoreWindow { nullptr };
CreateSwapChainForCompositionFn g_origCreateSwapChainForComposition { nullptr };
PresentFn                       g_origPresent { nullptr };
Present1Fn                      g_origPresent1 { nullptr };
ResizeBuffersFn                 g_origResizeBuffers { nullptr };

// 摘钩子要用的"目标函数地址"（MH_RemoveHook 收目标地址，不是我们的 detour）。
void* g_factoryTarget[4] { nullptr, nullptr, nullptr, nullptr };
void* g_presentTarget  = nullptr;
void* g_present1Target = nullptr;
void* g_resizeTarget   = nullptr;
bool  g_factoryHooked[4] { false, false, false, false };
bool  g_swapChainHooked = false;

// ─────────────────────────── 运行期状态 ───────────────────────────

std::atomic<bool>          g_active { false };
std::atomic<bool>          g_shuttingDown { false };
std::atomic<bool>          g_installClaimed { false };   // 只允许装一次交换链钩子
std::atomic<std::uint64_t> g_presentCalls { 0 };
std::atomic<std::uint64_t> g_drawnFrames { 0 };
std::atomic<std::uint64_t> g_swapChainsSeen { 0 };
std::atomic<void*>         g_firstQueue { nullptr };
std::atomic<void*>         g_firstSwapChain { nullptr };
std::atomic<HWND>          g_firstHwnd { nullptr };
std::atomic<int>           g_creationPath { 0 };   // 1 CoreWindow / 2 Hwnd / 3 Desc / 4 Composition
std::atomic<HANDLE>        g_worker { nullptr };

auto NoteFirstBinding(const char* origin, void* queue, void* swapChain, HWND hwnd) noexcept -> void {
	if (g_swapChainsSeen.fetch_add(1) != 0) {
		return;
	}
	g_firstQueue.store(queue);
	g_firstSwapChain.store(swapChain);
	g_firstHwnd.store(hwnd);
	char line[460] {};
	std::snprintf(line, sizeof(line),
		"standalone: first swap chain captured via %s -> queue=%p swapChain=%p hwnd=%p "
		"(this is the exact queue we submit our own draw commands on)",
		origin, queue, swapChain, hwnd);
	LogInfo(line);
}

// =====================================================================================
//  渲染（第 2 步）——用自己的命令列表画在游戏画面上
// =====================================================================================

constexpr UINT  kMaxFrames = 8;                 // 最多支持的交换链后台缓冲数
constexpr float kFontPx    = 18.0f;

struct GfxState {
	CRITICAL_SECTION lock {};
	bool  lockReady   = false;
	bool  initialized = false;
	bool  gaveUp      = false;      // 一旦为 true，永不再尝试（保护游戏）
	bool  needRebuild = false;      // ResizeBuffers 之后要重建渲染目标
	bool  loggedFirst = false;
	bool  imguiCtxReady     = false;   // ImGui::CreateContext 成功
	bool  imguiBackendReady = false;   // ImGui_ImplDX12_Init 成功
	bool  win32Ready        = false;   // ImGui_ImplWin32_Init 成功
	bool  earlyCallDone     = false;   // 已在"图集未烘焙"时叫过客户端一次（字体注册靠它）

	ID3D12Device*       device    = nullptr;
	ID3D12CommandQueue* queue     = nullptr;
	IDXGISwapChain3*    swapChain = nullptr;   // AddRef 过的
	HWND                hwnd      = nullptr;
	UINT                bufferCount = 0;
	DXGI_FORMAT         format    = DXGI_FORMAT_UNKNOWN;
	UINT                width     = 0;
	UINT                height    = 0;

	ID3D12DescriptorHeap* rtvHeap   = nullptr;
	UINT                  rtvStride = 0;
	ID3D12Resource*       backBuffers[kMaxFrames] {};

	ID3D12DescriptorHeap* srvHeap = nullptr;

	ID3D12CommandAllocator*    allocators[kMaxFrames] {};
	ID3D12GraphicsCommandList* cmdLists[kMaxFrames] {};

	ID3D12Fence* fence      = nullptr;
	HANDLE       fenceEvent = nullptr;
	UINT64       fenceValues[kMaxFrames] {};
	UINT64       lastFenceValue = 0;

	ImFont*       font       = nullptr;
	std::uint64_t lastTickMs = 0;
	std::uint64_t readyTickMs = 0;      // 叠加层就绪的时刻（提示板延迟用）
	bool          noClientBoardLogged = false;
	float         deltaTime  = 1.0f / 60.0f;
};

GfxState g_gfx;

auto ShutdownGfxUnlocked() noexcept -> void;

#define GFX_LOCK()    if (g_gfx.lockReady) { ::EnterCriticalSection(&g_gfx.lock); }
#define GFX_UNLOCK()  if (g_gfx.lockReady) { ::LeaveCriticalSection(&g_gfx.lock); }

// 等第 idx 号"帧槽"上的 GPU 活干完
auto WaitOneFrame(UINT idx) noexcept -> void {
	if (g_gfx.fence == nullptr || idx >= kMaxFrames || g_gfx.fenceValues[idx] == 0) {
		return;
	}
	if (g_gfx.fence->GetCompletedValue() < g_gfx.fenceValues[idx]) {
		if (SUCCEEDED(g_gfx.fence->SetEventOnCompletion(g_gfx.fenceValues[idx], g_gfx.fenceEvent))) {
			::WaitForSingleObject(g_gfx.fenceEvent, 5000);
		}
	}
	g_gfx.fenceValues[idx] = 0;
}

auto WaitAllFrames() noexcept -> void {
	for (UINT i = 0; i < kMaxFrames; ++i) {
		WaitOneFrame(i);
	}
}

auto ReleaseBackBuffers() noexcept -> void {
	for (UINT i = 0; i < kMaxFrames; ++i) {
		if (g_gfx.backBuffers[i] != nullptr) {
			g_gfx.backBuffers[i]->Release();
			g_gfx.backBuffers[i] = nullptr;
		}
	}
}

// 取后台缓冲并为每个建一个 RTV
auto CreateRenderTargets() noexcept -> bool {
	if (g_gfx.rtvHeap == nullptr || g_gfx.device == nullptr || g_gfx.swapChain == nullptr) {
		return false;
	}
	ReleaseBackBuffers();
	D3D12_CPU_DESCRIPTOR_HANDLE handle = g_gfx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < g_gfx.bufferCount; ++i) {
		ID3D12Resource* buffer = nullptr;
		const HRESULT hr = g_gfx.swapChain->GetBuffer(i, __uuidof(ID3D12Resource),
			reinterpret_cast<void**>(&buffer));
		if (FAILED(hr) || buffer == nullptr) {
			char line[200] {};
			std::snprintf(line, sizeof(line),
				"standalone: swapChain->GetBuffer(%u) failed (hr=0x%08X).",
				static_cast<unsigned>(i), static_cast<unsigned>(hr));
			LogWarn(line);
			return false;
		}
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc {};
		rtvDesc.Format        = g_gfx.format;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		g_gfx.device->CreateRenderTargetView(buffer, &rtvDesc, handle);
		g_gfx.backBuffers[i] = buffer;
		handle.ptr += g_gfx.rtvStride;
	}
	return true;
}

auto LoadFont() noexcept -> ImFont* {
	// 自己建图集 ⇒ 中文和数字天生同一个来源，不存在宿主那种"混排"问题。
	ImGuiIO& io = ImGui::GetIO();
	const char* candidates[] = {
		"C:\\Windows\\Fonts\\msyh.ttc",
		"C:\\Windows\\Fonts\\msyh.ttf",
		"C:\\Windows\\Fonts\\msyhl.ttc",
		"C:\\Windows\\Fonts\\simhei.ttf",
		"C:\\Windows\\Fonts\\simsun.ttc",
	};
	ImFont* font = nullptr;
	for (const char* path : candidates) {
		if (::GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
			continue;
		}
		font = io.Fonts->AddFontFromFileTTF(path, kFontPx, nullptr,
			io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		if (font != nullptr) {
			char line[260] {};
			std::snprintf(line, sizeof(line), "standalone: overlay font loaded from %s.", path);
			LogInfo(line);
			break;
		}
	}
	if (font == nullptr) {
		LogWarn("standalone: no CJK font found in C:\\Windows\\Fonts; falling back to the "
		        "built-in ASCII font (Chinese text will show as boxes).");
		font = io.Fonts->AddFontDefault();
	}
	io.FontDefault = font;
	return font;
}

// 完整初始化：设备 / 队列 / 堆 / 命令对象 / 围栏 / ImGui 上下文
auto InitGfx(IDXGISwapChain3* swapChain, void* queueRaw) noexcept -> bool {
	if (g_gfx.initialized || g_gfx.gaveUp || swapChain == nullptr) {
		return g_gfx.initialized;
	}
	if (queueRaw == nullptr) {
		g_gfx.gaveUp = true;
		LogError("standalone: no command queue was captured; overlay permanently disabled.");
		return false;
	}

	DXGI_SWAP_CHAIN_DESC desc {};
	if (FAILED(swapChain->GetDesc(&desc))) {
		g_gfx.gaveUp = true;
		LogError("standalone: swapChain->GetDesc failed; overlay permanently disabled.");
		return false;
	}
	if (desc.BufferCount == 0 || desc.BufferCount > kMaxFrames) {
		char line[200] {};
		std::snprintf(line, sizeof(line),
			"standalone: swap chain buffer count %u is out of range; overlay disabled.",
			static_cast<unsigned>(desc.BufferCount));
		LogError(line);
		g_gfx.gaveUp = true;
		return false;
	}

	// 下面这几个句柄都提前声明：函数中间用 goto 统一收尾时，C++ 不允许"跳过声明"。
	ID3D12Fence* fence      = nullptr;
	HANDLE       fenceEvent = nullptr;
	ImGuiIO*     ioP        = nullptr;

	// ① 设备：**从游戏的交换链取**，绝不自己 CreateDevice（这是当年崩机的根因之一）。
	ID3D12Device* device = nullptr;
	HRESULT hr = swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&device));
	if (FAILED(hr) || device == nullptr) {
		char line[220] {};
		std::snprintf(line, sizeof(line),
			"standalone: this is not a D3D12 swap chain (GetDevice hr=0x%08X); overlay disabled.",
			static_cast<unsigned>(hr));
		LogError(line);
		g_gfx.gaveUp = true;
		return false;
	}

	// ② 命令队列：确认第 1 步抓到的那个确实是同一个设备下的 D3D12 队列。
	ID3D12CommandQueue* queue = reinterpret_cast<ID3D12CommandQueue*>(queueRaw);
	ID3D12Device* queueDevice = nullptr;
	hr = queue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&queueDevice));
	if (FAILED(hr) || queueDevice != device) {
		char line[240] {};
		std::snprintf(line, sizeof(line),
			"standalone: the captured queue does not belong to the swap chain's device "
			"(hr=0x%08X); overlay disabled (game untouched).", static_cast<unsigned>(hr));
		LogError(line);
		if (queueDevice != nullptr) {
			queueDevice->Release();
		}
		device->Release();
		g_gfx.gaveUp = true;
		return false;
	}
	queueDevice->Release();

	IDXGISwapChain3* sc3 = nullptr;
	hr = swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3));
	if (FAILED(hr) || sc3 == nullptr) {
		LogError("standalone: IDXGISwapChain3 is unavailable; overlay disabled.");
		device->Release();
		g_gfx.gaveUp = true;
		return false;
	}

	// ③ 自己的 RTV 堆（bufferCount 个）与 SRV 堆（1 个，给字体用）
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	{
		D3D12_DESCRIPTOR_HEAP_DESC d {};
		d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		d.NumDescriptors = desc.BufferCount;
		d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		hr = device->CreateDescriptorHeap(&d, __uuidof(ID3D12DescriptorHeap),
			reinterpret_cast<void**>(&rtvHeap));
	}
	if (FAILED(hr) || rtvHeap == nullptr) {
		LogError("standalone: CreateDescriptorHeap(RTV) failed; overlay disabled.");
		sc3->Release();
		device->Release();
		g_gfx.gaveUp = true;
		return false;
	}

	ID3D12DescriptorHeap* srvHeap = nullptr;
	{
		D3D12_DESCRIPTOR_HEAP_DESC d {};
		d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		d.NumDescriptors = 1;
		d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = device->CreateDescriptorHeap(&d, __uuidof(ID3D12DescriptorHeap),
			reinterpret_cast<void**>(&srvHeap));
	}
	if (FAILED(hr) || srvHeap == nullptr) {
		LogError("standalone: CreateDescriptorHeap(SRV) failed; overlay disabled.");
		rtvHeap->Release();
		sc3->Release();
		device->Release();
		g_gfx.gaveUp = true;
		return false;
	}

	// ④ 每帧一套：命令分配器 + 命令列表（各帧槽独立，才能安全复用）
	ID3D12CommandAllocator*    allocators[kMaxFrames] {};
	ID3D12GraphicsCommandList* cmdLists[kMaxFrames] {};
	for (UINT i = 0; i < desc.BufferCount; ++i) {
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
			__uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&allocators[i]));
		if (FAILED(hr) || allocators[i] == nullptr) {
			LogError("standalone: CreateCommandAllocator failed; overlay disabled.");
			goto fail_commands;
		}
		hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i],
			nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&cmdLists[i]));
		if (FAILED(hr) || cmdLists[i] == nullptr) {
			LogError("standalone: CreateCommandList failed; overlay disabled.");
			goto fail_commands;
		}
		cmdLists[i]->Close();   // 建出来就是打开状态，先关掉（每帧画之前再 Reset）
	}

	// ⑤ 围栏 + 事件（用来等 GPU）
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
		reinterpret_cast<void**>(&fence));
	if (FAILED(hr) || fence == nullptr) {
		LogError("standalone: CreateFence failed; overlay disabled.");
		goto fail_pre_fence;
	}
	fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr) {
		LogError("standalone: CreateEvent for the fence failed; overlay disabled.");
		fence->Release();
		goto fail_pre_fence;
	}

	// ⑥ 记状态（先落状态，CreateRenderTargets 要用到）
	g_gfx.device       = device;
	g_gfx.queue        = queue;
	g_gfx.swapChain    = sc3;
	g_gfx.hwnd         = desc.OutputWindow;
	g_gfx.bufferCount  = desc.BufferCount;
	g_gfx.format       = desc.BufferDesc.Format;
	g_gfx.width        = desc.BufferDesc.Width;
	g_gfx.height       = desc.BufferDesc.Height;
	g_gfx.rtvHeap      = rtvHeap;
	g_gfx.rtvStride    = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	g_gfx.srvHeap      = srvHeap;
	g_gfx.fence        = fence;
	g_gfx.fenceEvent   = fenceEvent;
	g_gfx.lastFenceValue = 0;
	for (UINT i = 0; i < kMaxFrames; ++i) {
		g_gfx.allocators[i]  = (i < desc.BufferCount) ? allocators[i] : nullptr;
		g_gfx.cmdLists[i]    = (i < desc.BufferCount) ? cmdLists[i]   : nullptr;
		g_gfx.fenceValues[i] = 0;
	}
	// 先立起 initialized 标志：后面任何一步失败都走 fail_state，
	// 由 ShutdownGfxUnlocked 统一把已经建出来的资源收干净（不留半套）。
	g_gfx.initialized = true;

	if (!CreateRenderTargets()) {
		LogError("standalone: could not create render targets; overlay disabled.");
		goto fail_state;
	}

	// ⑦ 自己的 ImGui 上下文 + Win32/DX12 后端（不碰宿主那份，也就不用管宿主字体混排）
	::ImGui::CreateContext();
	g_gfx.imguiCtxReady = true;
	ioP = &::ImGui::GetIO();
	ioP->IniFilename = nullptr;      // 绝不往游戏目录写 imgui.ini
	ioP->LogFilename = nullptr;
	ioP->ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

	// ★★★ 时序（这一处错一次，面板就永远变英文，2026-09-23 实测踩过）：
	//   客户端（主插件）只在**"布局自检第一次通过"的那一瞬间**去图集里挑一次中文字体
	//   （`g_cjkFont = SafeFindCjkFont(c)` 只执行一次），而那一刻**图集必须已经烘焙好**，
	//   否则它一个中文字都查不到 → 从此所有文案退回英文（Tr() 见 g_cjkFont 为空）。
	//   真 MapSense 那边为什么没事：它的第一个回调打过来时自己后端还没初始化，
	//   自检必然失败 → 一直拖到后面几帧（图集已烘焙）才通过 → 挑得到。
	//   所以我们**必须在设置后端名字之前**先叫客户端的第一次，
	//   让它的自检"像在真 MapSense 上那样先失败一次"。
	{
		const int clients = HostApi::ClientCount();
		char line[300] {};
		std::snprintf(line, sizeof(line),
			"standalone: early call to %d overlay client(s) BEFORE both backends are named and "
			"before the font atlas is built (deliberately: the client must fail its layout "
			"self-check here, so that it only picks a CJK font once the atlas really exists).",
			clients);
		LogInfo(line);
		HostApi::InvokeClients(::ImGui::GetCurrentContext(), nullptr,
			static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
		g_gfx.earlyCallDone = true;
	}

	// 字体：客户端在上面那一下里可能已经往图集里加了中文（主插件的「早注册字体」）。
	// 它加了就直接用它那块（省一半显存，效果也和装真 MapSense 时一致）；
	// 没加（没客户端 / 客户端关了 own_font）才用我们自带的雅黑兜底。
	if (ioP->Fonts->Fonts.Size > 0) {
		g_gfx.font = ioP->Fonts->Fonts[ioP->Fonts->Fonts.Size - 1];
		ioP->FontDefault = g_gfx.font;
		char line[200] {};
		std::snprintf(line, sizeof(line),
			"standalone: using the %d font(s) the client registered; skipping our own CJK font.",
			ioP->Fonts->Fonts.Size);
		LogInfo(line);
	} else {
		g_gfx.font = LoadFont();     // 兜底：自带雅黑图集
	}

	// ★ Win32 后端必须起：客户端（主插件）会校验
	//   io.BackendPlatformName == "imgui_impl_win32"，缺了它人家的布局自检永远不过，
	//   面板就永远不出现（这是它防"两份 imgui 布局不一致"的闸门之一）。
	if (::ImGui_ImplWin32_Init(desc.OutputWindow)) {
		g_gfx.win32Ready = true;
	} else {
		LogWarn("standalone: ImGui_ImplWin32_Init failed; the main plugin's layout self-check "
		        "needs it, so its panel would stay hidden.");
	}

	if (!::ImGui_ImplDX12_Init(device, static_cast<int>(desc.BufferCount), desc.BufferDesc.Format,
			srvHeap, srvHeap->GetCPUDescriptorHandleForHeapStart(),
			srvHeap->GetGPUDescriptorHandleForHeapStart())) {
		LogError("standalone: ImGui_ImplDX12_Init failed; overlay disabled.");
		goto fail_state;
	}
	g_gfx.imguiBackendReady = true;

	g_gfx.lastTickMs = ::GetTickCount64();
	{
		char line[420] {};
		std::snprintf(line, sizeof(line),
			"standalone: overlay host READY -- device=%p queue=%p fmt=%u buffers=%u %ux%u "
			"(our own allocators/lists/RTV heap/fence, submitting to the GAME's queue)",
			static_cast<void*>(device), static_cast<void*>(queue),
			static_cast<unsigned>(desc.BufferDesc.Format), static_cast<unsigned>(desc.BufferCount),
			static_cast<unsigned>(desc.BufferDesc.Width), static_cast<unsigned>(desc.BufferDesc.Height));
		LogInfo(line);
	}
	g_gfx.readyTickMs = ::GetTickCount64();
	return true;

fail_state:
	ShutdownGfxUnlocked();
	return false;

fail_pre_fence:
	for (UINT i = 0; i < kMaxFrames; ++i) {
		if (cmdLists[i] != nullptr)   { cmdLists[i]->Release(); }
		if (allocators[i] != nullptr) { allocators[i]->Release(); }
	}
	srvHeap->Release();
	rtvHeap->Release();
	sc3->Release();
	device->Release();
	g_gfx.gaveUp = true;
	return false;

fail_commands:
	for (UINT i = 0; i < kMaxFrames; ++i) {
		if (cmdLists[i] != nullptr)   { cmdLists[i]->Release(); }
		if (allocators[i] != nullptr) { allocators[i]->Release(); }
	}
	srvHeap->Release();
	rtvHeap->Release();
	sc3->Release();
	device->Release();
	g_gfx.gaveUp = true;
	return false;
}

auto ShutdownGfxUnlocked() noexcept -> void {
	// 幂等：不管走到哪一步失败，这里都把已经建出来的东西收干净。
	if (!g_gfx.initialized && !g_gfx.imguiCtxReady && !g_gfx.imguiBackendReady
	    && g_gfx.device == nullptr && g_gfx.swapChain == nullptr) {
		return;
	}
	WaitAllFrames();
	if (g_gfx.imguiBackendReady) {
		::ImGui_ImplDX12_Shutdown();
		g_gfx.imguiBackendReady = false;
	}
	if (g_gfx.win32Ready) {
		::ImGui_ImplWin32_Shutdown();
		g_gfx.win32Ready = false;
	}
	if (g_gfx.imguiCtxReady) {
		::ImGui::DestroyContext();
		g_gfx.imguiCtxReady = false;
	}
	ReleaseBackBuffers();
	for (UINT i = 0; i < kMaxFrames; ++i) {
		if (g_gfx.cmdLists[i] != nullptr)   { g_gfx.cmdLists[i]->Release();   g_gfx.cmdLists[i] = nullptr; }
		if (g_gfx.allocators[i] != nullptr) { g_gfx.allocators[i]->Release(); g_gfx.allocators[i] = nullptr; }
		g_gfx.fenceValues[i] = 0;
	}
	if (g_gfx.fenceEvent != nullptr) { ::CloseHandle(g_gfx.fenceEvent); g_gfx.fenceEvent = nullptr; }
	if (g_gfx.fence != nullptr)      { g_gfx.fence->Release(); g_gfx.fence = nullptr; }
	if (g_gfx.srvHeap != nullptr)    { g_gfx.srvHeap->Release(); g_gfx.srvHeap = nullptr; }
	if (g_gfx.rtvHeap != nullptr)    { g_gfx.rtvHeap->Release(); g_gfx.rtvHeap = nullptr; }
	if (g_gfx.swapChain != nullptr)  { g_gfx.swapChain->Release(); g_gfx.swapChain = nullptr; }
	if (g_gfx.device != nullptr)     { g_gfx.device->Release(); g_gfx.device = nullptr; }
	g_gfx.queue = nullptr;
	g_gfx.font = nullptr;
	g_gfx.bufferCount = 0;
	g_gfx.lastFenceValue = 0;
	g_gfx.initialized = false;
}

auto ShutdownGfx() noexcept -> void {
	GFX_LOCK();
	ShutdownGfxUnlocked();
	GFX_UNLOCK();
}

// ── 没有任何客户端在画时的提示板 ──
//    平时根本不会出现（主插件一注册成功就被它的面板取代）；
//    只在"没装主插件"或"主插件没能注册上"时露出来 —— 一眼就能看出
//    "叠加层自己是活的，只是没人往里画"，省得靠猜。
auto DrawNoClientBoard() noexcept -> void {
	ImGuiIO& io = ::ImGui::GetIO();
	const float w = 560.0f;
	const float h = 96.0f;
	const float x = (io.DisplaySize.x - w) * 0.5f;
	const float y = 90.0f;

	if (g_gfx.font != nullptr) {
		::ImGui::PushFont(g_gfx.font);
	}
	ImDrawList* dl = ::ImGui::GetBackgroundDrawList();
	dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(14, 20, 28, 210), 10.0f);
	dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(96, 200, 255, 220), 10.0f, 0, 2.0f);
	dl->AddText(ImVec2(x + 18.0f, y + 14.0f), IM_COL32(232, 238, 246, 255),
		"Loot Map 独立叠加层 v0.3.1 已就绪（不需要 MapSense）");
	dl->AddText(ImVec2(x + 18.0f, y + 44.0f), IM_COL32(160, 200, 255, 255),
		"暂时没有插件在画：装上 d2rl-loot-map.dll 后，F7 面板与地图星标会出现在这一层");
	if (g_gfx.font != nullptr) {
		::ImGui::PopFont();
	}
}

// ── 真正画的那一帧（外面还有 SEH 防护罩）──
//    画面里的内容**是客户端（主插件）自己画的**：我们把宿主上下文交给它，
//    它就把设置面板和地图星标画进来。我们只负责开/收 ImGui 帧、把命令提交出去。
auto DrawHostFrame() noexcept -> void {
	if (g_gfx.swapChain == nullptr || g_gfx.rtvHeap == nullptr) {
		return;
	}
	// 这一帧要画到哪个后台缓冲：直接问交换链（flip 模型下它就是"下一个要呈现的"）
	const UINT idx = g_gfx.swapChain->GetCurrentBackBufferIndex();
	if (idx >= g_gfx.bufferCount) {
		return;
	}
	ID3D12CommandAllocator*    alloc = g_gfx.allocators[idx];
	ID3D12GraphicsCommandList* list  = g_gfx.cmdLists[idx];
	ID3D12Resource*            back  = g_gfx.backBuffers[idx];
	if (alloc == nullptr || list == nullptr || back == nullptr) {
		return;
	}

	// 等这一格上次的活干完，才能安全复用它的分配器/命令列表
	WaitOneFrame(idx);

	// Win32 后端在的时候由它填 DisplaySize 与鼠标位置（和真 MapSense 一样）；
	// 不在就退回交换链尺寸，绝不空指针调用它的内部数据。
	if (g_gfx.win32Ready) {
		::ImGui_ImplWin32_NewFrame();
	}
	ImGuiIO& io = ::ImGui::GetIO();
	if (io.DisplaySize.x <= 1.0f || io.DisplaySize.y <= 1.0f) {
		io.DisplaySize = ImVec2(static_cast<float>(g_gfx.width), static_cast<float>(g_gfx.height));
	}
	io.DeltaTime = g_gfx.deltaTime;

	::ImGui_ImplDX12_NewFrame();   // 第一次调用会把字体图集烘焙并上传
	::ImGui::NewFrame();

	// ★★ 在帧内把客户端叫一遍 —— 主插件就是在这个回调里
	//    建窗口、拉滑条、画地图星标的（它内部按帧号去重，一帧只画一次）。
	HostApi::InvokeClients(::ImGui::GetCurrentContext(), nullptr,
		io.DisplaySize.x, io.DisplaySize.y);

	// 没人在画 → 亮一块提示板（延迟几秒，避开主插件注册前的那一小段）
	const std::uint64_t now = ::GetTickCount64();
	if (HostApi::ClientCount() == 0
	    && now - g_gfx.readyTickMs > 4000
	    && !g_gfx.noClientBoardLogged) {
		g_gfx.noClientBoardLogged = true;
		char line[240] {};
		std::snprintf(line, sizeof(line),
			"standalone: the overlay is UP, but no plugin has registered as a client yet "
			"(no d2rl-loot-map.dll loaded?). Showing the standby board. alias_lookups=%u",
			static_cast<unsigned>(HostApi::AliasHitCount()));
		LogInfo(line);
	}
	if (HostApi::ClientCount() == 0 && now - g_gfx.readyTickMs > 4000) {
		DrawNoClientBoard();
	}

	::ImGui::Render();

	// ④ 提交到**游戏自己的队列**（顺序天生正确：游戏画完 → 我们画 → 交画面）
	D3D12_RESOURCE_BARRIER barrier {};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource   = back;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_gfx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtv.ptr += static_cast<SIZE_T>(idx) * g_gfx.rtvStride;

	if (FAILED(alloc->Reset()) || FAILED(list->Reset(alloc, nullptr))) {
		return;
	}
	list->ResourceBarrier(1, &barrier);
	list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	ID3D12DescriptorHeap* heaps[] = { g_gfx.srvHeap };
	list->SetDescriptorHeaps(1, heaps);
	::ImGui_ImplDX12_RenderDrawData(::ImGui::GetDrawData(), list);

	// 画完切回"呈现"（D3D12 规定 Present 之前必须是这个状态）
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
	list->ResourceBarrier(1, &barrier);
	if (FAILED(list->Close())) {
		return;
	}
	ID3D12CommandList* lists[] = { list };
	g_gfx.queue->ExecuteCommandLists(1, lists);

	g_gfx.fenceValues[idx] = ++g_gfx.lastFenceValue;
	g_gfx.queue->Signal(g_gfx.fence, g_gfx.fenceValues[idx]);
	++g_drawnFrames;
}

// ResizeBuffers 之后的重建（与绘制分开，便于 SEH 只包住绘制）
auto HandleNeedRebuild(IDXGISwapChain3* swapChain) noexcept -> bool {
	g_gfx.needRebuild = false;
	DXGI_SWAP_CHAIN_DESC desc {};
	if (FAILED(swapChain->GetDesc(&desc))) {
		return true;   // 拿不到描述就这帧不画，下帧再说
	}
	if (desc.BufferCount != g_gfx.bufferCount || desc.BufferDesc.Format != g_gfx.format) {
		LogInfo("standalone: swap chain format/buffer count changed; rebuilding the overlay "
		        "renderer from scratch.");
		ShutdownGfxUnlocked();
		return InitGfx(swapChain, g_firstQueue.load());
	}
	g_gfx.width  = desc.BufferDesc.Width;
	g_gfx.height = desc.BufferDesc.Height;
	WaitAllFrames();
	if (!CreateRenderTargets()) {
		LogWarn("standalone: could not rebuild render targets after ResizeBuffers; overlay "
		        "disabled (game continues).");
		g_gfx.gaveUp = true;
		return false;
	}
	return true;
}

// Present 里的一帧：锁 → 必要时初始化/重建 → SEH 保护的绘制 → 放锁
auto RenderFrameOnPresent(IDXGISwapChain3* swapChain) noexcept -> void {
	if (!g_active.load(std::memory_order_relaxed) || g_shuttingDown.load()) {
		return;
	}
	// 每帧的时间步长（给 ImGui 动画/滚动用）
	const std::uint64_t now = ::GetTickCount64();
	if (g_gfx.lastTickMs != 0) {
		float dt = static_cast<float>(now - g_gfx.lastTickMs) / 1000.0f;
		if (dt < 0.001f) { dt = 0.001f; }
		if (dt > 0.1f)   { dt = 0.1f; }
		g_gfx.deltaTime = dt;
	}
	g_gfx.lastTickMs = now;

	GFX_LOCK();

	if (!g_gfx.initialized) {
		if (g_gfx.gaveUp || !InitGfx(swapChain, g_firstQueue.load())) {
			GFX_UNLOCK();
			return;
		}
	}
	if (g_gfx.needRebuild) {
		const bool ok = HandleNeedRebuild(swapChain);
		if (!ok || !g_gfx.initialized) {
			GFX_UNLOCK();
			return;
		}
	}

	// ⚠️ 绘制整段套 SEH 防护罩：万一出异常，就地停用绘制，游戏照常跑。
	//    （v0.17.6 的教训：异常一旦逃出去，后果是整条渲染链停用。）
	__try {
		DrawHostFrame();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_gfx.gaveUp = true;
		LogError("standalone: drawing raised a CPU exception; the overlay is now PERMANENTLY "
		         "DISABLED. The game keeps running normally.");
	}

	if (!g_gfx.loggedFirst && g_gfx.gaveUp == false) {
		g_gfx.loggedFirst = true;
		char line[320] {};
		std::snprintf(line, sizeof(line),
			"standalone: first overlay frame drawn -- clients=%d font_atlas=%dx%d "
			"(the main plugin draws its panel and map stars inside this layer).",
			HostApi::ClientCount(),
			::ImGui::GetIO().Fonts->TexWidth, ::ImGui::GetIO().Fonts->TexHeight);
		LogInfo(line);
	}

	GFX_UNLOCK();
}

// ── 工厂侧：游戏建交换链时被叫到 ──

auto STDMETHODCALLTYPE HookCreateSwapChainForHwnd(
	IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
	const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
	IDXGIOutput* output, IDXGISwapChain1** swapChain) noexcept -> HRESULT {
	CreateSwapChainForHwndFn original = g_origCreateSwapChainForHwnd;
	if (original == nullptr) {
		return E_FAIL;
	}
	const HRESULT hr = original(factory, device, hwnd, desc, fullscreen, output, swapChain);
	if (SUCCEEDED(hr) && swapChain != nullptr && *swapChain != nullptr) {
		g_creationPath.store(2);
		NoteFirstBinding("IDXGIFactory2::CreateSwapChainForHwnd", device,
			static_cast<void*>(*swapChain), hwnd);
	}
	return hr;
}

auto STDMETHODCALLTYPE HookCreateSwapChain(
	IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
	IDXGISwapChain** swapChain) noexcept -> HRESULT {
	CreateSwapChainFn original = g_origCreateSwapChain;
	if (original == nullptr) {
		return E_FAIL;
	}
	const HRESULT hr = original(factory, device, desc, swapChain);
	if (SUCCEEDED(hr) && swapChain != nullptr && *swapChain != nullptr) {
		g_creationPath.store(3);
		const HWND hwnd = (desc != nullptr) ? desc->OutputWindow : nullptr;
		NoteFirstBinding("IDXGIFactory::CreateSwapChain", device,
			static_cast<void*>(*swapChain), hwnd);
	}
	return hr;
}

auto STDMETHODCALLTYPE HookCreateSwapChainForCoreWindow(
	IDXGIFactory2* factory, IUnknown* device, IUnknown* window,
	const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output,
	IDXGISwapChain1** swapChain) noexcept -> HRESULT {
	CreateSwapChainForCoreWindowFn original = g_origCreateSwapChainForCoreWindow;
	if (original == nullptr) {
		return E_FAIL;
	}
	const HRESULT hr = original(factory, device, window, desc, output, swapChain);
	if (SUCCEEDED(hr) && swapChain != nullptr && *swapChain != nullptr) {
		g_creationPath.store(1);
		NoteFirstBinding("IDXGIFactory2::CreateSwapChainForCoreWindow", device,
			static_cast<void*>(*swapChain), nullptr);
	}
	return hr;
}

auto STDMETHODCALLTYPE HookCreateSwapChainForComposition(
	IDXGIFactory2* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC1* desc,
	IDXGIOutput* output, IDXGISwapChain1** swapChain) noexcept -> HRESULT {
	CreateSwapChainForCompositionFn original = g_origCreateSwapChainForComposition;
	if (original == nullptr) {
		return E_FAIL;
	}
	const HRESULT hr = original(factory, device, desc, output, swapChain);
	if (SUCCEEDED(hr) && swapChain != nullptr && *swapChain != nullptr) {
		g_creationPath.store(4);
		NoteFirstBinding("IDXGIFactory2::CreateSwapChainForComposition", device,
			static_cast<void*>(*swapChain), nullptr);
	}
	return hr;
}

// ── 呈递侧：每帧都会被叫到（在这里画）──

auto NotePresent(const char* which, IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags) noexcept -> void {
	const std::uint64_t n = g_presentCalls.fetch_add(1) + 1;
	if (n <= 3 || (n % 600) == 0) {
		char line[340] {};
		std::snprintf(line, sizeof(line),
			"standalone: %s #%llu on swapChain=%p sync=%u flags=0x%X (drawn=%llu)",
			which, static_cast<unsigned long long>(n), static_cast<void*>(swapChain),
			static_cast<unsigned>(syncInterval), static_cast<unsigned>(flags),
			static_cast<unsigned long long>(g_drawnFrames.load()));
		LogInfo(line);
	}
}

auto STDMETHODCALLTYPE HookPresent(
	IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags) noexcept -> HRESULT {
	PresentFn original = g_origPresent;
	if (original == nullptr) {
		return E_FAIL;
	}
	NotePresent("Present", swapChain, syncInterval, flags);
	RenderFrameOnPresent(swapChain);
	return original(swapChain, syncInterval, flags);
}

auto STDMETHODCALLTYPE HookPresent1(
	IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags,
	const DXGI_PRESENT_PARAMETERS* params) noexcept -> HRESULT {
	Present1Fn original = g_origPresent1;
	if (original == nullptr) {
		return E_FAIL;
	}
	NotePresent("Present1", swapChain, syncInterval, flags);
	RenderFrameOnPresent(swapChain);
	return original(swapChain, syncInterval, flags, params);
}

auto STDMETHODCALLTYPE HookResizeBuffers(
	IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
	DXGI_FORMAT format, UINT flags) noexcept -> HRESULT {
	ResizeBuffersFn original = g_origResizeBuffers;
	if (original == nullptr) {
		return E_FAIL;
	}
	char line[280] {};
	std::snprintf(line, sizeof(line),
		"standalone: ResizeBuffers(%u, %ux%u, fmt=%u, flags=0x%X) -- releasing our back-buffer "
		"references first (otherwise the game's own resize would fail).",
		static_cast<unsigned>(bufferCount), static_cast<unsigned>(width),
		static_cast<unsigned>(height), static_cast<unsigned>(format),
		static_cast<unsigned>(flags));
	LogInfo(line);

	// ★ 关键：DXGI 规定"只要还有任何人持着后台缓冲的引用，ResizeBuffers 就会失败"。
	//   我们为了画 RTV 持着引用，所以必须先松开、并等 GPU 把我们的活干完。
	GFX_LOCK();
	if (g_gfx.initialized) {
		WaitAllFrames();
		ReleaseBackBuffers();
		g_gfx.needRebuild = true;
	}
	GFX_UNLOCK();

	const HRESULT hr = original(swapChain, bufferCount, width, height, format, flags);
	if (FAILED(hr)) {
		char fail[220] {};
		std::snprintf(fail, sizeof(fail),
			"standalone: the game's ResizeBuffers returned 0x%08X (our references were already "
			"released, so this is the game's own business).", static_cast<unsigned>(hr));
		LogWarn(fail);
	}
	return hr;
}

// ─────────────────────────── 装钩子 ───────────────────────────

auto InstallFactoryHooks() noexcept -> bool {
	// 只为"读虚表"创建一个 DXGI 工厂 —— 不创建设备、不建交换链，代价极小。
	// 所有 IDXGIFactory 派生实例共用同一张虚表，所以从它读到的函数地址
	// 就是游戏那个工厂要调用的函数。
	IDXGIFactory1* factory = nullptr;
	const HRESULT hr = ::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
		reinterpret_cast<void**>(&factory));
	if (FAILED(hr) || factory == nullptr) {
		char line[220] {};
		std::snprintf(line, sizeof(line),
			"standalone: CreateDXGIFactory1 failed (hr=0x%08X); cannot discover the DXGI vtable.",
			static_cast<unsigned>(hr));
		LogError(line);
		return false;
	}
	auto** vtbl = *reinterpret_cast<void***>(factory);

	struct Slot {
		int         group;
		std::size_t index;
		void*       detour;
		void**      original;
		const char* name;
	};
	const Slot slots[] = {
		{ 0, kFactoryCreateSwapChain,               reinterpret_cast<void*>(&HookCreateSwapChain),               reinterpret_cast<void**>(&g_origCreateSwapChain),               "CreateSwapChain" },
		{ 1, kFactoryCreateSwapChainForHwnd,        reinterpret_cast<void*>(&HookCreateSwapChainForHwnd),        reinterpret_cast<void**>(&g_origCreateSwapChainForHwnd),        "CreateSwapChainForHwnd" },
		{ 2, kFactoryCreateSwapChainForCoreWindow,  reinterpret_cast<void*>(&HookCreateSwapChainForCoreWindow),  reinterpret_cast<void**>(&g_origCreateSwapChainForCoreWindow),  "CreateSwapChainForCoreWindow" },
		{ 3, kFactoryCreateSwapChainForComposition, reinterpret_cast<void*>(&HookCreateSwapChainForComposition), reinterpret_cast<void**>(&g_origCreateSwapChainForComposition), "CreateSwapChainForComposition" },
	};

	int installed = 0;
	for (const Slot& s : slots) {
		void* target = vtbl[s.index];
		if (target == nullptr) {
			continue;
		}
		const MH_STATUS st = MH_CreateHook(target, s.detour, s.original);
		if (st == MH_OK) {
			g_factoryTarget[s.group] = target;
			g_factoryHooked[s.group] = true;
			++installed;
		} else {
			char line[260] {};
			std::snprintf(line, sizeof(line), "standalone: MH_CreateHook(%s @ %p) failed: %s",
				s.name, target, StatusText(st));
			LogWarn(line);
		}
	}
	factory->Release();
	char line[220] {};
	std::snprintf(line, sizeof(line),
		"standalone: DXGI factory hooks created = %d/4 (CreateSwapChain / ForHwnd / ForCoreWindow / ForComposition).",
		installed);
	LogInfo(line);
	return installed > 0;
}

auto InstallSwapChainHooks(void* swapChain) noexcept -> bool {
	if (swapChain == nullptr) {
		return false;
	}
	if (g_swapChainHooked) {
		return true;
	}
	// 只允许一个线程来装（后台线程与加载器 UI 线程路径都可能走到这里）
	if (g_installClaimed.exchange(true)) {
		return g_swapChainHooked;
	}

	auto** vtbl = *reinterpret_cast<void***>(swapChain);
	void* present       = vtbl[kSwapChainPresent];
	void* present1      = vtbl[kSwapChainPresent1];
	void* resizeBuffers = vtbl[kSwapChainResizeBuffers];
	char line[360] {};
	std::snprintf(line, sizeof(line),
		"standalone: arming swap-chain hooks from its vtable (Present=%p Present1=%p ResizeBuffers=%p).",
		present, present1, resizeBuffers);
	LogInfo(line);

	bool ok = false;
	if (present != nullptr) {
		const MH_STATUS st = MH_CreateHook(present, reinterpret_cast<void*>(&HookPresent),
			reinterpret_cast<void**>(&g_origPresent));
		if (st == MH_OK) {
			g_presentTarget = present;
			ok = true;
		} else {
			std::snprintf(line, sizeof(line), "standalone: MH_CreateHook(Present @ %p) failed: %s",
				present, StatusText(st));
			LogWarn(line);
		}
	}
	if (present1 != nullptr) {
		const MH_STATUS st = MH_CreateHook(present1, reinterpret_cast<void*>(&HookPresent1),
			reinterpret_cast<void**>(&g_origPresent1));
		if (st == MH_OK) {
			g_present1Target = present1;
		} else {
			std::snprintf(line, sizeof(line), "standalone: MH_CreateHook(Present1 @ %p) failed: %s",
				present1, StatusText(st));
			LogWarn(line);
		}
	}
	if (resizeBuffers != nullptr) {
		const MH_STATUS st = MH_CreateHook(resizeBuffers, reinterpret_cast<void*>(&HookResizeBuffers),
			reinterpret_cast<void**>(&g_origResizeBuffers));
		if (st == MH_OK) {
			g_resizeTarget = resizeBuffers;
		} else {
			std::snprintf(line, sizeof(line),
				"standalone: MH_CreateHook(ResizeBuffers @ %p) failed: %s",
				resizeBuffers, StatusText(st));
			LogWarn(line);
		}
	}
	g_swapChainHooked = ok;
	if (ok) {
		const MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
		std::snprintf(line, sizeof(line),
			"standalone: swap-chain hooks armed, MH_EnableHook -> %s. "
			"From here every frame passes through us.",
			StatusText(st));
		LogInfo(line);
	} else {
		LogError("standalone: could not arm the Present hook; staying in logging-only mode.");
	}
	return ok;
}

// ─────────────────────── 自带的后台重试线程 ───────────────────────
//  v0.1.0 用加载器的 ThreadService::runOnUiThread 排队等交换链，实测那个回调
//  没有被调度到（日志里完全没有后续行）。v0.1.1 改成自己起一个短命线程去等：
//  每 100 ms 看一眼拿到的交换链，拿到就装 Present/ResizeBuffers 钩子，然后退出。
auto __stdcall WorkerMain(void* /*param*/) noexcept -> unsigned {
	LogInfo("standalone: retry worker started (self-owned thread); polling for the game's swap chain.");
	for (int i = 0; i < 1800; ++i) {          // 最多约 3 分钟
		if (g_shuttingDown.load(std::memory_order_relaxed)) {
			LogInfo("standalone: retry worker leaving (plugin is unloading).");
			return 0;
		}
		void* sc = g_firstSwapChain.load(std::memory_order_relaxed);
		if (sc != nullptr) {
			char line[200] {};
			std::snprintf(line, sizeof(line),
				"standalone: retry worker saw the swap chain %p at attempt %d; arming now.",
				sc, i + 1);
			LogInfo(line);
			(void)InstallSwapChainHooks(sc);
			return 0;
		}
		if (i < 3 || (i % 100) == 0) {        // 只在开头和每 ~10 秒记一行
			char line[180] {};
			std::snprintf(line, sizeof(line),
				"standalone: retry worker waiting for a swap chain (attempt %d).", i + 1);
			LogInfo(line);
		}
		::Sleep(100);
	}
	LogWarn("standalone: retry worker gave up waiting for a swap chain (no Present hook armed).");
	return 0;
}

auto StartWorker() noexcept -> void {
	const HANDLE h = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &WorkerMain, nullptr, 0, nullptr));
	g_worker.store(h);
	if (h == nullptr) {
		LogWarn("standalone: could not start the retry worker thread; falling back to the "
		        "loader's UI-thread queue only.");
	}
}

// 备份路径：加载器的 UI 线程排队（v0.1.0 实测没被调度，但留着无害）
auto PollTick(const D2RL::PluginContext* ctx, void* /*userData*/) noexcept -> void {
	if (!g_active.load(std::memory_order_relaxed) || g_shuttingDown.load()) {
		return;
	}
	void* sc = g_firstSwapChain.load();
	if (sc != nullptr && !g_swapChainHooked) {
		(void)InstallSwapChainHooks(sc);
	}
}

auto Install(const D2RL::PluginContext* context) noexcept -> void {
	if (context == nullptr) {
		return;
	}
	// MapSense 在场 ⇒ 立刻收手：两个叠加层钩同一批函数会互相打架。
	if (::GetModuleHandleW(L"d2rl-ruffneckk-mapsense.dll") != nullptr) {
		LogWarn("standalone: RuffnecKk MapSense is present -- this standalone overlay build stays "
		        "DISABLED (the main d2rl-loot-map.dll draws inside MapSense's layer). Nothing was hooked.");
		return;
	}

	::InitializeCriticalSection(&g_gfx.lock);
	g_gfx.lockReady = true;

	// 叠加层宿主（客户端登记表 + 模块名别名）先接上日志
	HostApi::SetLogger(&LogInfo);

	const MH_STATUS init = MH_Initialize();
	if (init != MH_OK) {
		char line[200] {};
		std::snprintf(line, sizeof(line), "standalone: MH_Initialize failed: %s", StatusText(init));
		LogError(line);
		return;
	}
	if (!InstallFactoryHooks()) {
		LogError("standalone: no DXGI factory hook could be installed; giving up (game untouched).");
		(void)MH_Uninitialize();
		return;
	}
	// ★ 让主插件按 MapSense 的模块名能找到我们这一层（极窄的别名钩子，见 host_api.cpp）。
	//   失败也不影响叠加层本身，只是主插件找不到宿主（它会照旧退回原生面板并写日志）。
	(void)HostApi::InstallModuleAliasHooks();

	const MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
	char line[240] {};
	std::snprintf(line, sizeof(line),
		"standalone: MH_EnableHook(factory) -> %s; waiting for the game to create its swap chain.",
		StatusText(st));
	LogInfo(line);
	g_active.store(true);

	// 主路径：自己的后台线程；备份路径：加载器的 UI 线程排队。
	StartWorker();

	const D2RL::ThreadService* threads = nullptr;
	if (context->QueryService(&threads) == D2RL::ServiceQueryResult::Success && threads != nullptr
	    && threads->runOnUiThread != nullptr) {
		const auto r = threads->runOnUiThread(context, &PollTick, nullptr);
		std::snprintf(line, sizeof(line),
			"standalone: backup path -- loader runOnUiThread queued (result=%u).",
			static_cast<unsigned>(r));
		LogInfo(line);
	} else {
		LogWarn("standalone: ThreadService unavailable (the self-owned worker is the only path).");
	}

	LogInfo("standalone: v0.3.1 running -- this build IS the overlay layer (it implements the "
	        "MapSense overlay host API). The main plugin draws its panel and map stars here; "
	        "nothing needs to be changed in the main plugin.");
}

auto Uninstall() noexcept -> void {
	if (!g_active.exchange(false)) {
		return;
	}
	// ① 先把后台线程叫停并等它退出 —— 它可能正在调 InstallSwapChainHooks。
	g_shuttingDown.store(true);
	const HANDLE h = g_worker.exchange(nullptr);
	if (h != nullptr) {
		const DWORD w = ::WaitForSingleObject(h, 1500);
		char line[160] {};
		std::snprintf(line, sizeof(line),
			"standalone: retry worker joined (wait=%s).",
			(w == WAIT_OBJECT_0) ? "signalled" : "timed out");
		LogInfo(line);
		::CloseHandle(h);
	}
	// ② 摘钩子：按"安装的反向顺序"，MH_RemoveHook 收**目标函数地址**。
	(void)MH_DisableHook(MH_ALL_HOOKS);
	HostApi::RemoveModuleAliasHooks();   // 别名钩子（GetModuleHandleW / ExW）
	if (g_presentTarget != nullptr) {
		(void)MH_RemoveHook(g_presentTarget);
		g_presentTarget = nullptr;
	}
	if (g_present1Target != nullptr) {
		(void)MH_RemoveHook(g_present1Target);
		g_present1Target = nullptr;
	}
	if (g_resizeTarget != nullptr) {
		(void)MH_RemoveHook(g_resizeTarget);
		g_resizeTarget = nullptr;
	}
	for (int i = 3; i >= 0; --i) {
		if (g_factoryHooked[i] && g_factoryTarget[i] != nullptr) {
			(void)MH_RemoveHook(g_factoryTarget[i]);
			g_factoryTarget[i] = nullptr;
			g_factoryHooked[i] = false;
		}
	}
	// ③ 钩子摘干净了，才轮到释放 GPU 资源（先等 GPU 把我们最后几帧干完）
	ShutdownGfx();
	(void)MH_Uninitialize();
	if (g_gfx.lockReady) {
		::DeleteCriticalSection(&g_gfx.lock);
		g_gfx.lockReady = false;
	}
	LogInfo("standalone: all hooks removed, GPU resources released, MinHook uninitialized "
	        "(game left as found).");
}

}   // namespace

// ─────────────────────────── 插件入口 ───────────────────────────

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	context->LogInfo("Loot Map standalone 0.3.1 loading ... (self-contained OVERLAY HOST for "
	                 "machines WITHOUT MapSense: it publishes the MapSense overlay host API, so "
	                 "the unmodified main plugin draws its panel and map stars inside this layer)");
	Install(context);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Uninstall();
	g_context = nullptr;
}

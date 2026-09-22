// ===========================================================================
//  RenderHost 实现 —— 见 render_host.h
//
//  v0.3.1：不再依赖"工厂 CreateSwapChain 虚表钩子"（实测被先加载的
//  MapSense 拦截/接管，永远等不到游戏建交换链）。改为"事后抓取"：
//    1. 后台线程自己创建一个迷你 D3D12 设备 + 命令队列 + 隐藏窗口交换链
//    2. 系统运行时的同类 COM 对象共享同一张虚表 —— 把这张虚表的
//       Present(8) / Present1(17) / ExecuteCommandLists(7) 换掉，
//       游戏自己的交换链/命令队列立刻生效
//    3. 游戏第一次 Present 时：从交换链拿设备、从最近一次
//       ExecuteCommandLists 拿命令队列 → 初始化 ImGui
//  无论游戏先建后建交换链、无论 MapSense 怎么包，这条路都通。
// ===========================================================================

#include "render_host.h"

#include <windows.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3d12.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <utility>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

// ───────────────────────── 状态 ─────────────────────────

enum class HostState : int {
	WaitingForSwapChain = 0,   // 抓取钩子已装好，等游戏自己 Present 过来
	Ready               = 1,   // ImGui 已在游戏窗口上运行
	Failed              = 2,   // 初始化失败 / 设备移除，不再尝试
};

std::atomic<HostState> g_state { HostState::WaitingForSwapChain };
std::atomic<bool>      g_panelOpen { false };
std::atomic<bool>      g_adopting { false };
RenderHost::UiCallback g_uiCallback = nullptr;

RenderHost::LogFn g_log = nullptr;   // 插件注入的日志函数（可为空）

char g_statusText[192] = "not started";
std::atomic<bool> g_logOnce { false };

// ───────────────────── 抓取到的游戏图形对象 ─────────────────────

ID3D12Device*          g_device    = nullptr;
ID3D12CommandQueue*    g_queue     = nullptr;
HWND                   g_hwnd      = nullptr;
DXGI_FORMAT            g_rtvFormat = DXGI_FORMAT_UNKNOWN;
UINT                   g_bufferCount = 0;

// 迷你"诱饵"对象：只为了拿到系统运行时共享的虚表，创建后一直保持存活
HWND                   g_dummyHwnd    = nullptr;
ID3D12Device*          g_dummyDevice  = nullptr;
ID3D12CommandQueue*    g_dummyQueue   = nullptr;
IDXGISwapChain*        g_dummySwap    = nullptr;

std::atomic<ID3D12CommandQueue*> g_lastQueue { nullptr };   // 最近一次提交命令的 Direct 队列

// ───────────────────── 每帧资源 ─────────────────────

struct FrameCtx {
	ID3D12CommandAllocator*     allocator = nullptr;
	ID3D12GraphicsCommandList*  cmdList   = nullptr;
	ID3D12Resource*             backBuffer = nullptr;
	ID3D12Fence*                fence     = nullptr;
	UINT64                      fenceValue = 0;
	HANDLE                      fenceEvent = nullptr;
};

FrameCtx      g_frames[8];
int           g_numFrames = 0;
ID3D12DescriptorHeap* g_srvHeap = nullptr;   // ImGui 字体贴图用（1 个描述符）
ID3D12DescriptorHeap* g_rtvHeap = nullptr;   // 我们自己的 RTV 描述符堆
bool          g_imguiInited = false;

WNDPROC g_origWndProc = nullptr;
bool    g_prevF7 = false;

// ───────────────────── 小工具 ─────────────────────

auto SetStatus(const char* text) noexcept -> void {
	std::snprintf(g_statusText, sizeof(g_statusText), "%s", text);
	if (g_log != nullptr) {
		g_log(text);
	}
}

auto PatchSlot(void** vtbl, int index, void* detour, void** original) noexcept -> bool {
	if (vtbl == nullptr || original == nullptr) {
		return false;
	}
	*original = vtbl[index];
	if (*original == nullptr) {
		return false;
	}
	DWORD oldProtect = 0;
	if (!VirtualProtect(&vtbl[index], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	vtbl[index] = detour;
	VirtualProtect(&vtbl[index], sizeof(void*), oldProtect, &oldProtect);
	return true;
}

// ───────────────────── 每帧资源管理 ─────────────────────

auto ReleaseFrameResources() noexcept -> void {
	for (int i = 0; i < g_numFrames; ++i) {
		FrameCtx& f = g_frames[i];
		if (f.fenceEvent != nullptr) {
			CloseHandle(f.fenceEvent);
			f.fenceEvent = nullptr;
		}
		if (f.fence != nullptr)      { f.fence->Release();      f.fence = nullptr; }
		if (f.backBuffer != nullptr) { f.backBuffer->Release(); f.backBuffer = nullptr; }
		if (f.cmdList != nullptr)    { f.cmdList->Release();    f.cmdList = nullptr; }
		if (f.allocator != nullptr)  { f.allocator->Release();  f.allocator = nullptr; }
	}
	g_numFrames = 0;
	if (g_rtvHeap != nullptr) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
	if (g_srvHeap != nullptr) { g_srvHeap->Release(); g_srvHeap = nullptr; }
}

// 交换链尺寸/数量变化时重建 RTV 相关资源
auto RefreshBackBuffers(IDXGISwapChain* sc) noexcept -> bool {
	DXGI_SWAP_CHAIN_DESC desc {};
	if (FAILED(sc->GetDesc(&desc))) {
		return false;
	}
	if (desc.BufferCount == g_bufferCount &&
	    g_frames[0].backBuffer != nullptr &&
	    g_rtvFormat == desc.BufferDesc.Format) {
		return true;   // 没变
	}

	for (int i = 0; i < g_numFrames; ++i) {
		if (g_frames[i].backBuffer != nullptr) {
			g_frames[i].backBuffer->Release();
			g_frames[i].backBuffer = nullptr;
		}
	}
	if (g_rtvHeap != nullptr) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
	g_bufferCount = desc.BufferCount;
	g_rtvFormat   = desc.BufferDesc.Format;

	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc {};
	rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDesc.NumDescriptors = g_bufferCount;
	rtvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)))) {
		return false;
	}

	const UINT rtvStep = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < g_bufferCount; ++i) {
		ID3D12Resource* bb = nullptr;
		if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&bb)))) {
			return false;
		}
		D3D12_CPU_DESCRIPTOR_HANDLE cpu { g_rtvHeap->GetCPUDescriptorHandleForHeapStart() };
		cpu.ptr += i * rtvStep;
		g_device->CreateRenderTargetView(bb, nullptr, cpu);
		g_frames[i].backBuffer = bb;   // GetBuffer 返回的是带引用的
	}
	return true;
}

// ───────────────────── ImGui 初始化 ─────────────────────

auto CALLBACK HookWndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT;

auto LoadUiFont() noexcept -> void {
	ImGuiIO& io = ImGui::GetIO();
	const char* candidates[] = {
		"C:\\Windows\\Fonts\\msyh.ttc",     // 微软雅黑（Win10/11 都有，MapSense 同款）
		"C:\\Windows\\Fonts\\msyh.ttf",
		"C:\\Windows\\Fonts\\simhei.ttf",   // 黑体
		"C:\\Windows\\Fonts\\simsun.ttc",   // 宋体（兜底）
	};
	for (const char* path : candidates) {
		if (io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr,
				io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) != nullptr) {
			return;
		}
	}
	// 都没有就保持默认字体（英文界面，不影响功能）
}

auto InitImGui(IDXGISwapChain* sc) noexcept -> bool {
	if (g_device == nullptr || g_queue == nullptr) {
		SetStatus("no D3D12 device/queue captured; overlay disabled");
		return false;
	}

	DXGI_SWAP_CHAIN_DESC scDesc {};
	if (FAILED(sc->GetDesc(&scDesc))) {
		SetStatus("could not query the swap chain");
		return false;
	}
	g_hwnd      = scDesc.OutputWindow;
	g_rtvFormat = scDesc.BufferDesc.Format;

	// 每帧资源
	g_numFrames = static_cast<int>(scDesc.BufferCount);
	if (g_numFrames < 1)  g_numFrames = 1;
	if (g_numFrames > 8)  g_numFrames = 8;
	for (int i = 0; i < g_numFrames; ++i) {
		FrameCtx& f = g_frames[i];
		if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)))) {
			SetStatus("could not create a command allocator");
			return false;
		}
		if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, f.allocator, nullptr, IID_PPV_ARGS(&f.cmdList)))) {
			SetStatus("could not create a command list");
			return false;
		}
		f.cmdList->Close();
		if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f.fence)))) {
			SetStatus("could not create a fence");
			return false;
		}
		f.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (f.fenceEvent == nullptr) {
			SetStatus("could not create a fence event");
			return false;
		}
		f.fenceValue = 0;
		f.backBuffer = nullptr;
	}

	// SRV 堆：只放 ImGui 的字体贴图（1 个描述符）
	D3D12_DESCRIPTOR_HEAP_DESC srvDesc {};
	srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvDesc.NumDescriptors = 1;
	srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap)))) {
		SetStatus("could not create the SRV heap");
		return false;
	}

	// ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;   // 不在游戏目录里落 imgui.ini
	LoadUiFont();

	if (!ImGui_ImplWin32_Init(g_hwnd)) {
		SetStatus("ImGui Win32 startup failed");
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE cpu { g_srvHeap->GetCPUDescriptorHandleForHeapStart() };
	const D3D12_GPU_DESCRIPTOR_HANDLE gpu { g_srvHeap->GetGPUDescriptorHandleForHeapStart() };
	if (!ImGui_ImplDX12_Init(g_device, g_numFrames, g_rtvFormat, g_srvHeap, cpu, gpu)) {
		SetStatus("ImGui DirectX 12 startup failed");
		return false;
	}

	// 窗口消息钩子（转发给 ImGui，面板打开时按需吞输入）
	g_origWndProc = reinterpret_cast<WNDPROC>(
		SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProcThunk)));

	g_imguiInited = true;
	if (!RefreshBackBuffers(sc)) {
		SetStatus("could not wrap the back buffers");
		g_imguiInited = false;
		return false;
	}
	SetStatus("running on the game window");
	return true;
}

// ───────────────────── 面板渲染 ─────────────────────

auto WaitForFrame(int index) noexcept -> void {
	FrameCtx& f = g_frames[index];
	if (f.fence == nullptr || f.fenceEvent == nullptr) {
		return;
	}
	if (f.fence->GetCompletedValue() < f.fenceValue) {
		f.fence->SetEventOnCompletion(f.fenceValue, f.fenceEvent);
		WaitForSingleObject(f.fenceEvent, 2000);
	}
}

auto RenderOverlay(IDXGISwapChain* sc) noexcept -> void {
	// F7 边沿检测：本帧按下、上一帧没按 → 开关面板
	const bool f7Now = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
	if (f7Now && !g_prevF7) {
		g_panelOpen.store(!g_panelOpen.load());
	}
	g_prevF7 = f7Now;

	if (!g_panelOpen.load() || g_uiCallback == nullptr) {
		return;
	}

	if (!RefreshBackBuffers(sc)) {
		return;
	}

	const UINT bufferIndex = [&]() noexcept -> UINT {
		IDXGISwapChain3* sc3 = nullptr;
		UINT idx = 0;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3 != nullptr) {
			idx = sc3->GetCurrentBackBufferIndex();
			sc3->Release();
		}
		return idx;
	}();
	const int  frameIndex  = static_cast<int>(bufferIndex) % g_numFrames;
	FrameCtx&  f           = g_frames[frameIndex];
	WaitForFrame(frameIndex);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	g_uiCallback();

	ImGui::Render();

	if (FAILED(f.allocator->Reset()))     return;
	if (FAILED(f.cmdList->Reset(f.allocator, nullptr))) return;

	// 背板：PRESENT → RENDER_TARGET
	D3D12_RESOURCE_BARRIER barrier {};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource   = f.backBuffer;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	f.cmdList->ResourceBarrier(1, &barrier);

	const UINT rtvStep = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtv { g_rtvHeap->GetCPUDescriptorHandleForHeapStart() };
	rtv.ptr += bufferIndex * rtvStep;
	f.cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	ID3D12DescriptorHeap* heaps[] { g_srvHeap };
	f.cmdList->SetDescriptorHeaps(1, heaps);

	const D3D12_VIEWPORT viewport { 0.0f, 0.0f,
		static_cast<float>(ImGui::GetIO().DisplaySize.x),
		static_cast<float>(ImGui::GetIO().DisplaySize.y), 0.0f, 1.0f };
	f.cmdList->RSSetViewports(1, &viewport);
	D3D12_RECT scissor { 0, 0,
		static_cast<LONG>(ImGui::GetIO().DisplaySize.x),
		static_cast<LONG>(ImGui::GetIO().DisplaySize.y) };
	f.cmdList->RSSetScissorRects(1, &scissor);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), f.cmdList);

	// 背板：RENDER_TARGET → PRESENT（留给游戏）
	std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	f.cmdList->ResourceBarrier(1, &barrier);
	f.cmdList->Close();

	ID3D12CommandList* lists[] { f.cmdList };
	g_queue->ExecuteCommandLists(1, lists);
	++f.fenceValue;
	g_queue->Signal(f.fence, f.fenceValue);
}

// ───────────────────── 收养游戏交换链 ─────────────────────

// 返回 true = 已定论（成功或最终失败），false = 本帧信息不足、下一帧再试
auto AdoptSwapChain(IDXGISwapChain* sc) noexcept -> bool {
	ID3D12Device* device = nullptr;
	sc->GetDevice(IID_PPV_ARGS(&device));
	if (device == nullptr) {
		SetStatus("captured a swap chain, but it has no D3D12 device");
		g_state.store(HostState::Failed);
		return true;
	}

	ID3D12CommandQueue* queue = g_lastQueue.exchange(nullptr);
	if (queue == nullptr) {
		device->Release();
		return false;   // 还没见过游戏提交命令，等下一帧
	}

	g_device = device;
	g_queue  = queue;

	if (InitImGui(sc)) {
		g_state.store(HostState::Ready);
		char line[160] {};
		std::snprintf(line, sizeof(line), "Loot Map overlay ready (%u back buffers).",
			static_cast<unsigned>(g_bufferCount));
		SetStatus(line);
		return true;
	}

	// 初始化失败：彻底放弃（fail-closed）
	ReleaseFrameResources();
	if (g_imguiInited) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		g_imguiInited = false;
	}
	g_device->Release(); g_device = nullptr;
	g_queue->Release();  g_queue  = nullptr;
	g_state.store(HostState::Failed);
	return true;
}

// ───────────────────── Present / 提交 钩子 ─────────────────────

using PFN_Present        = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,  UINT, UINT);
using PFN_Present1       = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using PFN_ExecuteLists   = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

PFN_Present      g_origPresent = nullptr;
PFN_Present1     g_origPresent1 = nullptr;
PFN_ExecuteLists g_origExecuteLists = nullptr;

auto HandleFrame(IDXGISwapChain* sc, UINT flags) noexcept -> void {
	if (sc == nullptr || sc == g_dummySwap) {
		return;
	}
	if (g_state.load() == HostState::WaitingForSwapChain && !g_adopting.exchange(true)) {
		AdoptSwapChain(sc);
		g_adopting.store(false);
	}
	if (g_state.load() == HostState::Ready && !(flags & DXGI_PRESENT_TEST)) {
		RenderOverlay(sc);
	}
}

auto STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags) noexcept -> HRESULT {
	HandleFrame(sc, flags);
	PFN_Present original = g_origPresent;
	return original != nullptr ? original(sc, syncInterval, flags) : E_FAIL;
}

auto STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT syncInterval, UINT flags,
                                    const DXGI_PRESENT_PARAMETERS* params) noexcept -> HRESULT {
	HandleFrame(sc, flags);
	PFN_Present1 original = g_origPresent1;
	return original != nullptr ? original(sc, syncInterval, flags, params) : E_FAIL;
}

auto STDMETHODCALLTYPE HookExecuteCommandLists(ID3D12CommandQueue* self, UINT count,
                                               ID3D12CommandList* const* lists) noexcept -> void {
	if (self != nullptr && self != g_dummyQueue) {
		ID3D12CommandQueue* last = g_lastQueue.load(std::memory_order_relaxed);
		if (last != self && self->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
			self->AddRef();
			last = g_lastQueue.exchange(self);
			if (last != nullptr) {
				last->Release();
			}
		}
	}
	PFN_ExecuteLists original = g_origExecuteLists;
	if (original != nullptr) {
		original(self, count, lists);
	}
}

// ───────────────────── 窗口消息钩子 ─────────────────────

auto CALLBACK HookWndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT {
	if (g_imguiInited) {
		ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
	}
	if (g_imguiInited && g_panelOpen.load()) {
		ImGuiIO& io = ImGui::GetIO();
		const bool mouseMsg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_CAPTURECHANGED;
		const bool keyMsg   = (msg == WM_KEYDOWN || msg == WM_KEYUP ||
		                       msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_CHAR);
		if ((io.WantCaptureMouse && mouseMsg) || (io.WantTextInput && keyMsg)) {
			return 0;   // 面板正在接输入，别漏给游戏
		}
	}
	return g_origWndProc != nullptr ? CallWindowProcW(g_origWndProc, hwnd, msg, wParam, lParam) : 0;
}

// ───────────────────── 延迟加载失败钩子 + 显式预载 ─────────────────────
// v0.3.2 教训：图形 DLL 改成延迟加载后，若在线程里加载失败，延迟加载助手
// 会抛 0xC06D007E 且没人接 → 直接炸掉游戏进程。三层防御：
//   1. 失败钩子：记录是哪个库、错误码多少，并自己再试一次 SYSTEM32 搜索
//   2. 线程开头显式预载全部图形库，任何一个失败 → 优雅放弃面板（地图上色
//      钩子与本线程完全无关，照常工作）
//   3. 整个线程套 __try/__except 兜底，任何意外异常都不允许炸游戏

// 钩子变量需要可写：新 MSVC 的 delayimp.h 默认把它声明成 const
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <delayimp.h>

static FARPROC WINAPI DliFailureHook(unsigned notify, PDelayLoadInfo info) {
	if (notify == dliFailLoadLib && info != nullptr && info->szDll != nullptr) {
		HMODULE m = LoadLibraryExA(info->szDll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m != nullptr) {
			return reinterpret_cast<FARPROC>(m);
		}
		char line[160] {};
		std::snprintf(line, sizeof(line),
			"Loot Map: delay-load of %s failed (win error %lu).",
			info->szDll, static_cast<unsigned long>(info->dwLastError));
		SetStatus(line);
	}
	return nullptr;
}

extern "C" PfnDliHook __pfnDliFailureHook2 = DliFailureHook;

// 显式预载一个库（保持引用不释放，进程生命周期内有效）
static bool PreloadLibrary(const wchar_t* name) noexcept {
	HMODULE m = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (m == nullptr) {
		char line[160] {};
		std::snprintf(line, sizeof(line),
			"Loot Map: could not pre-load %ls (win error %lu); overlay disabled.",
			name, static_cast<unsigned long>(GetLastError()));
		SetStatus(line);
		return false;
	}
	return true;
}

// ───────────────────── 诱饵创建线程 ─────────────────────

auto CreateHiddenWindow() noexcept -> HWND {
	WNDCLASSEXW wc {};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = DefWindowProcW;
	wc.hInstance     = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"D2RLLootMapHostWnd";
	RegisterClassExW(&wc);
	return CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
		WS_OVERLAPPED, 0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
}

auto LateCaptureThreadImpl() noexcept -> void {
	// 显式预载图形库：任何一个加载不出来就放弃面板（绝不抛异常炸游戏）。
	// 预载成功后，延迟加载助手后续解析 D3D12CreateDevice 等导入时也会顺利命中。
	if (!PreloadLibrary(L"d3d12.dll") ||
	    !PreloadLibrary(L"dxgi.dll") ||
	    !PreloadLibrary(L"D3DCOMPILER_47.dll") ||
	    !PreloadLibrary(L"IMM32.dll")) {
		g_state.store(HostState::Failed);
		SetStatus("graphics DLLs unavailable; overlay disabled (map colours still active)");
		return;
	}
	for (int attempt = 1; attempt <= 30; ++attempt) {   // 约 5 分钟的重试窗口
		if (g_state.load() != HostState::WaitingForSwapChain) {
			return;   // 已经上钩（或已失败），诱饵线程收工
		}

		char line[160] {};

		HWND hwnd = CreateHiddenWindow();
		ID3D12Device* device = nullptr;
		ID3D12CommandQueue* queue = nullptr;
		IDXGIFactory2* factory = nullptr;
		IDXGISwapChain1* swap1 = nullptr;

		if (hwnd != nullptr &&
		    SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) && device != nullptr) {
			D3D12_COMMAND_QUEUE_DESC qd {};
			qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			if (SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))) && queue != nullptr &&
			    SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) && factory != nullptr) {
				DXGI_SWAP_CHAIN_DESC1 scd {};
				scd.Width       = 8;
				scd.Height      = 8;
				scd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
				scd.SampleDesc.Count = 1;
				scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				scd.BufferCount = 2;
				scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				factory->CreateSwapChainForHwnd(queue, hwnd, &scd, nullptr, nullptr, &swap1);
			}
		}

		if (swap1 != nullptr) {
			IDXGISwapChain* swap = nullptr;
			if (SUCCEEDED(swap1->QueryInterface(IID_PPV_ARGS(&swap))) && swap != nullptr) {
				g_dummyHwnd   = hwnd;
				g_dummyDevice = device;
				g_dummyQueue  = queue;
				g_dummySwap   = swap;   // 引用被我们持有，永远不释放（进程生命周期）

				// 系统运行时的同类对象共享同一张虚表：
				//   交换链虚表 Present=8、Present1=17；命令队列虚表 ExecuteCommandLists=7
				void** swapVtbl = *reinterpret_cast<void***>(swap);
				void** queueVtbl = *reinterpret_cast<void***>(queue);
				const bool p8  = PatchSlot(swapVtbl, 8,  reinterpret_cast<void*>(&HookPresent),
				                           reinterpret_cast<void**>(&g_origPresent));
				const bool p17 = PatchSlot(swapVtbl, 17, reinterpret_cast<void*>(&HookPresent1),
				                           reinterpret_cast<void**>(&g_origPresent1));
				const bool p7  = PatchSlot(queueVtbl, 7, reinterpret_cast<void*>(&HookExecuteCommandLists),
				                           reinterpret_cast<void**>(&g_origExecuteLists));

				std::snprintf(line, sizeof(line),
					"Loot Map: render hooks armed (Present=%d Present1=%d Execute=%d); "
					"waiting for the game's first frame.", p8 ? 1 : 0, p17 ? 1 : 0, p7 ? 1 : 0);
				SetStatus(line);
				return;
			}
			swap1->Release();
		}

		if (swap1 == nullptr) {
			if (factory != nullptr)  factory->Release();
			if (queue != nullptr)    queue->Release();
			if (device != nullptr)   device->Release();
			if (hwnd != nullptr)     DestroyWindow(hwnd);
			std::snprintf(line, sizeof(line),
				"Loot Map: dummy swap chain attempt %d failed; retrying...", attempt);
			SetStatus(line);
			Sleep(10000);
		}
	}
	SetStatus("could not arm the render hooks; overlay disabled (map colours still active)");
}

// 线程外壳：任何 SEH 异常（含延迟加载失败 0xC06D007E/0xC06D007F）都就地消化，
// 绝不允许把游戏进程炸掉 —— 面板没了可以接受，游戏崩了不行。
auto LateCaptureThread() noexcept -> void {
	__try {
		LateCaptureThreadImpl();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		char line[160] {};
		std::snprintf(line, sizeof(line),
			"Loot Map: unexpected exception 0x%08lX in the render thread; "
			"overlay disabled (map colours still active).",
			static_cast<unsigned long>(GetExceptionCode()));
		SetStatus(line);
		g_state.store(HostState::Failed);
	}
}

} // namespace

// ───────────────────────── 对外接口 ─────────────────────────

namespace RenderHost {

auto Install() noexcept -> void {
	SetStatus("arming render hooks (late capture) ...");
	std::thread([]() noexcept { LateCaptureThread(); }).detach();
}

auto Shutdown() noexcept -> void {
	if (g_imguiInited) {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		g_imguiInited = false;
		ReleaseFrameResources();
	}
	// 虚表和窗口过程不还原：进程生命周期到头了，还原反而有竞态
	g_state.store(HostState::Failed);
}

auto SetLogger(LogFn logger) noexcept -> void {
	g_log = logger;
}

auto SetUiCallback(UiCallback callback) noexcept -> void {
	g_uiCallback = callback;
}

auto TogglePanel() noexcept -> void {
	g_panelOpen.store(!g_panelOpen.load());
}

auto SetPanelOpen(bool open) noexcept -> void {
	g_panelOpen.store(open);
}

auto IsPanelOpen() noexcept -> bool {
	return g_panelOpen.load();
}

auto IsRendererAlive() noexcept -> bool {
	return g_state.load() == HostState::Ready;
}

auto RendererStatusText() noexcept -> const char* {
	return g_statusText;
}

} // namespace RenderHost

// ===========================================================================
//  OverlayHost 实现（独立版自己充当"叠加层宿主"）
//  接口布局与设计说明见 host_api.h —— 那里写清了为什么这么干。
// ===========================================================================

#include "host_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace HostApi {
namespace {

// ───────────────────────────── 日志 ─────────────────────────────

LogFn g_log = nullptr;

auto Log(const char* text) noexcept -> void {
	if (g_log != nullptr && text != nullptr) {
		g_log(text);
	}
}

// ───────────────────── 和主插件对齐的结构体布局 ─────────────────────
//  这些数字来自主插件源码里的注释（静态逆向 MapSense 得来），改一个字节就会
//  让主插件的 ABI 校验把我们拒掉，所以下面全都有 static_assert 钉死。

constexpr std::uint64_t kHostMagic = 0xF401021D19150002ull;
constexpr std::uint32_t kHostAbi   = 2;

struct HostApiBlock {
	std::uint32_t structSize;        // +0x00 = 0x20
	std::uint32_t version;           // +0x04 = 2
	std::uint64_t magic;             // +0x08
	std::uint32_t (*registerClient)(const void*);        // +0x10
	std::uint32_t (*unregisterClient)(const char*);      // +0x18
};
static_assert(sizeof(HostApiBlock) == 0x20, "host api block must be 32 bytes");
static_assert(offsetof(HostApiBlock, structSize) == 0x00, "");
static_assert(offsetof(HostApiBlock, version) == 0x04, "");
static_assert(offsetof(HostApiBlock, magic) == 0x08, "");
static_assert(offsetof(HostApiBlock, registerClient) == 0x10, "");
static_assert(offsetof(HostApiBlock, unregisterClient) == 0x18, "");

using Callback = void(__fastcall*)(const void* ctx, void* userData) noexcept;

struct ClientDesc {
	std::uint32_t structSize;        // +0x00 = 0x48
	std::uint32_t version;           // +0x04 = 2
	const char*   name;              // +0x08
	std::uint64_t magic;             // +0x10
	Callback      callback[5];       // +0x18 .. +0x38
	void*         userData;          // +0x40
};
static_assert(sizeof(ClientDesc) == 0x48, "client desc must be 72 bytes");
static_assert(offsetof(ClientDesc, name) == 0x08, "");
static_assert(offsetof(ClientDesc, magic) == 0x10, "");
static_assert(offsetof(ClientDesc, callback) == 0x18, "");
static_assert(offsetof(ClientDesc, userData) == 0x40, "");

// 回调时交给客户端的上下文（构造在栈上，只在回调期间有效）
struct HostCtx {
	std::uint32_t structSize;        // +0x00 = 0x20
	std::uint32_t version;           // +0x04 = 2
	void*         imguiContext;      // +0x08
	void*         hostObject;        // +0x10
	float         width;             // +0x18
	float         height;            // +0x1C
};
static_assert(sizeof(HostCtx) == 0x20, "host ctx must be 32 bytes");
static_assert(offsetof(HostCtx, imguiContext) == 0x08, "");
static_assert(offsetof(HostCtx, hostObject) == 0x10, "");
static_assert(offsetof(HostCtx, width) == 0x18, "");
static_assert(offsetof(HostCtx, height) == 0x1C, "");

// ───────────────────────── 客户端登记表 ─────────────────────────

constexpr int kMaxClients = 8;

SRWLOCK  g_clientLock = SRWLOCK_INIT;
int      g_clientCount = 0;

struct ClientSlot {
	bool     used = false;
	char     name[64] {};
	Callback callback[5] {};
	void*    userData = nullptr;
};
ClientSlot g_clients[kMaxClients];

// 宿主单例对象（客户端只是记一笔，我们给个稳定地址就行）
int g_hostObjectTag = 0x4C4D53;

auto CopyName(char* dst, std::size_t dstSize, const char* src) noexcept -> void {
	if (dstSize == 0) {
		return;
	}
	dst[0] = '\0';
	if (src == nullptr) {
		return;
	}
	std::size_t i = 0;
	for (; i + 1 < dstSize && src[i] != '\0'; ++i) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

auto SameName(const char* a, const char* b) noexcept -> bool {
	if (a == nullptr || b == nullptr) {
		return false;
	}
	return std::strncmp(a, b, 63) == 0;
}

// ───────────── registerClient / unregisterClient（宿主侧实现）─────────────
//
//  返回 1 = 接受、0 = 拒绝。主插件会在拒绝时每 1.5 秒重试（最多 160 次），
//  所以这里的校验必须严格：校验不过就干脆拒绝，不给它一个半成品宿主。

auto __fastcall HostRegisterClient(const void* descRaw) noexcept -> std::uint32_t {
	const auto* desc = static_cast<const ClientDesc*>(descRaw);
	if (desc == nullptr || desc->structSize < sizeof(ClientDesc)
	    || desc->version != kHostAbi || desc->name == nullptr
	    || desc->magic != kHostMagic) {
		Log("overlay host: registerClient rejected (header mismatch).");
		return 0;
	}
	bool anyCallback = false;
	for (int i = 0; i < 5; ++i) {
		if (desc->callback[i] != nullptr) {
			anyCallback = true;
			break;
		}
	}
	if (!anyCallback) {
		Log("overlay host: registerClient rejected (no callbacks at all).");
		return 0;
	}

	::AcquireSRWLockExclusive(&g_clientLock);
	int slot = -1;
	for (int i = 0; i < kMaxClients; ++i) {
		if (g_clients[i].used && SameName(g_clients[i].name, desc->name)) {
			slot = i;                    // 同名 → 原地更新（重试注册是正常的）
			break;
		}
	}
	if (slot < 0) {
		for (int i = 0; i < kMaxClients; ++i) {
			if (!g_clients[i].used) {
				slot = i;
				break;
			}
		}
	}
	if (slot < 0) {
		::ReleaseSRWLockExclusive(&g_clientLock);
		Log("overlay host: registerClient rejected (client table full).");
		return 0;
	}
	const bool wasUsed = g_clients[slot].used;
	CopyName(g_clients[slot].name, sizeof(g_clients[slot].name), desc->name);
	for (int i = 0; i < 5; ++i) {
		g_clients[slot].callback[i] = desc->callback[i];
	}
	g_clients[slot].userData = desc->userData;
	g_clients[slot].used = true;
	if (!wasUsed) {
		++g_clientCount;
	}
	const int total = g_clientCount;
	::ReleaseSRWLockExclusive(&g_clientLock);

	char line[240] {};
	std::snprintf(line, sizeof(line),
		"overlay host: ACCEPTED client '%s' (%d client(s) now drawing in our layer).",
		desc->name, total);
	Log(line);
	return 1;
}

auto __fastcall HostUnregisterClient(const char* name) noexcept -> std::uint32_t {
	if (name == nullptr) {
		return 0;
	}
	::AcquireSRWLockExclusive(&g_clientLock);
	for (int i = 0; i < kMaxClients; ++i) {
		if (g_clients[i].used && SameName(g_clients[i].name, name)) {
			g_clients[i].used = false;
			g_clients[i].name[0] = '\0';
			for (int k = 0; k < 5; ++k) {
				g_clients[i].callback[k] = nullptr;
			}
			g_clients[i].userData = nullptr;
			--g_clientCount;
			::ReleaseSRWLockExclusive(&g_clientLock);
			char line[200] {};
			std::snprintf(line, sizeof(line), "overlay host: client '%s' left.", name);
			Log(line);
			return 1;
		}
	}
	::ReleaseSRWLockExclusive(&g_clientLock);
	return 0;
}

// 静态的宿主接口块（客户端会长期持有这个指针，所以必须是静态存储）
HostApiBlock g_apiBlock {
	.structSize       = sizeof(HostApiBlock),
	.version          = kHostAbi,
	.magic            = kHostMagic,
	.registerClient   = &HostRegisterClient,
	.unregisterClient = &HostUnregisterClient,
};

}   // namespace

// ─────────────────── 对外：导出函数的实现 ───────────────────
//  注意：主插件是 GetProcAddress(模块, "RuffnecKkMapSenseGetOverlayHostApi") 拿它，
//  名字必须逐字符一致（extern "C" 保证不被 C++ 名字修饰）。

extern "C" __declspec(dllexport) auto __fastcall
RuffnecKkMapSenseGetOverlayHostApi(std::uint32_t abi, std::uint32_t structSize) noexcept
	-> const void* {
	if (abi != kHostAbi || structSize < sizeof(HostApiBlock)) {
		return nullptr;
	}
	static std::atomic<bool> logged { false };
	if (!logged.exchange(true)) {
		Log("overlay host: a plugin asked for the overlay host API -- publishing our layer "
		    "(standalone build; no MapSense needed).");
	}
	return &g_apiBlock;
}

auto SetLogger(LogFn logger) noexcept -> void {
	g_log = logger;
}

auto ClientCount() noexcept -> int {
	::AcquireSRWLockShared(&g_clientLock);
	const int n = g_clientCount;
	::ReleaseSRWLockShared(&g_clientLock);
	return n;
}

auto InvokeClients(void* imguiContext, void* hostObject, float width, float height) noexcept -> void {
	// 先把登记表复制出来（不在持锁状态下调用别人的代码）
	struct Snapshot {
		Callback callback[5];
		void*    userData;
	};
	Snapshot snap[kMaxClients] {};
	int count = 0;
	::AcquireSRWLockShared(&g_clientLock);
	for (int i = 0; i < kMaxClients && count < kMaxClients; ++i) {
		if (!g_clients[i].used) {
			continue;
		}
		for (int k = 0; k < 5; ++k) {
			snap[count].callback[k] = g_clients[i].callback[k];
		}
		snap[count].userData = g_clients[i].userData;
		++count;
	}
	::ReleaseSRWLockShared(&g_clientLock);
	if (count == 0) {
		return;
	}

	HostCtx ctx {};
	ctx.structSize   = sizeof(HostCtx);
	ctx.version      = kHostAbi;
	ctx.imguiContext = imguiContext;
	ctx.hostObject   = (hostObject != nullptr) ? hostObject : &g_hostObjectTag;
	ctx.width        = width;
	ctx.height       = height;

	for (int i = 0; i < count; ++i) {
		for (int k = 0; k < 5; ++k) {
			if (snap[i].callback[k] != nullptr) {
				snap[i].callback[k](&ctx, snap[i].userData);
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  "模块名别名"钩子 —— 让主插件按 MapSense 的名字找到我们
//
//  主插件唯一的找宿主方式：
//      GetModuleHandleW(L"d2rl-ruffneckk-mapsense.dll") → GetProcAddress(..., "…HostApi")
//  我们的 DLL 不叫这个名字 ⇒ 给 GetModuleHandleW / GetModuleHandleExW 装一个
//  **极窄**的钩子：只有"真实模块不存在 + 问的正是这个名字"才把我们自己的句柄
//  回给它。其它名字、其它情况一律原样转发。
//  好处：主插件一个字节都不用改就能用上这一层；装了真 MapSense 的机器上
//  本插件本来就会自我停用，也就永远不会走到这个别名。
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using GetModuleHandleWFn   = HMODULE(WINAPI*)(LPCWSTR);
using GetModuleHandleExWFn = BOOL(WINAPI*)(DWORD, LPCWSTR, HMODULE*);

GetModuleHandleWFn   g_origGetModuleHandleW   = nullptr;
GetModuleHandleExWFn g_origGetModuleHandleExW = nullptr;
void*                g_gmhTarget   = nullptr;
void*                g_gmhexTarget = nullptr;
HMODULE              g_selfModule  = nullptr;
std::atomic<std::uint32_t> g_aliasHits { 0 };
std::atomic<bool>          g_aliasLogged { false };

constexpr wchar_t kHostModuleName[] = L"d2rl-ruffneckk-mapsense.dll";

auto ToLowerW(wchar_t c) noexcept -> wchar_t {
	return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
}

// 只认"基础文件名"，且不区分大小写
auto IsHostModuleName(LPCWSTR name) noexcept -> bool {
	if (name == nullptr) {
		return false;
	}
	const wchar_t* base = name;
	for (const wchar_t* p = name; *p != L'\0'; ++p) {
		if (*p == L'\\' || *p == L'/') {
			base = p + 1;
		}
	}
	for (int i = 0;; ++i) {
		const wchar_t a = ToLowerW(base[i]);
		const wchar_t b = ToLowerW(kHostModuleName[i]);
		if (a != b) {
			return false;
		}
		if (a == L'\0') {
			return true;
		}
	}
}

auto NoteAliasHit() noexcept -> void {
	g_aliasHits.fetch_add(1, std::memory_order_relaxed);
	if (!g_aliasLogged.exchange(true)) {
		Log("overlay host: 'd2rl-ruffneckk-mapsense.dll' was asked for and is NOT installed -- "
		    "answering with OUR module handle so the main plugin finds this overlay layer "
		    "(the real MapSense stays untouched; if you ever install it, this standalone "
		    "disables itself and the real one takes over).");
	}
}

HMODULE WINAPI HookGetModuleHandleW(LPCWSTR name) noexcept {
	GetModuleHandleWFn original = g_origGetModuleHandleW;
	if (original == nullptr) {
		return nullptr;
	}
	HMODULE handle = original(name);
	if (handle == nullptr && g_selfModule != nullptr && IsHostModuleName(name)) {
		NoteAliasHit();
		handle = g_selfModule;
	}
	return handle;
}

BOOL WINAPI HookGetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* out) noexcept {
	GetModuleHandleExWFn original = g_origGetModuleHandleExW;
	if (original == nullptr) {
		return FALSE;
	}
	const BOOL ok = original(flags, name, out);
	if (ok || out == nullptr || g_selfModule == nullptr) {
		return ok;
	}
	// 不做引用计数的查询才敢代答；需要 AddRef 的那种（没带 UNCHANGED_REFCOUNT）
	// 我们无法正确配平引用，宁可维持原样失败。
	if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) != 0
	    || (flags & GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT) == 0) {
		return ok;
	}
	if (IsHostModuleName(name)) {
		NoteAliasHit();
		*out = g_selfModule;
		return TRUE;
	}
	return ok;
}

}   // namespace

auto InstallModuleAliasHooks() noexcept -> bool {
	// 自己是谁：用"从函数地址反查模块"的方式拿（此刻钩子还没装，不会自问自答）
	HMODULE self = nullptr;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
	                              | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        reinterpret_cast<LPCWSTR>(&InstallModuleAliasHooks), &self)
	    || self == nullptr) {
		Log("overlay host: cannot resolve my own module handle; module-name alias disabled.");
		return false;
	}
	g_selfModule = self;

	// 真实实现住在 kernelbase.dll（kernel32 的导出是转发过去的），
	// 钩它才能覆盖到所有调用者（包括走 kernel32 转发进来的）。
	HMODULE source = ::GetModuleHandleW(L"kernelbase.dll");
	if (source == nullptr) {
		source = ::GetModuleHandleW(L"kernel32.dll");
	}
	if (source == nullptr) {
		Log("overlay host: neither kernelbase nor kernel32 found; alias disabled.");
		return false;
	}

	void* gmh   = reinterpret_cast<void*>(::GetProcAddress(source, "GetModuleHandleW"));
	void* gmhex = reinterpret_cast<void*>(::GetProcAddress(source, "GetModuleHandleExW"));

	int installed = 0;
	if (gmh != nullptr) {
		if (MH_CreateHook(gmh, reinterpret_cast<void*>(&HookGetModuleHandleW),
				reinterpret_cast<void**>(&g_origGetModuleHandleW)) == MH_OK) {
			g_gmhTarget = gmh;
			++installed;
		}
	}
	if (gmhex != nullptr) {
		if (MH_CreateHook(gmhex, reinterpret_cast<void*>(&HookGetModuleHandleExW),
				reinterpret_cast<void**>(&g_origGetModuleHandleExW)) == MH_OK) {
			g_gmhexTarget = gmhex;
			++installed;
		}
	}
	if (installed == 0) {
		Log("overlay host: could not install the module-name alias hooks (the main plugin will "
		    "not find this layer; the overlay still works for other clients).");
		return false;
	}
	char line[220] {};
	std::snprintf(line, sizeof(line),
		"overlay host: module-name alias armed (%d hook(s) on %s) -- the main plugin will find "
		"this overlay layer under the MapSense name.",
		installed, (source == ::GetModuleHandleW(L"kernelbase.dll")) ? "kernelbase" : "kernel32");
	Log(line);
	return true;
}

auto RemoveModuleAliasHooks() noexcept -> void {
	if (g_gmhTarget != nullptr) {
		(void)MH_RemoveHook(g_gmhTarget);
		g_gmhTarget = nullptr;
		g_origGetModuleHandleW = nullptr;
	}
	if (g_gmhexTarget != nullptr) {
		(void)MH_RemoveHook(g_gmhexTarget);
		g_gmhexTarget = nullptr;
		g_origGetModuleHandleExW = nullptr;
	}
}

auto AliasHitCount() noexcept -> std::uint32_t {
	return g_aliasHits.load(std::memory_order_relaxed);
}

}   // namespace HostApi

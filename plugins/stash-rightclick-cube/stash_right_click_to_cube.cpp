// stash-rightclick-cube (TAYA)
//
// Ctrl + Right-click on a hovered item moves it directly into the Horadric
// Cube — no need to open the Cube panel.
//
// How it works on D2RLoader 1.2.x (whose ItemInteractionService emits no
// events):
//   1. SharedEventService item-tooltip listener records the last hovered item
//      handle (+ position/time) whenever D2R shows an item tooltip.
//   2. A low-level Windows mouse hook detects Ctrl + right-click while the
//      game window is focused. If a fresh hovered-item record exists, the
//      right-click is swallowed (so the game never sees it — consumables are
//      never used) and a move is queued.
//   3. On the authoritative game thread the plugin verifies the item container
//      (inventory / personal stash / shared stash) and executes an atomic
//      existing-item transaction that moves it into the Cube (automatic
//      placement, collision handled by the loader).

#include <D2RLPlugin/api.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace {
	// Match the ABI accepted by the installed 1.2.1-beta loader. Must stay in
	// sync with the manifest resource in the .rc file.
	constexpr uint32_t kPluginAbiVersion = 3;

	constexpr D2RL::PluginInfo PluginInfo {
		.infoSize    = D2RL::PluginInfoSize,
		.abiVersion  = kPluginAbiVersion,
		.id          = "stash-rightclick-cube",
		.name        = "Stash Right-Click to Cube",
		.version     = "3.1.1",
		.author      = "TAYA",
		.description = "Ctrl+Right-click a hovered item to move it into the Horadric Cube.",
		.flags       = D2RL::PluginFlags::Shared,
	};

	const D2RL::InventoryService* g_inventory = nullptr;
	const D2RL::ItemService*      g_items     = nullptr;
	const D2RL::ThreadService*    g_threads   = nullptr;
	const D2RL::PluginContext*    g_ctx       = nullptr;

	// Hover history: the last few items whose tooltip appeared, newest last.
	// Rapid consecutive clicks need this — the next item's tooltip may lag
	// behind the click. Matching prefers the freshest record at the click
	// position whose item still exists.
	struct HoverSnapshot {
		std::atomic<uint32_t> runtimeId { 0 };
		std::atomic<uint32_t> code       { 0 };
		std::atomic<uint32_t> container  { 0 };
		std::atomic<uint64_t> tickMs     { 0 };
		std::atomic<int32_t>  x          { -1 };
		std::atomic<int32_t>  y          { -1 };
	};
	constexpr uint32_t kHoverSlots = 8;
	HoverSnapshot g_hover[kHoverSlots];
	std::atomic<uint32_t> g_hoverCursor { 0 };

	// Hover record is valid for this long after its tooltip appeared.
	constexpr uint64_t kHoverValidityMs = 2000;
	// Click must happen near the position recorded with the tooltip.
	constexpr int32_t  kHoverMaxDistancePx = 28;

	std::atomic<uint32_t> g_hookThreadId { 0 };
	std::atomic<bool>     g_shutdown { false };
	HHOOK                 g_mouseHook = nullptr;

	struct MoveRequest {
		uint32_t runtimeId;
		uint32_t code;
		// Shared-stash origin: the game's own native quick-move (simulated
		// Ctrl+Left-click) lands the item in the inventory first; this counts
		// game-thread retries until it shows up there.
		uint32_t attempts;
		// Pre-click inventory runtimeIds with the same item code. The newly
		// arrived quick-moved item is the one NOT in this set — this makes
		// rapid consecutive clicks reliable even when the hover snapshot is
		// one click behind (stale runtimeId).
		uint32_t preCount;
		uint32_t preIds[16];
	};

	const char* DescribeItemResult(D2RL::Items::Result result) noexcept {
		switch (result) {
			case D2RL::Items::Result::Success:          return "success";
			case D2RL::Items::Result::InvalidArgument:  return "invalid argument";
			case D2RL::Items::Result::Unsupported:      return "unsupported";
			case D2RL::Items::Result::Unavailable:      return "unavailable";
			case D2RL::Items::Result::Conflict:         return "no space in the Horadric Cube";
			case D2RL::Items::Result::NotFound:         return "item not found";
			case D2RL::Items::Result::Busy:             return "busy";
			case D2RL::Items::Result::OwnerInactive:    return "owner inactive";
			case D2RL::Items::Result::OwnerMismatch:    return "owner mismatch";
			case D2RL::Items::Result::StaleHandle:      return "stale handle";
			case D2RL::Items::Result::CallbackFault:    return "callback fault";
			case D2RL::Items::Result::PolicyRejected:   return "policy rejected";
			case D2RL::Items::Result::NotAuthoritative: return "not authoritative";
			default:                                    return "unknown error";
		}
	}

	// Runs on the authoritative game thread.
	void __cdecl PerformMove(const D2RL::PluginContext* context, void* userData) noexcept {
		auto* request = static_cast<MoveRequest*>(userData);
		if (request == nullptr) {
			return;
		}

		do {
			if (context == nullptr || g_items == nullptr || g_inventory == nullptr
				|| request->runtimeId == 0) {
				break;
			}

			D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
			if (g_inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) {
				context->LogWarn("ctrl-rightclick-cube: could not resolve the local player.");
				break;
			}

			// Re-resolve the live item handle from the hover snapshot.
			struct FindContext {
				uint32_t           runtimeId;
				uint32_t           container;
				D2RL::ItemHandle   found;
			} findContext { request->runtimeId, 0, D2RL::InvalidItemHandle };

			const auto enumerate = +[](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
				auto* find = static_cast<FindContext*>(userData);
				if (find != nullptr && item != nullptr && item->runtimeId == find->runtimeId) {
					find->found = item->handle;
					find->container = static_cast<uint32_t>(item->container);
					return D2RL::Inventory::IterationAction::Stop;
				}
				return D2RL::Inventory::IterationAction::Continue;
			};

			const D2RL::Inventory::ItemFilter filter {
				.structSize    = D2RL::Inventory::ItemFilterSize,
				.flags         = 0,
				.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory)
					| D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::PersonalStash)
					| D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::SharedStash),
				.reserved      = 0,
			};
			const D2RL::Inventory::Result findStatus = g_inventory->forEachInventoryItem(context, player, &filter, enumerate, &findContext);
			const bool foundById = findStatus == D2RL::Inventory::Result::Success && findContext.found != D2RL::InvalidItemHandle;

			if (foundById && findContext.container == static_cast<uint32_t>(D2RL::Items::ItemContainer::SharedStash)) {
				// Shared-stash origin: the cube and the stash are mutually
				// exclusive panels in D2R, and the 1.2.x transaction engine
				// crashes on direct shared-stash moves (unfinished
				// bookkeeping). Wait for the injected native quick-move to
				// land the item in the inventory: retry once per game tick,
				// non-blocking.
				if (request->attempts < 30) {
					request->attempts += 1;
					if (g_threads->runOnGameThread(context, PerformMove, request) == D2RL::Threads::Result::Success) {
						return;
					}
				}
				char message[192] {};
				std::snprintf(message, sizeof(message),
					"ctrl-rightclick-cube: code=%08X stayed in the shared stash - the inventory is likely full. Free one inventory slot and try again.",
					request->code);
				context->LogWarn(message);
				delete request;
				return;
			}

			if (!foundById && request->preCount == 0) {
				char message[128] {};
				std::snprintf(message, sizeof(message),
					"ctrl-rightclick-cube: hovered item code=%08X not found in any accessible container.",
					request->code);
				context->LogWarn(message);
				break;
			}

			if (!foundById) {
				// Rapid-click race: the hover snapshot was one click behind, so
				// its runtimeId is gone (already in the Cube) while the injected
				// quick-move just delivered a NEW same-code item into the
				// inventory. Claim the arrival: any same-code inventory item
				// whose runtimeId was not in the pre-click snapshot.
				struct ClaimContext {
					uint32_t         code;
					const uint32_t*  preIds;
					uint32_t         preCount;
					D2RL::ItemHandle found;
				} claimContext { request->code, request->preIds, request->preCount, D2RL::InvalidItemHandle };

				const auto claim = +[](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
					auto* claim = static_cast<ClaimContext*>(userData);
					if (claim == nullptr || item == nullptr || item->code != claim->code
						|| claim->found != D2RL::InvalidItemHandle) {
						return D2RL::Inventory::IterationAction::Continue;
					}
					for (uint32_t i = 0; i < claim->preCount; ++i) {
						if (claim->preIds[i] == item->runtimeId) {
							return D2RL::Inventory::IterationAction::Continue;
						}
					}
					claim->found = item->handle;
					return D2RL::Inventory::IterationAction::Stop;
				};

				const D2RL::Inventory::ItemFilter inventoryFilter {
					.structSize    = D2RL::Inventory::ItemFilterSize,
					.flags         = 0,
					.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory),
					.reserved      = 0,
				};
				if (g_inventory->forEachInventoryItem(context, player, &inventoryFilter, claim, &claimContext) != D2RL::Inventory::Result::Success
					|| claimContext.found == D2RL::InvalidItemHandle) {
					// Nothing claimed yet - the quick-move may not have landed.
					if (request->attempts < 30) {
						request->attempts += 1;
						if (g_threads->runOnGameThread(context, PerformMove, request) == D2RL::Threads::Result::Success) {
							return;
						}
					}
					char message[160] {};
					std::snprintf(message, sizeof(message),
						"ctrl-rightclick-cube: hovered item code=%08X not found in any accessible container.",
						request->code);
					context->LogWarn(message);
					break;
				}
				findContext.found = claimContext.found;
			}

			D2RL::Items::ExistingItemOperation operation {};
			operation.structSize = D2RL::Items::ExistingItemOperationSize;
			operation.kind       = D2RL::Items::ExistingItemOperationKind::Move;
			operation.item       = findContext.found;
			operation.move.destination.structSize = D2RL::Items::ItemDestinationSize;
			operation.move.destination.container  = D2RL::Items::ItemContainer::Cube;
			operation.move.destination.placement  = D2RL::Items::Placement::Automatic;

			D2RL::Items::ExistingItemTransaction transaction {};
			transaction.structSize     = D2RL::Items::ExistingItemTransactionSize;
			transaction.player         = player;
			transaction.operationCount = 1;
			transaction.operations     = &operation;

			D2RL::Items::ExistingItemTransactionResult result {};
			result.structSize = D2RL::Items::ExistingItemTransactionResultSize;

			const D2RL::Items::Result status = g_items->executeExistingItemTransaction(context, &transaction, &result);
			if (status != D2RL::Items::Result::Success) {
				// Only failures are logged — normal play keeps the log empty.
				char message[160] {};
				std::snprintf(message, sizeof(message),
					"ctrl-rightclick-cube: move failed (%s), item left in place.",
					DescribeItemResult(status));
				context->LogWarn(message);
			}
		} while (false);

		delete request;
	}

	// UnlockSharedStashMoves removed: empirically the 1.2.1 transaction engine
	// performs the shared-stash move but skips its bookkeeping — the game then
	// asserts (SUnitMsg.cpp:635, stat send-bits overflow) and crashes. The
	// capability gate exists because this feature is unfinished in 1.2.x.
	// Shared-stash items therefore take the safe two-hop route instead: the
	// game's own native quick-move (simulated Ctrl+Left-click) into the
	// inventory, then a regular SDK move into the Cube.

	bool IsGameWindowForeground() noexcept {
		const HWND foreground = GetForegroundWindow();
		if (foreground == nullptr) {
			return false;
		}
		DWORD processId = 0;
		GetWindowThreadProcessId(foreground, &processId);
		return processId == GetCurrentProcessId();
	}

	// Finds the freshest hover snapshot near the click position whose record
	// is still valid. Returns -1 when none matches.
	int FindFreshHover(int32_t mouseX, int32_t mouseY) noexcept {
		const uint64_t now = GetTickCount64();
		int best = -1;
		uint64_t bestTick = 0;
		for (uint32_t i = 0; i < kHoverSlots; ++i) {
			const uint32_t runtimeId = g_hover[i].runtimeId.load(std::memory_order_acquire);
			const uint64_t tickMs    = g_hover[i].tickMs.load(std::memory_order_acquire);
			if (runtimeId == 0 || tickMs == 0 || now < tickMs || now - tickMs > kHoverValidityMs) {
				continue;
			}
			const int32_t dx = mouseX - g_hover[i].x.load(std::memory_order_relaxed);
			const int32_t dy = mouseY - g_hover[i].y.load(std::memory_order_relaxed);
			if (dx * dx + dy * dy > kHoverMaxDistancePx * kHoverMaxDistancePx) {
				continue;
			}
			if (tickMs > bestTick) {
				bestTick = tickMs;
				best = static_cast<int>(i);
			}
		}
		return best;
	}

	void __cdecl InjectNativeQuickMove(const D2RL::PluginContext* context, void* userData) noexcept;

	LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept {
		if (nCode == HC_ACTION && wParam == WM_RBUTTONDOWN && !g_shutdown.load(std::memory_order_relaxed)) {
			const MSLLHOOKSTRUCT* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
			if (info != nullptr
				&& (GetKeyState(VK_CONTROL) & 0x8000) != 0
				&& IsGameWindowForeground()
				&& g_threads != nullptr && g_ctx != nullptr) {
				const int slot = FindFreshHover(static_cast<int32_t>(info->pt.x), static_cast<int32_t>(info->pt.y));
				if (slot >= 0) {
					const bool sharedStashOrigin = g_hover[slot].container.load(std::memory_order_relaxed)
						== static_cast<uint32_t>(D2RL::Items::ItemContainer::SharedStash);

					auto* request = new (std::nothrow) MoveRequest {
						g_hover[slot].runtimeId.load(std::memory_order_acquire),
						g_hover[slot].code.load(std::memory_order_relaxed),
						0,
					};
					if (request != nullptr) {
						if (sharedStashOrigin) {
							// Inject the native quick-move click on the UI
							// thread — calling SendInput from inside this
							// low-level hook callback would re-enter the hook
							// chain and stall mouse input (visible stutter).
							if (g_threads->runOnUiThread(g_ctx, InjectNativeQuickMove, request) == D2RL::Threads::Result::Success) {
								return 1; // swallow: the game never sees this right-click
							}
						} else if (g_threads->runOnGameThread(g_ctx, PerformMove, request) == D2RL::Threads::Result::Success) {
							return 1; // swallow: the game never sees this right-click
						}
						delete request;
					}
				}
			}
		}
		return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
	}

	// Dedicated hook thread. Low-level hooks require a message pump on the
	// installing thread, so this thread runs one.
	DWORD WINAPI HookThreadMain(LPVOID) noexcept {
		g_hookThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
		g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);
		if (g_mouseHook == nullptr) {
			return 1;
		}

		MSG message {};
		BOOL pumpResult;
		while (!g_shutdown.load(std::memory_order_relaxed) && (pumpResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		UnhookWindowsHookEx(g_mouseHook);
		g_mouseHook = nullptr;
		return 0;
	}

	// Runs on the UI thread whenever D2R shows an item tooltip. The tooltip
	// item handle is only valid inside this callback, so snapshot its stable
	// identity right now.
	void __cdecl OnItemTooltip(const D2RL::PluginContext* context, D2RL::SharedEvents::ItemTooltipEvent* event, void*) noexcept {
		if (event == nullptr || event->item == D2RL::InvalidItemHandle || context == nullptr || g_items == nullptr) {
			return;
		}

		D2RL::Items::ItemInfo info {};
		info.structSize = D2RL::Items::ItemInfoSize;
		if (g_items->getItemInfo(context, event->item, &info) != D2RL::Items::Result::Success) {
			return;
		}

		POINT point {};
		GetCursorPos(&point);
		const uint32_t slot = g_hoverCursor.fetch_add(1, std::memory_order_relaxed) % kHoverSlots;
		g_hover[slot].runtimeId.store(info.runtimeId, std::memory_order_release);
		g_hover[slot].code.store(info.code, std::memory_order_relaxed);
		g_hover[slot].container.store(static_cast<uint32_t>(info.container), std::memory_order_relaxed);
		// x/y store the MOUSE position at tooltip time (the click position is
		// matched against these), not the item's grid cell.
		g_hover[slot].x.store(static_cast<int32_t>(point.x), std::memory_order_relaxed);
		g_hover[slot].y.store(static_cast<int32_t>(point.y), std::memory_order_relaxed);
		g_hover[slot].tickMs.store(GetTickCount64(), std::memory_order_release);
	}

	// Runs on the UI thread for shared-stash items: injects the native
	// quick-move click (Ctrl is still physically held and the cursor is on the
	// item), then queues the follow-up move on the game thread. Doing this
	// here instead of inside the mouse hook avoids re-entering the low-level
	// hook chain, which used to stall mouse input.
	void __cdecl InjectNativeQuickMove(const D2RL::PluginContext* context, void* userData) noexcept {
		auto* request = static_cast<MoveRequest*>(userData);
		if (request == nullptr) {
			return;
		}

		do {
			if (context == nullptr || g_threads == nullptr) {
				break;
			}
			if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 || !IsGameWindowForeground()) {
				// Ctrl released or focus lost between hook and here — do not
				// inject a plain click (that would pick the item up).
				break;
			}

			// Snapshot the pre-click same-code inventory items so the follow-up
			// move can identify the freshly quick-moved arrival even when the
			// hover snapshot was one click behind (stale runtimeId).
			{
				struct PreContext {
					uint32_t code;
					uint32_t ids[16];
					uint32_t count;
				} pre { request->code, {}, 0 };

				const auto collect = +[](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
					auto* pre = static_cast<PreContext*>(userData);
					if (pre != nullptr && item != nullptr && item->code == pre->code && pre->count < 16) {
						pre->ids[pre->count++] = item->runtimeId;
					}
					return D2RL::Inventory::IterationAction::Continue;
				};

				D2RL::PlayerHandle player = D2RL::InvalidPlayerHandle;
				if (g_inventory->getLocalPlayer(context, &player) == D2RL::Inventory::Result::Success) {
					const D2RL::Inventory::ItemFilter inventoryFilter {
						.structSize    = D2RL::Inventory::ItemFilterSize,
						.flags         = 0,
						.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory),
						.reserved      = 0,
					};
					g_inventory->forEachInventoryItem(context, player, &inventoryFilter, collect, &pre);
					request->preCount = pre.count;
					for (uint32_t i = 0; i < pre.count; ++i) {
						request->preIds[i] = pre.ids[i];
					}
				}
			}

			INPUT inputs[2] {};
			inputs[0].type       = INPUT_MOUSE;
			inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
			inputs[1].type       = INPUT_MOUSE;
			inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
			SendInput(2, inputs, sizeof(INPUT));

			// Follow up on the game thread: once the quick-move lands the item
			// in the inventory, move it into the Horadric Cube.
			if (g_threads->runOnGameThread(context, PerformMove, request) == D2RL::Threads::Result::Success) {
				return;
			}
		} while (false);

		delete request;
	}
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_ctx = context;

	if (context->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
		context->LogWarn("ctrl-rightclick-cube: InventoryService is unavailable.");
		return false;
	}

	if (context->QueryService(&g_items) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
		context->LogWarn("ctrl-rightclick-cube: ItemService is unavailable.");
		return false;
	}

	if (context->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
		context->LogWarn("ctrl-rightclick-cube: ThreadService is unavailable.");
		return false;
	}

	{
		// Keep player-facing logs minimal: only the loaded line and failures.
		// (No capability/diagnostic output in normal builds.)
	}

	const D2RL::SharedEventService* sharedEvents = nullptr;
	if (context->QueryService(&sharedEvents) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasSharedEventServiceField(sharedEvents, D2RL::SharedEventServiceRequiredSize)) {
		context->LogWarn("ctrl-rightclick-cube: SharedEventService is unavailable.");
		return false;
	}

	// Track the hovered item through item tooltips (Description region always
	// renders for every item tooltip).
	const D2RL::SharedEvents::ItemTooltipListener tooltipListener {
		.structSize = D2RL::SharedEvents::ItemTooltipListenerSize,
		.flags      = 0,
		.priority   = 0,
		.slot       = 0,
		.region     = D2RL::SharedEvents::ItemTooltipRegion::Description,
		.position   = D2RL::SharedEvents::ItemTooltipPosition::Top,
		.anchor     = D2RL::SharedEvents::ItemTooltipAnchor::None,
		.fallback   = D2RL::SharedEvents::ItemTooltipFallback::Omit,
		.callback   = OnItemTooltip,
		.userData   = nullptr,
	};
	D2RL::SharedEvents::ListenerHandle tooltipHandle = D2RL::SharedEvents::InvalidHandle;
	if (sharedEvents->registerItemTooltipListener(context, &tooltipListener, &tooltipHandle) != D2RL::SharedEvents::Result::Success) {
		context->LogWarn("ctrl-rightclick-cube: failed to register the tooltip listener.");
		return false;
	}

	// Unlock direct shared-stash moves in the loader's transaction engine.
	// UnlockSharedStashMoves(context);

	// Start the low-level mouse hook thread.
	HANDLE thread = CreateThread(nullptr, 0, HookThreadMain, nullptr, 0, nullptr);
	if (thread == nullptr) {
		context->LogWarn("ctrl-rightclick-cube: failed to start the mouse hook thread.");
		return false;
	}
	CloseHandle(thread);

	context->LogInfo("ctrl-rightclick-cube: loaded. Ctrl+Right-click a hovered item to move it into the Horadric Cube.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	g_shutdown.store(true, std::memory_order_relaxed);
	const DWORD threadId = g_hookThreadId.load();
	if (threadId != 0) {
		PostThreadMessageW(threadId, WM_QUIT, 0, 0);
	}
	// The hook thread unhooks itself before exiting.
	g_inventory = nullptr;
	g_items     = nullptr;
	g_threads   = nullptr;
	g_ctx       = nullptr;
	for (uint32_t i = 0; i < kHoverSlots; ++i) {
		g_hover[i].runtimeId.store(0);
		g_hover[i].tickMs.store(0);
	}
}

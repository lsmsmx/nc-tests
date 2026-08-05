#include <diva.h>
#include <hooks.h>
#include <nc_log.h>
#include <nc_state.h>
#include <util.h>
#include "common.h"
#include "result.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static void PC_LOG(const char* format, ...) {
    static char logPath[MAX_PATH] = {0};
    if (logPath[0] == 0) {
        HMODULE hMod;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&PC_LOG, &hMod);
        GetModuleFileNameA(hMod, logPath, MAX_PATH);
        char* lastSlash = strrchr(logPath, '\\');
        if (lastSlash) *lastSlash = '\0';
        strcat_s(logPath, MAX_PATH, "\\nc_pc_debug_log.txt");
    }

    FILE* f = fopen(logPath, "a");
    if (!f) return;
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

struct CAetController
{
	void** vftable;
	AetArgs args;
};

struct CStageResultAetController_InLoopOut
{
	void** vftable;
	uint8_t gap0[0x1F8];
	prj::string in_name;
	prj::string lp_name;
	prj::string out_name;
};

static prj::string GetModeLayerName(int32_t kind, bool no_fail)
{
	static const char* styles[4] = { "", "console", "mixed", "max" };
	static const char* kinds[3] = { "in", "loop", "out" };

	return prj::string("nsw_mode_tit_")
		+ styles[state.GetGameStyle()]
		+ "_"
		+ (no_fail ? "nofail_" : "")
		+ kinds[kind]
		+ GetLanguageSuffix().c_str();
}

static prj::string GetWindowLayerName(int32_t kind)
{
	static const char* kinds[4] = { "slide in", "slide loop", "slide_out", "count" };

	return prj::string("nsw_win_arcard_")
		+ kinds[kind]
		+ (GetGameLocale() == GameLocale_JP ? "_jp" : "_en");
}

static bool CheckLayerNameMatchesNC(std::string_view name)
{
	static const char* checks[2] = { "nsw_win_arcard_slide", "nsw_mode_tit" };
	for (int32_t i = 0; i < 2; i++)
	{
		if (util::Contains(name, checks[i]))
			return true;
	}

	return false;
}

static bool patch_scene = false;

HOOK(uint32_t, __fastcall, CStageResultAetControllerInLoopOutGetSceneID, 0x14064BC50, void* a1) 
{
    void** vtable = a1 ? *(void***)a1 : nullptr;
    PC_LOG("[GetSceneID] Enter. a1 = %p, vtable = %p", a1, vtable);
    if (a1)
    {
        uint32_t* ptr = (uint32_t*)a1;
        PC_LOG("[GetSceneID] MemDump: [0x00]=%p | [0x08]=0x%08X | [0x0C]=0x%08X | [0x10]=0x%08X | [0x14]=0x%08X | [0x18]=0x%08X | [0x1C]=0x%08X",
               vtable, ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);
    }
	uint32_t id = originalCStageResultAetControllerInLoopOutGetSceneID(a1);
    PC_LOG("[GetSceneID] Exit. Orig ID = %u, Patch Scene = %d, Returned ID = %u", id, patch_scene, patch_scene ? results::AetSceneID : id);
	return patch_scene ? results::AetSceneID : id;
}

HOOK(void, __fastcall, CAetControllerInLoopOutSetLayer, 0x14065E7D0, void* a1, const prj::string* in, const prj::string* loop, const prj::string* out, int32_t prio) 
{
    void** vtable = a1 ? *(void***)a1 : nullptr;
    PC_LOG("[SetLayerInLoopOut] Enter. a1 = %p, vtable = %p, prio = %d", a1, vtable, prio);
    PC_LOG("[SetLayerInLoopOut] Args -> In = '%s', Loop = '%s', Out = '%s'",
           in ? in->c_str() : "NULL", loop ? loop->c_str() : "NULL", out ? out->c_str() : "NULL");

	prj::string patched_in = *in;
	prj::string patched_lp = *loop;
	prj::string patched_out = *out;

	if (util::StartsWith(*in, "mode_tit_arcade") && util::StartsWith(*loop, "mode_tit_arcade"))
	{
		if (state.GetGameStyle() != GameStyle_Arcade)
		{
			bool no_fail = util::Contains(*in, "comp") || util::Contains(*loop, "comp");
			patched_in = GetModeLayerName(0, no_fail);
			patched_lp = GetModeLayerName(1, no_fail);
		}
	}
	else if (util::StartsWith(*in, "win_arcard") && util::StartsWith(*loop, "win_arcard"))
	{
		if (nc::ShouldUseConsoleStyleWin())
		{
			patched_in = GetWindowLayerName(0);
			patched_lp = GetWindowLayerName(1);
			patched_out = GetWindowLayerName(2);
		}
	}
	
    PC_LOG("[SetLayerInLoopOut] Patched -> In = '%s', Loop = '%s', Out = '%s'", patched_in.c_str(), patched_lp.c_str(), patched_out.c_str());
	originalCAetControllerInLoopOutSetLayer(a1, &patched_in, &patched_lp, &patched_out, prio);
    PC_LOG("[SetLayerInLoopOut] Exit");
}

HOOK(void, __fastcall, CStageResultAetControllerSetLayer, 0x14065DBA0, void* a1, prj::string* name, int32_t prio, int32_t action) 
{
    void** vtable = a1 ? *(void***)a1 : nullptr;
    PC_LOG("[SetLayer] Enter. a1 = %p, vtable = %p, name = '%s', prio = %d, action = %d",
           a1, vtable, name ? name->c_str() : "NULL", prio, action);
	patch_scene = CheckLayerNameMatchesNC(*name);
    PC_LOG("[SetLayer] Match check. patch_scene = %d", patch_scene);
	originalCStageResultAetControllerSetLayer(a1, name, prio, action);
	patch_scene = false;
    PC_LOG("[SetLayer] Exit");
}

HOOK(void, __fastcall, CAetControllerGetLayout, 0x14065E200, CAetController* a1, void* a2, void* a3) 
{
    void** vtable = a1 ? *(void***)a1 : nullptr;
    const char* layer_name = a1 ? a1->args.layer_name : "NULL";
    PC_LOG("[GetLayout] Enter. a1 = %p, vtable = %p, a2 = %p, a3 = %p, layer_name = '%s'",
           a1, vtable, a2, a3, layer_name);
	patch_scene = CheckLayerNameMatchesNC(layer_name);
    PC_LOG("[GetLayout] Match check. patch_scene = %d", patch_scene);
	originalCAetControllerGetLayout(a1, a2, a3);
	patch_scene = false;
    PC_LOG("[GetLayout] Exit");
}

HOOK(bool, __fastcall, StageResultSwitchInit, 0x14064C0E0, void* a1)  
{
    PC_LOG("[Init] Enter. a1 = %p", a1);
	state.ui.ResetAllLayers();
	prj::string out;
	prj::string_view strv;
	aet::LoadAetSet(results::AetSetID, &out);
	spr::LoadSprSet(results::SprSetID, &strv);
    PC_LOG("[Init] Loading requested Assets: AetSetID = %u, SprSetID = %u", results::AetSetID, results::SprSetID);
	bool ret = originalStageResultSwitchInit(a1);
    PC_LOG("[Init] Exit. Return = %d", ret);
    return ret;
}

HOOK(void, __fastcall, StageResultSwitchWaitLoad, 0x14064C4B0, void* a1) 
{
    bool aet_loading = aet::CheckAetSetLoading(results::AetSetID);
    bool spr_loading = spr::CheckSprSetLoading(results::SprSetID);
    PC_LOG("[WaitLoad] Enter. a1 = %p, aet_loading = %d, spr_loading = %d", a1, aet_loading, spr_loading);
	if (aet_loading || spr_loading)
    {
        PC_LOG("[WaitLoad] Still loading, returning early");
		return;
    }
	originalStageResultSwitchWaitLoad(a1);
    PC_LOG("[WaitLoad] Exit");
}

HOOK(bool, __fastcall, StageResultSwitchDest, 0x14064C300, void* a1)  
{
    PC_LOG("[Dest] Enter. a1 = %p", a1);
	aet::UnloadAetSet(results::AetSetID);
	spr::UnloadSprSet(results::SprSetID);
    PC_LOG("[Dest] Assets unloaded");
	bool ret = originalStageResultSwitchDest(a1);
    PC_LOG("[Dest] Exit. Return = %d", ret);
    return ret;
}

HOOK(void, __fastcall, StageResultSwitchDetailInit, 0x1406463B0, char* a1) 
{
    PC_LOG("[DetailInit] Enter. a1 = %p", a1);
    if (a1)
    {
        uintptr_t* ptr = (uintptr_t*)a1;
        PC_LOG("[DetailInit] Mem: [0x00]=%p, [0x08]=%p, [0x10]=%p, [0x18]=%p, [0x20]=%p, [0x28]=%p",
               (void*)ptr[0], (void*)ptr[1], (void*)ptr[2], (void*)ptr[3], (void*)ptr[4], (void*)ptr[5]);
    }
	originalStageResultSwitchDetailInit(a1);
    ScoreDetail* detail = *reinterpret_cast<ScoreDetail**>(a1 + 0x18);
    PC_LOG("[DetailInit] ScoreDetail pointer at 0x18 = %p", detail);
    if (detail)
    {
        PC_LOG("[DetailInit] ScoreDetail fields: Cool = %d, Fine = %d", detail->judge_count[0], detail->judge_count[1]);
    }
	nc::InitResultsData(detail);
    PC_LOG("[DetailInit] Exit");
}

HOOK(void, __fastcall, StageResultSwitchDetailDisp, 0x140647840, char* a1)  
{
    PC_LOG("[DetailDisp] Enter. a1 = %p", a1);
    int32_t val1 = *reinterpret_cast<const int32_t*>(a1 + 0x28 + 0x8 + 0x15C);
    int32_t val2 = *reinterpret_cast<const int32_t*>(a1 + 0xC);
    PC_LOG("[DetailDisp] Args: val1 (handle) = %d, val2 (prio) = %d", val1, val2);
	nc::DrawResultsWindowText(val1, val2);
	originalStageResultSwitchDetailDisp(a1);
    PC_LOG("[DetailDisp] Exit");
}

void InstallResultSwitchHooks()
{
    FILE* f = fopen("nc_pc_debug_log.txt", "w");
    if (f) { fprintf(f, "--- NEW UNLIMITED PC LOG SESSION ---\n"); fclose(f); }

	INSTALL_HOOK(CStageResultAetControllerInLoopOutGetSceneID);
	INSTALL_HOOK(CAetControllerInLoopOutSetLayer);
	INSTALL_HOOK(CStageResultAetControllerSetLayer);
	INSTALL_HOOK(CAetControllerGetLayout);
	INSTALL_HOOK(StageResultSwitchInit);
	INSTALL_HOOK(StageResultSwitchWaitLoad);
	INSTALL_HOOK(StageResultSwitchDest);
	INSTALL_HOOK(StageResultSwitchDetailInit);
	INSTALL_HOOK(StageResultSwitchDetailDisp);
}

#include <diva.h>
#include <hooks.h>
#include <nc_log.h>
#include <nc_state.h>
#include <util.h>
#include "common.h"
#include "result.h"

#include <stdio.h>
#include <stdarg.h>

static void PC_LOG(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("%s\n", buffer);

}

static void HexDumpPC(const char* desc, const void* addr, int len) {
    int i;
    unsigned char buff[17];
    const unsigned char* pc = (const unsigned char*)addr;
    if (desc != NULL) PC_LOG("%s:", desc);
    if (len == 0) { PC_LOG("  ZERO LENGTH"); return; }
    if (len < 0) { PC_LOG("  NEGATIVE LENGTH: %i", len); return; }
    for (i = 0; i < len; i++) {
        if ((i % 16) == 0) {
            if (i != 0) PC_LOG("  %s", buff);
            printf("  %04x ", i);
        }
        printf(" %02x", pc[i]);
        if ((pc[i] < 0x20) || (pc[i] > 0x7e)) buff[i % 16] = '.';
        else buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }
    while ((i % 16) != 0) { printf("   "); i++; }
    PC_LOG("  %s", buff);
}
// ---------------------------------------------

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

	prj::string name = prj::string("nsw_mode_tit_")
		+ styles[state.GetGameStyle()]
		+ "_"
		+ (no_fail ? "nofail_" : "")
		+ kinds[kind]
		+ GetLanguageSuffix().c_str();
        
    PC_LOG("[PC_DEBUG][GetModeLayerName] Generated: %s", name.c_str());
    return name;
}

static prj::string GetWindowLayerName(int32_t kind)
{
	static const char* kinds[4] = { "slide in", "slide loop", "slide_out", "count" };

	prj::string name = prj::string("nsw_win_arcard_")
		+ kinds[kind]
		+ (GetGameLocale() == GameLocale_JP ? "_jp" : "_en");
        
    PC_LOG("[PC_DEBUG][GetWindowLayerName] Generated: %s", name.c_str());
    return name;
}

static bool CheckLayerNameMatchesNC(std::string_view name)
{
	static const char* checks[2] = { "nsw_win_arcard_slide", "nsw_mode_tit" };
	for (int32_t i = 0; i < 2; i++)
	{
		if (util::Contains(name, checks[i])) {
            PC_LOG("[PC_DEBUG][CheckLayerNameMatchesNC] MATCHED: %s", name.data());
			return true;
        }
	}
	return false;
}

static bool patch_scene = false;

HOOK(uint32_t, __fastcall, CStageResultAetControllerInLoopOutGetSceneID, 0x14064BC50, void* a1) 
{
	uint32_t id = originalCStageResultAetControllerInLoopOutGetSceneID(a1);
    PC_LOG("[PC_DEBUG][GetSceneID] Called! a1=%p, Original_ID=%u, patch_scene=%d", a1, id, patch_scene);
    
	if (patch_scene) {
        PC_LOG("[PC_DEBUG][GetSceneID] -> PATCHING TO %u", results::AetSceneID);
        return results::AetSceneID;
    }
    return id;
}

HOOK(void, __fastcall, CAetControllerInLoopOutSetLayer, 0x14065E7D0, void* a1, const prj::string* in, const prj::string* loop, const prj::string* out, int32_t prio) 
{
    PC_LOG("[PC_DEBUG][SetLayerInLoopOut] Called! a1=%p", a1);
    PC_LOG("[PC_DEBUG][SetLayerInLoopOut] Original args: IN='%s', LOOP='%s', OUT='%s', PRIO=%d", 
        in ? in->c_str() : "NULL", 
        loop ? loop->c_str() : "NULL", 
        out ? out->c_str() : "NULL", prio);

	prj::string patched_in = *in;
	prj::string patched_lp = *loop;
	prj::string patched_out = *out;
    bool did_patch = false;

	if (util::StartsWith(*in, "mode_tit_arcade") && util::StartsWith(*loop, "mode_tit_arcade"))
	{
		if (state.GetGameStyle() != GameStyle_Arcade)
		{
			bool no_fail = util::Contains(*in, "comp") || util::Contains(*loop, "comp");
			patched_in = GetModeLayerName(0, no_fail);
			patched_lp = GetModeLayerName(1, no_fail);
            did_patch = true;
		}
	}
	else if (util::StartsWith(*in, "win_arcard") && util::StartsWith(*loop, "win_arcard"))
	{
		if (nc::ShouldUseConsoleStyleWin())
		{
			patched_in = GetWindowLayerName(0);
			patched_lp = GetWindowLayerName(1);
			patched_out = GetWindowLayerName(2);
            did_patch = true;
		}
	}
	
    if (did_patch) {
        PC_LOG("[PC_DEBUG][SetLayerInLoopOut] Will patch to: IN='%s', LOOP='%s', OUT='%s'", patched_in.c_str(), patched_lp.c_str(), patched_out.c_str());
    }

	originalCAetControllerInLoopOutSetLayer(a1, &patched_in, &patched_lp, &patched_out, prio);
}

HOOK(void, __fastcall, CStageResultAetControllerSetLayer, 0x14065DBA0, void* a1, prj::string* name, int32_t prio, int32_t action) 
{
    PC_LOG("[PC_DEBUG][SetLayer] Called! a1=%p, name='%s', prio=%d, action=%d", a1, name ? name->c_str() : "NULL", prio, action);
	
    patch_scene = CheckLayerNameMatchesNC(*name);
    if (patch_scene) PC_LOG("[PC_DEBUG][SetLayer] patch_scene is now TRUE");

	originalCStageResultAetControllerSetLayer(a1, name, prio, action);
	
    if (patch_scene) PC_LOG("[PC_DEBUG][SetLayer] patch_scene is now FALSE");
    patch_scene = false;
}

HOOK(void, __fastcall, CAetControllerGetLayout, 0x14065E200, CAetController* a1, void* a2, void* a3) 
{
    PC_LOG("[PC_DEBUG][GetLayout] Called! a1=%p, layer_name='%s'", a1, a1->args.layer_name);
	
    patch_scene = CheckLayerNameMatchesNC(a1->args.layer_name);
    if (patch_scene) PC_LOG("[PC_DEBUG][GetLayout] patch_scene is now TRUE");

	originalCAetControllerGetLayout(a1, a2, a3);
	
    if (patch_scene) PC_LOG("[PC_DEBUG][GetLayout] patch_scene is now FALSE");
    patch_scene = false;
}

HOOK(bool, __fastcall, StageResultSwitchInit, 0x14064C0E0, void* a1)
{
    PC_LOG("[PC_DEBUG][Init] Called! Loading AET %u and SPR %u", results::AetSceneID, results::SprSetID);
	state.ui.ResetAllLayers();
	prj::string out;
	prj::string_view strv;
	aet::LoadAetSet(results::AetSetID, &out);
	spr::LoadSprSet(results::SprSetID, &strv);
	return originalStageResultSwitchInit(a1);
}

HOOK(void, __fastcall, StageResultSwitchWaitLoad, 0x14064C4B0, void* a1) 
{
	if (aet::CheckAetSetLoading(results::AetSetID) || spr::CheckSprSetLoading(results::SprSetID))
		return;

	return originalStageResultSwitchWaitLoad(a1);
}

HOOK(bool, __fastcall, StageResultSwitchDest, 0x14064C300, void* a1) 
{
    PC_LOG("[PC_DEBUG][Dest] Called! Unloading assets.");
	aet::UnloadAetSet(results::AetSetID);
	spr::UnloadSprSet(results::SprSetID);
	return originalStageResultSwitchDest(a1);
}

HOOK(void, __fastcall, StageResultSwitchDetailInit, 0x1406463B0, char* a1) 
{
    PC_LOG("[PC_DEBUG][DetailInit] Called! a1=%p", a1);
    HexDumpPC("DUMPING a1 IN DetailInit (First 64 bytes)", a1, 64);
    
	originalStageResultSwitchDetailInit(a1);
    
    ScoreDetail* detail = *reinterpret_cast<ScoreDetail**>(a1 + 0x18);
    PC_LOG("[PC_DEBUG][DetailInit] ScoreDetail pointer at a1+0x18 = %p", detail);
    
	nc::InitResultsData(detail);
}

HOOK(void, __fastcall, StageResultSwitchDetailDisp, 0x140647840, char* a1) 
{
    int32_t val1 = *reinterpret_cast<const int32_t*>(a1 + 0x28 + 0x8 + 0x15C); // Это 0x18C !
    int32_t val2 = *reinterpret_cast<const int32_t*>(a1 + 0xC);
    
    PC_LOG("[PC_DEBUG][DetailDisp] Called! a1=%p", a1);
    PC_LOG("[PC_DEBUG][DetailDisp] HANDLE (val1 at 0x18C) = %d, PRIO (val2 at 0xC) = %d", val1, val2);
    

    HexDumpPC("DUMPING a1 IN DetailDisp around AET Handle (0x150 - 0x1B0)", a1 + 0x150, 96);

	nc::DrawResultsWindowText(val1, val2);
	originalStageResultSwitchDetailDisp(a1);
}

void InstallResultSwitchHooks()
{
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

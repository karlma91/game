#include "cbase.h"
#include "Timer.h"

#include "tier0/memdbgon.h"

extern IFileSystem *filesystem;

void CTimer::Start(int start)
{
    if (m_bUsingCPMenu) return;
    m_iStartTick = start;
    SetRunning(true);
    DispatchStateMessage();

    IGameEvent *timeStartEvent = gameeventmanager->CreateEvent("timer_started");

    if (timeStartEvent)
    {
        timeStartEvent->SetBool("timer_isrunning", true);
        gameeventmanager->FireEvent(timeStartEvent);
    }
}

void CTimer::PostTime()
{
    if (steamapicontext->SteamHTTP() && steamapicontext->SteamUser() && !m_bWereCheatsActivated)
    {
        //Get required info 
        //MOM_TODO include the extra security measures for beta+
        uint64 steamID = steamapicontext->SteamUser()->GetSteamID().ConvertToUint64();
        const char* map = gpGlobals->mapname.ToCStr();
        int ticks = gpGlobals->tickcount - m_iStartTick;

        TickSet::Tickrate tickRate = TickSet::GetCurrentTickrate();
        
        //Build URL
        char webURL[512];
        Q_snprintf(webURL, 512, "http://momentum-mod.org/postscore/%llu/%s/%i/%s", steamID, map,
            ticks, tickRate.sType);

        DevLog("Ticks sent to server: %i\n", ticks);
        //Build request
        mom_UTIL.PostTime(webURL);
    }
    else
    {
        Warning("Failed to post scores online: Cannot access STEAM HTTP or Steam User!\n");
    }
}

////MOM_TODO: REMOVEME
//CON_COMMAND(mom_test_hash, "Tests SHA1 Hashing\n")
//{
//    char pathToZone[MAX_PATH];
//    char mapName[MAX_PATH];
//    V_ComposeFileName("maps", gpGlobals->mapname.ToCStr(), mapName, MAX_PATH);
//    Q_strncat(mapName, ".zon", MAX_PATH);
//    filesystem->RelativePathToFullPath(mapName, "MOD", pathToZone, MAX_PATH);
//    Log("File path is: %s\n", pathToZone);
//
//    CSHA1 sha1;
//    sha1.HashFile(pathToZone);
//    sha1.Final();
//    unsigned char hash[20];
//    sha1.GetHash(hash);
//    Log("The hash for %s is: ", mapName);
//    for (int i = 0; i < 20; i++)
//    {
//        Log("%02x", hash[i]);
//    }
//    Log("\n");
//}

//Called upon map load, loads any and all times stored in the <mapname>.tim file
void CTimer::LoadLocalTimes(const char *szMapname)
{
    char timesFilePath[MAX_PATH];
    Q_strcpy(timesFilePath, c_mapDir);
    Q_strcat(timesFilePath, szMapname, MAX_PATH);
    Q_strncat(timesFilePath, c_timesExt, MAX_PATH);
    KeyValues *timesKV = new KeyValues(szMapname);

    if (timesKV->LoadFromFile(filesystem, timesFilePath, "MOD"))
    {
        for (KeyValues *kv = timesKV->GetFirstSubKey(); kv; kv = kv->GetNextKey())
        {
            Time t;
            t.ticks = Q_atoi(kv->GetName());
            t.tickrate = kv->GetFloat("rate");
            t.date = (time_t) kv->GetInt("date");

            for (KeyValues *subKv = kv->GetFirstSubKey(); subKv; subKv = subKv->GetNextKey()) 
            {
                if (!Q_strnicmp(subKv->GetName(), "stage", strlen("stage")))
                {
                    int i = Q_atoi(subKv->GetName()); //atoi will ignore "stage" and only return the stage number
                    t.stageticks[i] = subKv->GetInt("ticks");
                    t.stagevel[i] = subKv->GetInt("stage_enter_vel");
                    t.stageavgsync[i] = subKv->GetFloat("avg_sync");
                    t.stageavgsync2[i] = subKv->GetFloat("avg_sync2");
                    t.stageavgvel[i] = subKv->GetFloat("avg_vel");
                    t.stagemaxvel[i] = subKv->GetFloat("max_vel");
                    t.stagejumps[i] = subKv->GetInt("num_jumps");
                    t.stagestrafes[i] = subKv->GetInt("num_strafes");
                }
                if (!Q_strcmp(subKv->GetName(), "total"))
                {
                    t.jumps = subKv->GetInt("jumps");
                    t.strafes = subKv->GetInt("strafes");
                    t.avgsync = subKv->GetFloat("avgsync");
                    t.avgsync2 = subKv->GetFloat("avgsync2");
                    t.avgvel = subKv->GetFloat("avgvel");
                    t.maxvel = subKv->GetFloat("maxvel");
                    t.startvel = subKv->GetFloat("startvel");
                    t.endvel = subKv->GetFloat("endvel");
                }
            }
            localTimes.AddToTail(t);
        }
    }
    else
    {
        DevLog("Failed to load local times; no local file was able to be loaded!\n");
    }
    timesKV->deleteThis();
}

//Called every time a new time is achieved
void CTimer::SaveTime()
{
    const char *szMapName = gpGlobals->mapname.ToCStr();
    KeyValues *timesKV = new KeyValues(szMapName);
    int count = localTimes.Count();

    IGameEvent *runSaveEvent = gameeventmanager->CreateEvent("run_save");

    for (int i = 0; i < count; i++)
    {
        Time t = localTimes[i];
        char timeName[512];
        Q_snprintf(timeName, 512, "%i", t.ticks);
        KeyValues *pSubkey = new KeyValues(timeName);
        pSubkey->SetFloat("rate", t.tickrate);
        pSubkey->SetInt("date", t.date);

        KeyValues *pOverallKey = new KeyValues("total");
        pOverallKey->SetInt("jumps", t.jumps);
        pOverallKey->SetInt("strafes", t.strafes);
        pOverallKey->SetFloat("avgsync", t.avgsync);
        pOverallKey->SetFloat("avgsync2", t.avgsync2);
        pOverallKey->SetFloat("startvel", t.startvel);
        pOverallKey->SetFloat("endvel", t.endvel);
        pOverallKey->SetFloat("avgvel", t.avgvel);
        pOverallKey->SetFloat("maxvel", t.maxvel);

        char stageName[9]; // "stage 64\0"
        if (GetStageCount() > 1)
        {
            for (int i = 1; i <= GetStageCount(); i++) 
            {
                Q_snprintf(stageName, sizeof(stageName), "stage %d", i);

                KeyValues *pStageKey = new KeyValues(stageName);
                pStageKey->SetInt("ticks", t.stageticks[i]);
                pStageKey->SetInt("num_jumps", t.stagejumps[i]);
                pStageKey->SetInt("num_strafes", t.stagestrafes[i]);
                pStageKey->SetFloat("avg_sync", t.stageavgsync[i]);
                pStageKey->SetFloat("avg_sync2", t.stageavgsync2[i]);
                pStageKey->SetFloat("avg_vel", t.stageavgvel[i]);
                pStageKey->SetFloat("max_vel", t.stagemaxvel[i]);
                pStageKey->SetFloat("stage_enter_vel", t.stagevel[i]);
                pSubkey->AddSubKey(pStageKey);
            }
        }

        timesKV->AddSubKey(pSubkey);
        pSubkey->AddSubKey(pOverallKey);
    }

    char file[MAX_PATH];
    Q_strcpy(file, c_mapDir);
    Q_strcat(file, szMapName, MAX_PATH);
    Q_strncat(file, c_timesExt, MAX_PATH);

    if (timesKV->SaveToFile(filesystem, file, "MOD", true) && runSaveEvent)
    {
        runSaveEvent->SetBool("run_saved", true);
        gameeventmanager->FireEvent(runSaveEvent);
        Log("Successfully saved new time!\n");
        //initialize events resource file
    }
    timesKV->deleteThis();
}

void CTimer::Stop(bool endTrigger /* = false */)
{
    CMomentumPlayer *pPlayer = ToCMOMPlayer(UTIL_GetLocalPlayer());

    IGameEvent *runSaveEvent = gameeventmanager->CreateEvent("run_save");
    IGameEvent *timeStopEvent = gameeventmanager->CreateEvent("timer_started");
    IGameEvent *mapZoneEvent = gameeventmanager->CreateEvent("player_inside_mapzone");

    if (endTrigger && !m_bWereCheatsActivated && pPlayer)
    {
        // Post time to leaderboards if they're online
        // and if cheats haven't been turned on this session
        if (SteamAPI_IsSteamRunning())
            PostTime();

        //Save times locally too, regardless of SteamAPI condition
        Time t;
        t.ticks = gpGlobals->tickcount - m_iStartTick;
        t.tickrate = gpGlobals->interval_per_tick;
        time(&t.date);

        //stage 0 is overall stats
        t.jumps = pPlayer->m_nStageJumps[0];
        t.strafes = pPlayer->m_nStageStrafes[0];
        t.avgsync = pPlayer->m_flStageStrafeSyncAvg[0];
        t.avgsync2 = pPlayer->m_flStageStrafeSync2Avg[0];
        t.avgvel = pPlayer->m_flStageVelocityAvg[0];
        t.maxvel = pPlayer->m_flStageVelocityMax[0];
        t.startvel = pPlayer->m_flStartSpeed;
        t.endvel = pPlayer->m_flEndSpeed;
        if (GetStageCount() > 1) //don't save stage specific stats if we are on a linear map
        {
            for (int i = 1; i <= GetStageCount(); i++) //stages start at 1 since stage 0 is overall stats
            {
                t.stageticks[i] = m_iStageEnterTick[i]; //add each stage's total time in ticks
                t.stagejumps[i] = pPlayer->m_nStageJumps[i];
                t.stagestrafes[i] = pPlayer->m_nStageStrafes[i];
                t.stageavgsync[i] = pPlayer->m_flStageStrafeSyncAvg[i];
                t.stageavgsync2[i] = pPlayer->m_flStageStrafeSync2Avg[i];
                t.stageavgvel[i] = pPlayer->m_flStageVelocityAvg[i];
                t.stagemaxvel[i] = pPlayer->m_flStageVelocityMax[i];
                t.stagevel[i] = pPlayer->m_flStageEnterVelocity[i];
            }
        }   

        localTimes.AddToTail(t);

        SaveTime();
    }
    else if (runSaveEvent) //reset run saved status to false if we cant or didn't save
    {  
        runSaveEvent->SetBool("run_saved", false);
        gameeventmanager->FireEvent(runSaveEvent);
    }
    if (timeStopEvent)
    {
        timeStopEvent->SetBool("timer_isrunning", false);
        gameeventmanager->FireEvent(timeStopEvent);
    }
    if (mapZoneEvent)
    {
        mapZoneEvent->SetInt("current_stage", 0);
        mapZoneEvent->SetInt("stage_ticks", 0);
        gameeventmanager->FireEvent(mapZoneEvent);
    }
    SetRunning(false);
    DispatchStateMessage();
}
void CTimer::OnMapEnd(const char *pMapName)
{
    if (IsRunning())
        Stop(false);
    m_bWereCheatsActivated = false;
    SetCurrentCheckpointTrigger(NULL);
    SetStartTrigger(NULL);
    SetCurrentStage(NULL);
    RemoveAllCheckpoints();
    localTimes.Purge();
    //MOM_TODO: onlineTimes.RemoveAll();
}

void CTimer::OnMapStart(const char *pMapName)
{
    SetGameModeConVars();
    m_bWereCheatsActivated = false;
    RequestStageCount();
    //DispatchMapStartMessage();
    LoadLocalTimes(pMapName);
    //MOM_TODO: g_Timer.LoadOnlineTimes();
}

void CTimer::RequestStageCount()
{
    CTriggerStage *stage = (CTriggerStage *) gEntList.FindEntityByClassname(NULL, "trigger_momentum_timer_stage");
    int iCount = 1;//CTriggerStart counts as one
    while (stage)
    {
        iCount++;
        stage = (CTriggerStage *) gEntList.FindEntityByClassname(stage, "trigger_momentum_timer_stage");
    }
    m_iStageCount = iCount;
}
//This function is called every time CTriggerStage::StartTouch is called
int CTimer::GetStageTicks(int stage)
{
    if (stage == 1)
        m_iStageEnterTick[stage] = m_iStartTick; //stage "enter" for start zone is actually exit tick
    else if (stage > 1) //only compare pb/show time for stages after start zone
    {
        if (stage > m_iLastStage)
        {
            m_iStageEnterTick[stage] = gpGlobals->tickcount - m_iStartTick; //compare stage time diff
        }
            
    }
    m_iLastStage = stage;
    return m_iStageEnterTick[stage];
}
void CTimer::DispatchResetMessage()
{
    CSingleUserRecipientFilter user(UTIL_GetLocalPlayer());
    user.MakeReliable();
    UserMessageBegin(user, "Timer_Reset");
    MessageEnd();
}

void CTimer::DispatchStageMessage()
{
    CBasePlayer* cPlayer = UTIL_GetLocalPlayer();
    if (cPlayer && GetCurrentStage())
    {
        CSingleUserRecipientFilter user(cPlayer);
        user.MakeReliable();
        UserMessageBegin(user, "Timer_Stage");
        WRITE_LONG(GetCurrentStage()->GetStageNumber());
        MessageEnd();
    }
}

void CTimer::DispatchStateMessage()
{
    CBasePlayer* cPlayer = UTIL_GetLocalPlayer();
    if (cPlayer)
    {
        CSingleUserRecipientFilter user(cPlayer);
        user.MakeReliable();
        UserMessageBegin(user, "Timer_State");
        WRITE_BOOL(m_bIsRunning);
        WRITE_LONG(m_iStartTick);
        MessageEnd();
    }
}

void CTimer::DispatchCheckpointMessage()
{
    CBasePlayer* cPlayer = UTIL_GetLocalPlayer();
    if (cPlayer)
    {
        CSingleUserRecipientFilter user(cPlayer);
        user.MakeReliable();
        UserMessageBegin(user, "Timer_Checkpoint");
        WRITE_BOOL(m_bUsingCPMenu);
        WRITE_LONG(m_iCurrentStepCP + 1);
        WRITE_LONG(checkpoints.Count());
        MessageEnd();
    }
}

void CTimer::DispatchStageCountMessage()
{
    CBasePlayer* cPlayer = UTIL_GetLocalPlayer();
    if (cPlayer)
    {
        CSingleUserRecipientFilter user(cPlayer);
        user.MakeReliable();
        UserMessageBegin(user, "Timer_StageCount");
        WRITE_LONG(m_iStageCount);
        MessageEnd();
    }
}

CON_COMMAND_F(hud_timer_request_stages, "", FCVAR_DONTRECORD | FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_HIDDEN)
{
    g_Timer.DispatchStageCountMessage();
}
//set ConVars according to Gamemode. Tickrate is by in tickset.h
void CTimer::SetGameModeConVars()
{
    ConVarRef gm("mom_gamemode");
    switch (gm.GetInt())
    {
    case MOMGM_SURF:
        sv_maxvelocity.SetValue(3500);
        sv_airaccelerate.SetValue(150);
        sv_maxspeed.SetValue(260);
        break;
    case MOMGM_BHOP:
        sv_maxvelocity.SetValue(100000);
        sv_airaccelerate.SetValue(1000);
        sv_maxspeed.SetValue(260);
        break;
    case MOMGM_SCROLL:
        sv_maxvelocity.SetValue(3500);
        sv_airaccelerate.SetValue(100);
        sv_maxspeed.SetValue(250);
        break;
    case MOMGM_UNKNOWN:
    case MOMGM_ALLOWED:
        sv_maxvelocity.SetValue(3500);
        sv_airaccelerate.SetValue(150);
        sv_maxspeed.SetValue(260);
        break;
    default:
        DevWarning("[%i] GameMode not defined.\n", gm.GetInt());
        break;
    }
    DevMsg("CTimer set values:\nsv_maxvelocity: %i\nsv_airaccelerate: %i \nsv_maxspeed: %i\n",
        sv_maxvelocity.GetInt(), sv_airaccelerate.GetInt(), sv_maxspeed.GetInt());
}
//Practice mode that stops the timer and allows the player to noclip.
void CTimer::EnablePractice(CBasePlayer *pPlayer)
{
    pPlayer->SetParent(NULL);
    pPlayer->SetMoveType(MOVETYPE_NOCLIP);
    ClientPrint(pPlayer, HUD_PRINTCONSOLE, "Practice mode ON!\n");
    pPlayer->AddEFlags(EFL_NOCLIP_ACTIVE);
    g_Timer.Stop(false);

    IGameEvent *pracModeEvent = gameeventmanager->CreateEvent("practice_mode");
    if (pracModeEvent)
    {
        pracModeEvent->SetBool("has_practicemode", true);
        gameeventmanager->FireEvent(pracModeEvent);
    }

}
void CTimer::DisablePractice(CBasePlayer *pPlayer)
{
    pPlayer->RemoveEFlags(EFL_NOCLIP_ACTIVE);
    ClientPrint(pPlayer, HUD_PRINTCONSOLE, "Practice mode OFF!\n");
    pPlayer->SetMoveType(MOVETYPE_WALK);

    IGameEvent *pracModeEvent = gameeventmanager->CreateEvent("practice_mode");
    if (pracModeEvent)
    {
        pracModeEvent->SetBool("has_practicemode", false);
        gameeventmanager->FireEvent(pracModeEvent);
    }

}
bool CTimer::IsPracticeMode(CBaseEntity *pOther)
{
    return pOther->GetMoveType() == MOVETYPE_NOCLIP && (pOther->GetEFlags() & EFL_NOCLIP_ACTIVE);
}
//--------- CPMenu stuff --------------------------------

void CTimer::CreateCheckpoint(CBasePlayer *pPlayer)
{
    if (!pPlayer) return;
    Checkpoint c;
    c.ang = pPlayer->GetAbsAngles();
    c.pos = pPlayer->GetAbsOrigin();
    c.vel = pPlayer->GetAbsVelocity();
    checkpoints.AddToTail(c);
    m_iCurrentStepCP++;
}

void CTimer::RemoveLastCheckpoint()
{
    if (checkpoints.IsEmpty()) return;
    checkpoints.Remove(m_iCurrentStepCP);
    m_iCurrentStepCP--;//If there's one element left, we still need to decrease currentStep to -1
}

void CTimer::TeleportToCP(CBasePlayer* cPlayer, int cpNum)
{
    if (checkpoints.IsEmpty() || !cPlayer) return;
    Checkpoint c = checkpoints[cpNum];
    cPlayer->Teleport(&c.pos, &c.ang, &c.vel);
}

void CTimer::SetUsingCPMenu(bool pIsUsingCPMenu)
{
    m_bUsingCPMenu = pIsUsingCPMenu;
}

void CTimer::SetCurrentCPMenuStep(int pNewNum)
{
    m_iCurrentStepCP = pNewNum;
}

//--------- CTriggerOnehop stuff --------------------------------

int CTimer::AddOnehopToListTail(CTriggerOnehop *pTrigger)
{
    return onehops.AddToTail(pTrigger);
}

bool CTimer::RemoveOnehopFromList(CTriggerOnehop *pTrigger)
{
    return onehops.FindAndRemove(pTrigger);
}

int CTimer::FindOnehopOnList(CTriggerOnehop *pTrigger)
{
    return onehops.Find(pTrigger);
}

CTriggerOnehop *CTimer::FindOnehopOnList(int pIndexOnList)
{
    return onehops.Element(pIndexOnList);
}

//--------- Commands --------------------------------

class CTimerCommands
{
public:
    static void ResetToStart()
    {
        CBasePlayer* cPlayer = UTIL_GetCommandClient();
        CTriggerTimerStart *start;
        if ((start = g_Timer.GetStartTrigger()) != NULL && cPlayer)
        {
            // Don't set angles if still in start zone.
            if (g_Timer.IsRunning() && start->GetHasLookAngles())
            {
                QAngle ang = start->GetLookAngles();

                cPlayer->Teleport(&start->WorldSpaceCenter(), &ang, &vec3_origin);
            }
            else
            {
                cPlayer->Teleport(&start->WorldSpaceCenter(), NULL, &vec3_origin);
            }
        }
    }

    static void ResetToCheckpoint()
    {
        CTriggerStage *stage;
        CBaseEntity* pPlayer = UTIL_GetCommandClient();
        if ((stage = g_Timer.GetCurrentStage()) != NULL && pPlayer)
        {
            pPlayer->Teleport(&stage->WorldSpaceCenter(), NULL, &vec3_origin);
        }
    }

    static void CPMenu(const CCommand &args)
    {
        if (!g_Timer.IsUsingCPMenu())
            g_Timer.SetUsingCPMenu(true);

        if (g_Timer.IsRunning())
        {
            // MOM_TODO: consider
            // 1. having a local timer running, as people may want to time their routes they're using CP menu for
            // 2. gamemodes (KZ) where this is allowed

            ConVarRef gm("mom_gamemode");
            switch (gm.GetInt())
            {
            case MOMGM_SURF:
            case MOMGM_BHOP:
            case MOMGM_SCROLL:
                g_Timer.Stop(false);

                //case MOMGM_KZ:
            default:
                break;
            }
        }
        if (args.ArgC() > 1)
        {
            int sel = Q_atoi(args[1]);
            CBasePlayer* cPlayer = UTIL_GetCommandClient();
            switch (sel)
            {
            case 1://create a checkpoint
                g_Timer.CreateCheckpoint(cPlayer);
                break;

            case 2://load previous checkpoint
                g_Timer.TeleportToCP(cPlayer, g_Timer.GetCurrentCPMenuStep());
                break;

            case 3://cycle through checkpoints forwards (+1 % length)
                if (g_Timer.GetCPCount() > 0)
                {
                    g_Timer.SetCurrentCPMenuStep((g_Timer.GetCurrentCPMenuStep() + 1) % g_Timer.GetCPCount());
                    g_Timer.TeleportToCP(cPlayer, g_Timer.GetCurrentCPMenuStep());
                }
                break;

            case 4://cycle backwards through checkpoints
                if (g_Timer.GetCPCount() > 0)
                {
                    g_Timer.SetCurrentCPMenuStep(g_Timer.GetCurrentCPMenuStep() == 0 ? g_Timer.GetCPCount() - 1 : g_Timer.GetCurrentCPMenuStep() - 1);
                    g_Timer.TeleportToCP(cPlayer, g_Timer.GetCurrentCPMenuStep());
                }
                break;

            case 5://remove current checkpoint
                g_Timer.RemoveLastCheckpoint();
                break;
            case 6://remove every checkpoint
                g_Timer.RemoveAllCheckpoints();
                break;
            case 0://They closed the menu
                g_Timer.SetUsingCPMenu(false);
                break;
            default:
                if (cPlayer != NULL)
                {
                    cPlayer->EmitSound("Momentum.UIMissingMenuSelection");
                }
                break;
            }
        }
        g_Timer.DispatchCheckpointMessage();
    }

    static void PracticeMove()
    {
        CBasePlayer *pPlayer = UTIL_GetCommandClient();
        if (!pPlayer)
            return;
        Vector velocity = pPlayer->GetAbsVelocity();

        if (!g_Timer.IsPracticeMode(pPlayer))
        {
            if (velocity.Length2DSqr() != 0)
                DevLog("You cannot enable practice mode while moving!\n");
            else
                g_Timer.EnablePractice(pPlayer);
        }
        else //player is either already in practice mode
            g_Timer.DisablePractice(pPlayer);
    }
};


static ConCommand mom_practice("mom_practice", CTimerCommands::PracticeMove, "Toggle. Stops timer and allows player to fly around in noclip.\n" 
    "Only activates when player is standing still (xy vel = 0)\n",
    FCVAR_CLIENTCMD_CAN_EXECUTE);
static ConCommand mom_reset_to_start("mom_restart", CTimerCommands::ResetToStart, "Restarts the player to the start trigger.\n",
    FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
static ConCommand mom_reset_to_checkpoint("mom_reset", CTimerCommands::ResetToCheckpoint, "Teleports the player back to the start of the current stage.\n",
    FCVAR_CLIENTCMD_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE);
static ConCommand mom_cpmenu("cpmenu", CTimerCommands::CPMenu, "", FCVAR_HIDDEN | FCVAR_SERVER_CAN_EXECUTE | FCVAR_CLIENTCMD_CAN_EXECUTE);
CTimer g_Timer;
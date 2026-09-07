ScriptName RCAI_Sandbox extends Quest
; M6/M7 glue · Living-world sandbox controls for playtesters + QA.
;
; The plugin exposes console commands (RCAISetHostility / RCAIInjectRaid /
; RCAIDumpMemory) that drive the faction-memory and settlement ledgers;
; this quest script packages them for the mod menu and the controller
; (P2-b2) so QA can script a raid or neutralize a faction without console.

Event OnLoad()
EndEvent

; aiMode: 0 = stock behaviour, 1 = friendly, 2 = hostile (all factions in
; the current cell), 3 = neutral.
Function SetCellFactionMode(int aiMode)
    int i = ClampI(aiMode, 0, 3)
    Utility.SetINIInt(i, "RCAI", "iRCAISandboxMode")
    Utility.SendConsoleCommand("rcai_sandbox_mode " + Utility.IntToString(i))
    Debug.Notification(778, "RCAI sandbox mode: " + Utility.IntToString(i))
EndFunction

; Inject a raid event at the named settlement (ledger update + prices up).
Function InjectRaid(String asSettlement)
    Utility.SendConsoleCommand("rcai_inject_raid " + asSettlement)
    Debug.Notification(779, "Raid injected: " + asSettlement)
EndFunction

; Dump the faction-memory ledger to Data/RCAI/memory_dump.json (QA evidence).
Function DumpMemory()
    Utility.SendConsoleCommand("rcai_dump_memory")
    Debug.Notification(780, "Faction memory dumped.")
EndFunction

int Function ClampI(int aiV, int aiMin, int aiMax)
    if aiV < aiMin
        return aiMin
    endif
    if aiV > aiMax
        return aiMax
    endif
    return aiV
EndFunction

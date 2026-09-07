ScriptName RCAI_AudioCues extends Quest
; M7 (P2-c) · Audio layer for the modernized AI.
;
; The plugin raises Papyrus events (RCAICombat / RCASquadAlert); this quest
; script turns them into audio cues so sound becomes a real information
; channel (stock FO4 has none). Sound properties are filled in the CK with
; the shipped assets (Data/Sound/RCAI/*.wsb).

Property Sound RCAI_AlertSound Auto
Property Sound RCAI_KillSound Auto
Property Sound RCAI_FlankSound Auto
Property Sound RCAI_CoverSound Auto

bool bCuesEnabled = true

Event OnLoad()
    RegisterForGameEvent("RCAICombat", "RCAICombatEvent")
    RegisterForGameEvent("RCASquadAlert", "SquadAlertEvent")
EndEvent

Function SetCuesEnabled(bool abEnabled)
    bCuesEnabled = abEnabled
    Utility.SetINIBool(abEnabled, "RCAI", "bRCAIAudioCues")
EndFunction

Event RCAICombatEvent(String asType, String asDetail)
    if !bCuesEnabled
        return
    endif
    if asType == "alert"
        ; short high ping: enemies have spotted the player
        If RCAI_AlertSound
            Game.GetPlayer().PlaySound(RCAI_AlertSound)
        EndIf
    elseif asType == "kill"
        If RCAI_KillSound
            Game.GetPlayer().PlaySound(RCAI_KillSound)
        EndIf
    elseif asType == "flank"
        If RCAI_FlankSound
            Game.GetPlayer().PlaySound(RCAI_FlankSound)
        EndIf
    elseif asType == "cover"
        If RCAI_CoverSound
            Game.GetPlayer().PlaySound(RCAI_CoverSound)
        EndIf
    endif
EndEvent

Event SquadAlertEvent(String asFaction, String asDetail)
    If bCuesEnabled && RCAI_AlertSound
        Game.GetPlayer().PlaySound(RCAI_AlertSound)
    EndIf
EndEvent

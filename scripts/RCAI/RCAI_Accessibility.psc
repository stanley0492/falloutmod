ScriptName RCAI_Accessibility extends Quest
; M7 (P2-b) · Accessibility + HUD options, the in-game face of RCAI.
;
; Attach to a persistent quest (start-on-load). The plugin polls the shared
; INI (config/RCAI.ini, [RCAI] section) every second, so every option takes
; effect without a reload.

String RCAI_LastAnnouncement

Event OnLoad()
EndEvent

; --- options (write the shared INI the plugin polls) ---

Function SetFOV(float afDegrees)
    float f = ClampF(afDegrees, 30.0, 110.0)
    Utility.SetINIFloat(f, "Display", "fFov")
    Announce("Field of view: " + Utility.FloatToString(f, 1) + " degrees.")
EndFunction

Function SetHUDScale(float afScale)
    float f = ClampF(afScale, 0.5, 2.0)
    Utility.SetINIFloat(f, "General", "fUIScale")
    Announce("HUD scale: " + Utility.FloatToString(f, 1) + "x")
EndFunction

Function SetAnnouncementsEnabled(bool abEnabled)
    Utility.SetINIBool(abEnabled, "RCAI", "bRCAIAnnouncements")
EndFunction

; Colorblind palettes: 0 = off, 1 = deuteranopia, 2 = protanopia, 3 = tritanopia.
; The plugin recolors its HUD and squad pips (P2-b3).
Function SetColorblindPalette(int aiMode)
    int i = ClampI(aiMode, 0, 3)
    Utility.SetINIInt(i, "RCAI", "iRCAIColorblindPalette")
    Announce("Colorblind palette: mode " + Utility.IntToString(i) + ".")
EndFunction

; --- screen-reader style announcements (P2-b4) ---

; Read-aloud + on-screen caption.
Function Announce(String asText)
    if !Utility.GetINIBool("RCAI", "bRCAIAnnouncements")
        return
    endif
    RCAI_LastAnnouncement = asText
    Game.GetPlayer().PlayText(asText, 3.0)
    Debug.Notification(777, asText)
EndFunction

; Combat event hook: the plugin raises RCAICombat("type","detail") on the
; Papyrus VM (docs/INTEGRATION_CHECKLIST.md, step 4).
Event RCAICombat(String asType, String asDetail)
    if asType == "kill"
        Announce("Enemy eliminated: " + asDetail + ".")
    elseif asType == "alert"
        Announce("Squad alert: " + asDetail + ".")
    elseif asType == "flanked"
        Announce("Flanked. Taking cover.")
    endif
EndEvent

; --- helpers ---

float Function ClampF(float afV, float afMin, float afMax)
    if afV < afMin
        return afMin
    endif
    if afV > afMax
        return afMax
    endif
    return afV
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

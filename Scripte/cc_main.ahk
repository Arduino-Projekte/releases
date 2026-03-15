#Requires AutoHotkey v2.0
#WinActivateForce
SetTitleMatchMode 2

title := "cc_main"

if WinExist(title)
{
    WinActivate(title)
    WinWaitActive(title,,2)
    Sleep 200
    Send("^r")
}
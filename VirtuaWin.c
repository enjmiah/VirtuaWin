//
//  VirtuaWin - Virtual Desktop Manager for Win9x/NT/Win2K
// 
//  Copyright (c) 1999, 2000 jopi
// 
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
//  USA.
//

// Includes
#include "VirtuaWin.h"
#include "DiskRoutines.h"
#include "SetupDialog.h"
#include "ConfigParameters.h"
#include "Messages.h"
#include "ModuleRoutines.h"

// Standard includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>
#include <math.h>
#include <io.h>

/*************************************************
 * VirtuaWin start point
 */
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
   RECT r;
   MSG msg;
   WNDCLASSEX wc;
   int threadID;
   char *classname = appName;
   hInst = hInstance;
   
   /* Only one instance may be started */
   CreateMutex(NULL, TRUE, "PreventSecondVirtuaWin");
   if(GetLastError() == ERROR_ALREADY_EXISTS)
      return 0; // just quit 
  
   /* Create a window class for the window that receives systray notifications.
      The window will never be displayed */
   wc.cbSize = sizeof(WNDCLASSEX);
   wc.style = 0;
   wc.lpfnWndProc = wndProc;
   wc.cbClsExtra = wc.cbWndExtra = 0;
   wc.hInstance = hInstance;
   wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VIRTWIN));
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
   wc.lpszMenuName = NULL;
   wc.lpszClassName = classname;
   wc.hIconSm = (HICON) LoadImage(hInstance, MAKEINTRESOURCE(IDI_VIRTWIN), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), 0);

   RegisterClassEx(&wc);

   /* Get screen witdth and height */
   GetClientRect(GetDesktopWindow(), &r);
   screenWidth = r.right - 3;
   screenHeight = r.bottom - 3;
   /* Get the height of the task bar */
   GetWindowRect(FindWindow("Shell_traywnd", ""), &r);
   taskBarHeight = (r.bottom - r.top);

   /* set the window to give focus to when releasing focus on switch also used to refresh */
   releaseHnd = GetDesktopWindow();
  
   loadFilePaths();
   
   readConfig();	// Read the config file
   loadIcons();
    
   /* Now, set the lock file */
   if(crashRecovery) {
      if(!tryToLock())
         if(MessageBox(hWnd, "VirtuaWin was not properly shut down.\n Would you try to recover your windows?", "VirtuaWin", MB_YESNO | MB_SYSTEMMODAL) == IDYES) {
            recoverWindows();
         }
   }
  
   /* Create window. Note that WS_VISIBLE is not used, and window is never shown */
   hWnd = CreateWindowEx(0, classname, classname, WS_POPUP, CW_USEDEFAULT, 0,
                         CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
  
   nIconD.cbSize = sizeof(NOTIFYICONDATA); // size
   nIconD.hWnd = hWnd;		    // window to receive notifications
   nIconD.uID = 1;		    // application-defined ID for icon (can be any UINT value)
   nIconD.uFlags = NIF_MESSAGE |   // nIconD.uCallbackMessage is valid, use it
      NIF_ICON |		    // nIconD.hIcon is valid, use it
      NIF_TIP;		    // nIconD.szTip is valid, use it
   nIconD.uCallbackMessage = UWM_SYSTRAY;  // message sent to nIconD.hWnd
   nIconD.hIcon = icons[1];

   strcpy(nIconD.szTip, appName);		// Tooltip
   Shell_NotifyIcon(NIM_ADD, &nIconD);		// This adds the icon

   /* Register the keys */
   if(!registerKeys())
      MessageBox(hWnd, "Invalid key modifier combination, check control keys!", 
                 NULL, MB_ICONWARNING);
   if(!registerHotKeys())
      MessageBox(hWnd, "Invalid key modifier combination, check hot keys!", 
                 NULL, MB_ICONWARNING);
   if(!registerStickyKey())
      MessageBox(hWnd, "Invalid key modifier combination, check sticky hot key!", 
                 NULL, MB_ICONWARNING);
   if(!registerCyclingKeys())
      MessageBox(hWnd, "Invalid key modifier combination, check cycling hot keys!", 
                 NULL, MB_ICONWARNING);
   if(!registerMenuHotKey())
      MessageBox(hWnd, "Invalid key modifier combination, check menu hot key!", 
                 NULL, MB_ICONWARNING);
   setMouseKey();
   
   /* Load some stuff */
   curSticky = loadStickyList(stickyList);
   curAssigned = loadAssignedList(assignedList);
   initData();			      // init window list
   EnumWindows(enumWindowsProc, 0);   // get all windows
   saveDesktopState(&nWin, winList);  // Let's save them now
   
   curUser = loadUserList(userList);  // load any user defined windows
   if(curUser != 0)
      userDefinedWin = TRUE;
   if(userDefinedWin)                 // locate windows, if any
      findUserWindows();

   /* Load user modules */
   curDisabledMod = loadDisabledModules(disabledModules);
   loadModules();

   /* Create the thread responsible for mouse monitoring */   
   mouseThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MouseProc, NULL, 0, &threadID); 	
   if(!mouseEnable) // Suspend the thread if no mouse support
      SuspendThread(mouseThread);
   
   /* Main message loop */
   while (GetMessage(&msg, NULL, 0, 0)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }
   
   return msg.wParam;
}

/*************************************************
 * The mouse thread. This function runs in a thread and checks the mouse
 * position every 50ms. If mouse support is disabled, the thread will be in 
 * suspended state.
 */
DWORD WINAPI MouseProc(LPVOID lpParameter)
{
   POINT pt;
   
   while(1) {
      Sleep(50);
      GetCursorPos(&pt);
      
      if(pt.x < 3) {
         // switch left
         SendNotifyMessage(hWnd, VW_MOUSEWARP, 0, 
                           MAKELPARAM(checkMouseState(), VW_MOUSELEFT));
      }
      else if(pt.x > screenWidth) { 
         // switch right
         SendNotifyMessage(hWnd, VW_MOUSEWARP, 0, 
                           MAKELPARAM(checkMouseState(), VW_MOUSERIGHT));
      }
      else if(pt.y < 3) { 
         // switch up
         SendNotifyMessage(hWnd, VW_MOUSEWARP, 0, 
                           MAKELPARAM(checkMouseState(), VW_MOUSEUP));
      }
      else if(pt.y > (screenHeight - (taskBarWarp * taskBarHeight * checkMouseState()))) {
         // switch down
         SendNotifyMessage(hWnd, VW_MOUSEWARP, 0, 
                           MAKELPARAM(checkMouseState(), VW_MOUSEDOWN));
      }
      else {
         SendNotifyMessage(hWnd, VW_MOUSEWARP, 0, MAKELPARAM(0, VW_MOUSERESET));
      }
   }
   return TRUE;
}

/*************************************************
 * Checks if mouse key modifier is pressed
 */
__inline BOOL checkMouseState()
{
   if(!GetSystemMetrics(SM_SWAPBUTTON)) {  // Check the state of mouse button(s)
      if(HIWORD(GetAsyncKeyState(VK_LBUTTON)))
         return TRUE;
      else
         return FALSE;
   } else if(HIWORD(GetAsyncKeyState(VK_RBUTTON)))
      return TRUE;
   else
      return FALSE;
}

/************************ *************************
 * Loads the icons for the systray according to the current setup
 */
void loadIcons() {
  int xIcon = GetSystemMetrics(SM_CXSMICON);
  int yIcon = GetSystemMetrics(SM_CYSMICON);

  /* Try to load user defined icons */
  icons[0] = (HICON) LoadImage(hInst, "icons/0.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[1] = (HICON) LoadImage(hInst, "icons/1.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[2] = (HICON) LoadImage(hInst, "icons/2.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[3] = (HICON) LoadImage(hInst, "icons/3.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[4] = (HICON) LoadImage(hInst, "icons/4.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[5] = (HICON) LoadImage(hInst, "icons/5.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[6] = (HICON) LoadImage(hInst, "icons/6.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[7] = (HICON) LoadImage(hInst, "icons/7.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[8] = (HICON) LoadImage(hInst, "icons/8.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);
  icons[9] = (HICON) LoadImage(hInst, "icons/9.ico", IMAGE_ICON, xIcon, yIcon, LR_LOADFROMFILE);

  /* Create all icons for the system tray */
  if(nDesksY == 2 && nDesksX == 2) { // if 2 by 2 mode
    if(!icons[0])
      icons[0] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL_DIS), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[1])
      icons[1] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL_NW), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[2])
      icons[2] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL_NE), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[3])
      icons[3] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL_SW), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[4])
      icons[4] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_SMALL_SE), IMAGE_ICON, xIcon, yIcon, 0);
  } else { // otherwise load 1-9 icons
    if(!icons[0])
      icons[0] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON0), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[1])
      icons[1] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[2])
      icons[2] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[3])
      icons[3] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON3), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[4])
      icons[4] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON4), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[5])
      icons[5] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON5), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[6])
      icons[6] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON6), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[7])
      icons[7] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON7), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[8])
      icons[8] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON8), IMAGE_ICON, xIcon, yIcon, 0);
    if(!icons[9])
      icons[9] = (HICON) LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON9), IMAGE_ICON, xIcon, yIcon, 0);
  }
}

/*************************************************
 * Resets all icons and reloads them
 */
void reLoadIcons()
{
  int i;
  for(i = 0; i < 9; ++i)
    icons[i] = NULL;
  loadIcons();
  setIcon(calculateDesk());
}

/*************************************************
 * Register the keys to use for switching desktop with the arrow keys
 */
BOOL registerKeys()
{
  if(keyEnable && !keysRegistred) {
    keysRegistred = TRUE;
    setKeyMod();
    vwLeft = GlobalAddAtom("atomKeyLeft");
    if((RegisterHotKey(hWnd, vwLeft, MODKEY, VK_LEFT) == FALSE))
      return FALSE;
    vwRight = GlobalAddAtom("atomKeyRight");
    if((RegisterHotKey(hWnd, vwRight, MODKEY, VK_RIGHT) == FALSE))
      return FALSE;
    vwUp = GlobalAddAtom("atomKeyUp");
    if((RegisterHotKey(hWnd, vwUp, MODKEY, VK_UP) == FALSE))
      return FALSE;
    vwDown = GlobalAddAtom("atomKeyDown");
    if((RegisterHotKey(hWnd, vwDown, MODKEY, VK_DOWN) == FALSE))
      return FALSE;
    return TRUE;
  }
  return TRUE;
}

/*************************************************
 * Unegister the keys to use for switching desktop
 */
void unRegisterKeys()
{
  if(keysRegistred) {
    keysRegistred = FALSE;
    UnregisterHotKey(hWnd, vwLeft);
    UnregisterHotKey(hWnd, vwRight);
    UnregisterHotKey(hWnd, vwUp);
    UnregisterHotKey(hWnd, vwDown);
  }
}

/*************************************************
 * Register the hotkeys to use for switching desktop and the winlist hotkey
 */
BOOL registerHotKeys()
{
   if(hotKeyEnable && !hotKeysRegistred) {
      hotKeysRegistred = TRUE;
      if(hotkey1) {
         vw1 = GlobalAddAtom("atomKey1");
         if((RegisterHotKey(hWnd, vw1, hotKey2ModKey(hotkey1Mod) | hotkey1Win, hotkey1) == FALSE))
            return FALSE;
      }
      if(hotkey2) {
         vw2 = GlobalAddAtom("atomKey2");
         if((RegisterHotKey(hWnd, vw2, hotKey2ModKey(hotkey2Mod) | hotkey2Win, hotkey2) == FALSE))
            return FALSE;
      }
      if(hotkey3) {
         vw3 = GlobalAddAtom("atomKey3");
         if((RegisterHotKey(hWnd, vw3, hotKey2ModKey(hotkey3Mod) | hotkey3Win, hotkey3) == FALSE))
            return FALSE;
      }
      if(hotkey4) {
         vw4 = GlobalAddAtom("atomKey4");
         if((RegisterHotKey(hWnd, vw4, hotKey2ModKey(hotkey4Mod) | hotkey4Win, hotkey4) == FALSE))
            return FALSE;
      }
      if(hotkey5) {
         vw5 = GlobalAddAtom("atomKey5");
         if((RegisterHotKey(hWnd, vw5, hotKey2ModKey(hotkey5Mod) | hotkey5Win, hotkey5) == FALSE))
            return FALSE;
      }
      if(hotkey6) {
         vw6 = GlobalAddAtom("atomKey6");
         if((RegisterHotKey(hWnd, vw6, hotKey2ModKey(hotkey6Mod) | hotkey6Win, hotkey6) == FALSE))
            return FALSE;
      }
      if(hotkey7) {
         vw7 = GlobalAddAtom("atomKey7");
         if((RegisterHotKey(hWnd, vw7, hotKey2ModKey(hotkey7Mod) | hotkey7Win, hotkey7) == FALSE))
            return FALSE;
      }
      if(hotkey8) {
         vw8 = GlobalAddAtom("atomKey8");
         if((RegisterHotKey(hWnd, vw8, hotKey2ModKey(hotkey8Mod) | hotkey8Win, hotkey8) == FALSE))
            return FALSE;
      }
      if(hotkey9) {
         vw9 = GlobalAddAtom("atomKey9");
         if((RegisterHotKey(hWnd, vw9, hotKey2ModKey(hotkey9Mod) | hotkey9Win, hotkey9) == FALSE))
            return FALSE;
      }
      return TRUE;
   }
   return TRUE;
}

/*************************************************
 * Unregister the hot keys
 */
void unRegisterHotKeys()
{
  if(hotKeysRegistred) {
    hotKeysRegistred = FALSE;
    UnregisterHotKey(hWnd, vw1);
    UnregisterHotKey(hWnd, vw2);
    UnregisterHotKey(hWnd, vw3);
    UnregisterHotKey(hWnd, vw4);
    UnregisterHotKey(hWnd, vw5);
    UnregisterHotKey(hWnd, vw6);
    UnregisterHotKey(hWnd, vw7);
    UnregisterHotKey(hWnd, vw8);
    UnregisterHotKey(hWnd, vw9);
  }
}

/*************************************************
 * Register the sticky hot key, if defined
 */
BOOL registerStickyKey()
{
  if(!stickyKeyRegistered && VW_STICKYMOD && VW_STICKY) {
    stickyKeyRegistered = TRUE;
    stickyKey = GlobalAddAtom("VWStickyKey");
    if((RegisterHotKey(hWnd, stickyKey, hotKey2ModKey(VW_STICKYMOD), VW_STICKY) == FALSE))
      return FALSE;
    else
      return TRUE;
  }
  return TRUE;
}

/*************************************************
 * Unregister the sticky hot key, if previosly registred
 */
void unRegisterStickyKey()
{
  if(stickyKeyRegistered) {
    stickyKeyRegistered = FALSE;
    UnregisterHotKey(hWnd, stickyKey);
  }
}

/*************************************************
 * Register the desktop cycling hot keys, if defined
 */
BOOL registerCyclingKeys()
{
  if(!cyclingKeysRegistered && cyclingKeysEnabled) {
     cyclingKeysRegistered = TRUE;
     cyclingKeyUp = GlobalAddAtom("VWCyclingKeyUp");
     cyclingKeyDown = GlobalAddAtom("VWCyclingKeyDown");
     if((RegisterHotKey(hWnd, cyclingKeyUp, hotKey2ModKey(hotCycleUpMod), hotCycleUp) == FALSE))
        return FALSE;
     if((RegisterHotKey(hWnd, cyclingKeyDown, hotKey2ModKey(hotCycleDownMod), hotCycleDown) == FALSE))
        return FALSE; 
  }
  return TRUE;
}

/*************************************************
 * Unregister the cycling  hot keys, if previosly registred
 */
void unRegisterCyclingKeys()
{
   if(cyclingKeysRegistered) {
      cyclingKeysRegistered = FALSE;
      UnregisterHotKey(hWnd, cyclingKeyUp);
      UnregisterHotKey(hWnd, cyclingKeyDown);
   }
}

/*************************************************
 * Register the window menu hot key, if defined
 */
BOOL registerMenuHotKey()
{
   if(!menuHotKeyRegistered && hotkeyMenu) {
      menuHotKeyRegistered = TRUE;
      vwMenu = GlobalAddAtom("atomKeyMenu");
      if((RegisterHotKey(hWnd, vwMenu, hotKey2ModKey(hotkeyMenuMod) | 
                         hotkeyMenuWin, hotkeyMenu) == FALSE))
         return FALSE;
   }
   return TRUE;
}

/*************************************************
 * Unregister the window menu hot key, if previosly registred
 */
void unRegisterMenuHotKey()
{
   if(menuHotKeyRegistered) {
      menuHotKeyRegistered = FALSE;
      UnregisterHotKey(hWnd, vwMenu);
   }
}

/*************************************************
 * Main window callback, this is where all main window messages are taken care of
 */
LRESULT CALLBACK wndProc(HWND aHWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
   POINT pt;
   HMENU hmenu, hpopup;
   int retItem;
   static UINT taskbarRestart; 
    
   switch (message) {
      case VW_MOUSEWARP:
         if(enabled) { // Is virtuawin enabled
            if(useMouseKey) { // Are we using a mouse key
               if(!HIWORD(GetAsyncKeyState(MOUSEKEY))) {
                  goto skipMouseWarp; // If key not pressed skip whole sequence
               }
            }
            // Suspend mouse thread during message processing, 
            // otherwise we might step over several desktops
            SuspendThread(mouseThread); 
            GetCursorPos(&pt);
            
            switch HIWORD(lParam) {
               case VW_MOUSELEFT:
                  warpMultiplier++;
                  if((warpMultiplier >= configMultiplier)) {
                     isDragging = LOWORD(lParam);
                     if(stepLeft() != 0) {
                        if(isDragging) {
                           ShowWindow(currentActive, SW_SHOWNA);
                           ShowOwnedPopups(currentActive, SW_SHOWNA);
                        }
                        isDragging = FALSE;
                        if(noMouseWrap)
                           SetCursorPos(pt.x + warpLength, pt.y);
                        else
                           SetCursorPos(screenWidth-warpLength, pt.y);
                     }
                     warpMultiplier = 0;
                  }
                  ResumeThread(mouseThread);
                  break;

               case VW_MOUSERIGHT:
                  warpMultiplier++;
                  if((warpMultiplier >= configMultiplier)) {
                     isDragging = LOWORD(lParam);
                     if(stepRight() != 0) {
                        if(isDragging) {
                           ShowWindow(currentActive, SW_SHOWNA);
                           ShowOwnedPopups(currentActive, SW_SHOWNA);
                        }
                        isDragging = FALSE;
                        if(noMouseWrap)
                           SetCursorPos(pt.x - warpLength, pt.y);
                        else
                           SetCursorPos(warpLength, pt.y);
                     }
                     warpMultiplier = 0;
                  }
                  ResumeThread(mouseThread);
                  break;

               case VW_MOUSEUP:
                  warpMultiplier++;
                  if((warpMultiplier >= configMultiplier)) {
                     int switchVal;
                     isDragging = LOWORD(lParam);
                     if(invertY)
                        switchVal = stepDown();
                     else
                        switchVal = stepUp();
                     if(switchVal != 0) {
                        if(isDragging) {
                           ShowWindow(currentActive, SW_SHOWNA);
                           ShowOwnedPopups(currentActive, SW_SHOWNA);
                        }
                        isDragging = FALSE;
                        if(noMouseWrap)
                           SetCursorPos(pt.x, pt.y + warpLength);
                        else
                           SetCursorPos(pt.x, screenHeight - warpLength);
                     }
                     warpMultiplier = 0;
                  }
                  ResumeThread(mouseThread);
                  break;

               case VW_MOUSEDOWN:
                  warpMultiplier++;
                  if((warpMultiplier >= configMultiplier)) {
                     // Try to avoid switching if we press on the taskbar
                     if(!(LOWORD(lParam) && (GetForegroundWindow() == FindWindow("Shell_traywnd", "")))) {
                        int switchVal;
                        isDragging = LOWORD(lParam);
                        if(invertY)
                           switchVal = stepUp();
                        else
                           switchVal = stepDown();
                        if(switchVal != 0) {
                           if(isDragging) {
                              ShowWindow(currentActive, SW_SHOWNA);
                              ShowOwnedPopups(currentActive, SW_SHOWNA);
                           }
                           isDragging = FALSE;
                           if(noMouseWrap)
                              SetCursorPos(pt.x, pt.y - warpLength);
                           else
                              SetCursorPos(pt.x, warpLength);
                        }
                     }
                     warpMultiplier = 0;
                  }
                  ResumeThread(mouseThread);
                  break;

               case VW_MOUSERESET:
                  warpMultiplier = 0;
            }
            ResumeThread(mouseThread);
         }
     skipMouseWarp:  // goto label for skipping mouse stuff
         
         return TRUE;

      case WM_HOTKEY:				// A hot key was pressed
         // Sticky hot key
         if(wParam == stickyKey) {
            toggleActiveSticky();
            break;
         }
         // Desktop Hot keys
         else if(wParam == vw1) {
            gotoDesk(1);
            break;
         }
         else if(wParam == vw2) {
            gotoDesk(2);
            break;
         }
         else if(wParam == vw3) {
            gotoDesk(3);
            break;
         }
         else if(wParam == vw4) {
            gotoDesk(4);
            break;
         }
         else if(wParam == vw5) {
            gotoDesk(5);
            break;
         }
         else if(wParam == vw6) {
            gotoDesk(6);
            break;
         }
         else if(wParam == vw7) {
            gotoDesk(7);
            break;
         }
         else if(wParam == vw8) {
            gotoDesk(8);
            break;
         }
         else if(wParam == vw9) {
            gotoDesk(9);
            break;
         }
         else if(wParam == vwMenu && hotkeyMenuEn == TRUE) {
            if(enabled) {
               hpopup = createSortedWinList(2);
               GetCursorPos(&pt);
               SetForegroundWindow(aHWnd);
               
               retItem = TrackPopupMenu(hpopup, TPM_RETURNCMD |  // Return menu code
                                        TPM_LEFTBUTTON, (pt.x-2), (pt.y-2), // screen coordinates
                                        0, aHWnd, NULL);
               if(retItem) {
                  if(retItem < (2 * MAXWIN)) { // Sticky toggle
                     if(winList[retItem - MAXWIN].Sticky)
                        winList[retItem - MAXWIN].Sticky = FALSE;
                     else {
                        winList[retItem - MAXWIN].Sticky = TRUE; // mark sticky..
                        safeShowWindow(winList[retItem - MAXWIN].Handle, SW_SHOWNA); //.. and show it now
                     }
                  } else if(retItem < (MAXWIN * 3)) { // window access
                     gotoDesk(winList[retItem -  (2 * MAXWIN)].Desk);
                     forceForeground(winList[retItem - (2 * MAXWIN)].Handle);
                  } else { // Assign to this desktop
                     safeShowWindow(winList[retItem - (3 * MAXWIN)].Handle, SW_SHOWNA);
                     forceForeground(winList[retItem - (3 * MAXWIN)].Handle);
                  }
               }
               
               PostMessage(aHWnd, 0, 0, 0);  // see above
               DestroyMenu(hpopup);       // Delete loaded menu and reclaim its resources
            }
            
            break;
         }
         // Cycling hot keys
         else if(wParam == cyclingKeyUp) {
            if(currentDesk == (nDesksX * nDesksY)) {
               if(deskWrap)
                  gotoDesk(1);
            } else 
               gotoDesk(currentDesk + 1);
            break;
         }
         else if(wParam == cyclingKeyDown) {
            if(currentDesk == 1) {
               if(deskWrap)
                  gotoDesk(nDesksX * nDesksY);
            } else 
               gotoDesk(currentDesk - 1);
            break;
         }
         // Desk step hot keys
         switch HIWORD(lParam) {
            case VK_LEFT:
               stepLeft();
               break;
            case VK_RIGHT:
               stepRight();
               break;
            case VK_UP:
               if(invertY)
                  stepDown();
               else
                  stepUp();
               break;
            case VK_DOWN:
               if(invertY)
                  stepUp();
               else
                  stepDown();
               break;
         }

         return TRUE;
    
         // Plugin messages
      case VW_CHANGEDESK: 
         switch (wParam) {
            case VW_STEPLEFT:
               stepLeft();
               break;
            case VW_STEPRIGHT:
               stepRight();
               break;
            case VW_STEPUP:
               stepUp();
               break;
            case VW_STEPDOWN:
               stepDown();
               break;
            default:
               gotoDesk(wParam);
               break;
         }
         return TRUE;
    
      case VW_CLOSE: 
         shutDown();
         return TRUE;
    
      case VW_SETUP:
         showSetup();
         return TRUE;
    
      case VW_DELICON:
         Shell_NotifyIcon(NIM_DELETE, &nIconD); // This removes the icon
         return TRUE;
    
      case VW_HELP:
         WinHelp(aHWnd, vwHelp, HELP_CONTENTS, 0);
         return TRUE;
    
      case VW_GATHER:
         showAll();
         return TRUE;
    
      case VW_DESKX:
         return nDesksX;
    
      case VW_DESKY:
         return nDesksY;

      case VW_WINLIST:
      {
         // Send over the window list with WM_COPYDATA
         COPYDATASTRUCT cds;         
         cds.dwData = nWin;
         cds.cbData = sizeof(winList);
         cds.lpData = (void*)winList;   
         sendModuleMessage(WM_COPYDATA, 0, (LPARAM)&cds); 
         return TRUE;
      }
    
      // End plugin messages
    
      case WM_CREATE:		       // when main window is created
         // set the timer in ms
         SetTimer(aHWnd, 0x29a, 250, TimerProc); 
         // register message for explorer/systray crash restart
         // only works with >= IE4.0 
         taskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));
         return TRUE;

      case WM_ENDSESSION:
         if(wParam) {
            shutDown();
         }
         return TRUE;
  
      case WM_DESTROY:	  // when application is closed
         shutDown();            
         return TRUE;

      case UWM_SYSTRAY:		   // We are being notified of mouse activity over the icon
         switch (lParam) {
            case WM_RBUTTONUP:		   // Let's track a popup menu
               GetCursorPos(&pt);
               hmenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU1));
               hpopup = GetSubMenu(hmenu, 0);
               SetForegroundWindow(aHWnd);
      
               switch (TrackPopupMenu(hpopup, TPM_RETURNCMD |    // Return menu code
                                      TPM_RIGHTBUTTON, (pt.x-2), (pt.y-2), // screen coordinates
                                      0, aHWnd, NULL))
               {
                  case ID_EXIT:	// exit application
                     DestroyWindow(aHWnd);
                     break;
                  case ID_SETUP:	// show setup box
                     showSetup();
                     break;
                  case ID_GATHER:	// gather all windows
                     showAll();
                     break;
                  case ID_HELP:	// show help
                     WinHelp(aHWnd, vwHelp, HELP_CONTENTS, 0);
                     break;
               }
               PostMessage(aHWnd, 0, 0, 0);	
               DestroyMenu(hpopup);  // Delete loaded menu and reclaim its resources
               DestroyMenu(hmenu);		
               break;

            case WM_LBUTTONDBLCLK:		// double click on icon
               if(enabled) {			// disable VirtuaWin
                  strcpy(nIconD.szTip,"VirtuaWin - Disabled"); //Tooltip
                  nIconD.hIcon = icons[0];
                  Shell_NotifyIcon(NIM_MODIFY, &nIconD);
                  unRegisterKeys();
                  unRegisterHotKeys();
                  unRegisterStickyKey();
                  unRegisterCyclingKeys();
                  unRegisterMenuHotKey();
                  KillTimer(aHWnd, 0x29a);
                  enabled = FALSE;
               } else {			        // Enable VirtuaWin
                  strcpy(nIconD.szTip, appName);	// Tooltip
                  setIcon(calculateDesk());
                  if(!registerKeys())
                     MessageBox(aHWnd, "Invalid key modifier combination, check control keys!", 
                                NULL, MB_ICONWARNING);
                  if(!registerHotKeys())
                     MessageBox(aHWnd, "Invalid key modifier combination, check hot keys!", 
                                NULL, MB_ICONWARNING);
                  if(!registerStickyKey())
                     MessageBox(aHWnd, "Invalid key modifier combination, check sticky hot key!", 
                                NULL, MB_ICONWARNING);
                  if(!registerCyclingKeys())
                     MessageBox(aHWnd, "Invalid key modifier combination, check cycling hot keys!", 
                                NULL, MB_ICONWARNING);
                  if(!registerMenuHotKey())
                     MessageBox(aHWnd, "Invalid key modifier combination, check menu hot key!", 
                                NULL, MB_ICONWARNING);
                  SetTimer(aHWnd, 0x29a, 250, TimerProc);  // Set the timer in ms
                  enabled = TRUE;
               }
               break;
      
            case WM_LBUTTONDOWN: // Show the window list
               if(enabled) {
                  hpopup = createWinList();
                  GetCursorPos(&pt);
                  SetForegroundWindow(aHWnd);
        
                  retItem = TrackPopupMenu(hpopup, TPM_RETURNCMD |  // Return menu code
                                           TPM_LEFTBUTTON, (pt.x-2), (pt.y-2), // screen coordinates
                                           0, aHWnd, NULL);
                  if(retItem) {
                     if(retItem < (2 * MAXWIN)) { // Sticky toggle
                        if(winList[retItem - MAXWIN].Sticky)
                           winList[retItem - MAXWIN].Sticky = FALSE;
                        else {
                           winList[retItem - MAXWIN].Sticky = TRUE; // mark sticky..
                           safeShowWindow(winList[retItem - MAXWIN].Handle, SW_SHOWNA); //.. and show it now
                        }
                     } else if(retItem < (MAXWIN * 3)) { // window access
                        gotoDesk(winList[retItem -  (2 * MAXWIN)].Desk);
                        forceForeground(winList[retItem - (2 * MAXWIN)].Handle);
                     } else { // Assign to this desktop
                        safeShowWindow(winList[retItem - (3 * MAXWIN)].Handle, SW_SHOWNA);
                        forceForeground(winList[retItem - (3 * MAXWIN)].Handle);
                     }
                  }
        
                  PostMessage(aHWnd, 0, 0, 0);  // see above
                  DestroyMenu(hpopup);	      // Delete loaded menu and reclaim its resources
               }
               break;
         }
         return TRUE;
         
      default:
         // If taskbar restarted
         if(message == taskbarRestart)
            Shell_NotifyIcon(NIM_ADD, &nIconD);	// This re-adds the icon
         break;
   }
   return DefWindowProc(aHWnd, message, wParam, lParam);
}

/************************************************
 * Show the setup dialog and perform some stuff before and after display
 */
void showSetup()
{
  if(!setupOpen) { // Stupid fix, can't get this modal
    setupOpen = TRUE;
    if(mouseEnable) // Suspend mouse if running
       SuspendThread(mouseThread);
    unRegisterStickyKey();
    unRegisterHotKeys();
    unRegisterKeys();
    unRegisterCyclingKeys();
    unRegisterMenuHotKey();
    readConfig();
    createPropertySheet(hInst, hWnd); // Show the actual dialog
    writeConfig();
    if(!registerKeys())
      MessageBox(hWnd, "Invalid key modifier combination, check control keys!", NULL, MB_ICONWARNING);
    if(!registerHotKeys())
      MessageBox(hWnd, "Invalid key modifier combination, check hot keys!", NULL, MB_ICONWARNING);
    if(!registerStickyKey())
      MessageBox(hWnd, "Invalid key modifier combination, check sticky hot key!", NULL, MB_ICONWARNING);
    if(!registerCyclingKeys())
       MessageBox(hWnd, "Invalid key modifier combination, check cycling hot keys!", NULL, MB_ICONWARNING);
    if(!registerMenuHotKey())
       MessageBox(hWnd, "Invalid key modifier combination, check menu hot key!", NULL, MB_ICONWARNING);
    if(mouseEnable) // Start again, if enabled
       ResumeThread(mouseThread);
    setupOpen = FALSE;
  }
}

/************************************************
 * Callback for the timer. Updates window list and other stuff 
 */
VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT idEvent, DWORD dwTime)
{
   EnumWindows(enumWindowsProc, 0); // Get all windows
   currentActive = GetForegroundWindow();
   if(userDefinedWin)
      findUserWindows();
   packList();	// Clean up the window list
  
   // save the desktop state every minute
   if(crashRecovery) {
      if(saveInterval < 0) {
         saveInterval = 240; // 250 * 240 = 1 min
         saveDesktopState(&nWin, winList);
      }
      saveInterval--;
   }
}

/************************************************
 * Does necessary stuff before shutting down
 */
void shutDown()
{
   if(saveLayoutOnExit)
      saveDesktopConfiguration(&nWin, winList); 
   writeDisabledList(&nOfModules, moduleList);
   unloadModules();
   showAll();	        // gather all windows on exit
   remove(vwLock);       // Remove the lock file, don't bother if this fails
   unRegisterKeys();
   unRegisterHotKeys();
   unRegisterStickyKey();
   unRegisterCyclingKeys();
   unRegisterMenuHotKey();
   Shell_NotifyIcon(NIM_DELETE, &nIconD); // This removes the icon
   if(saveSticky)
      saveStickyWindows(&nWin, winList);
   PostQuitMessage(0);
   KillTimer(hWnd, 0x29a);                // Remove the timer
}

/*************************************************
 * Sets the modifier key(s) for switching desktop with arrow keys
 */
void setKeyMod()
{
  MODKEY = modAlt | modShift | modCtrl | modWin;
}

/*************************************************
 * Sets the modifier key for switching desktop with number mouse
 */
void setMouseKey()
{
  if(mouseModAlt == TRUE)
    MOUSEKEY = VK_MENU;
  else if(mouseModShift == TRUE)
    MOUSEKEY = VK_SHIFT;
  else if(mouseModCtrl == TRUE)
    MOUSEKEY = VK_CONTROL;
}

/*************************************************
 * Helper function for all the step* functions below
 * Does the actual switching work 
 */
void stepDesk()
{
   int x;  
   for (x = 0; x < nWin ; ++x) {
      // Show these windows
      if(winList[x].Desk == currentDesk) {
         safeShowWindow(winList[x].Handle, SW_SHOWNA);
         if(winList[x].Active && keepActive && !isDragging) {
            forceForeground(winList[x].Handle);
            topWindow = winList[x].Handle;
         }
      }
      // Hide these, not iconic or sticky
      else if(!IsIconic(winList[x].Handle) && !winList[x].Sticky) {
         safeShowWindow(winList[x].Handle, SW_HIDE);
      }
      // Hide these, iconic but "Switch minimized" true
      else if(IsIconic(winList[x].Handle) && minSwitch && !winList[x].Sticky) {
         safeShowWindow(winList[x].Handle, SW_HIDE);
      }
   }
   
   if(releaseFocus)     // Release focus maybe?
      SetForegroundWindow(releaseHnd);
   else if(topWindow)   // Raise the active window
      BringWindowToTop(topWindow);
   else {               // Ok, no active candidate, try the first sticky in list
      HWND tmpHwnd = GetTopWindow(NULL); 
      // We are trying to find the sticky window (if any) 
      // with the highest z-order
      while(tmpHwnd = GetNextWindow(tmpHwnd, GW_HWNDNEXT)) {
         for (x = 0; x < nWin; ++x) {
            if (winList[x].Sticky && tmpHwnd == winList[x].Handle) {
               SetForegroundWindow(winList[x].Handle);
               tmpHwnd = NULL; // Yes, brutal exit out of while loop
            }
         }
      }
   }
   topWindow = NULL;
   
   if(refreshOnWarp) // Refresh the desktop 
      RedrawWindow( NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN );
}

/*************************************************
 * Step on desk to the right
 */
int stepRight()
{
   if((currentDeskX + 1) > nDesksX) {
      if(deskWrap) {
         int retVal = gotoDeskXY(1, currentDeskY);
         if(retVal != 0)
            postModuleMessage(MOD_CHANGEDESK, MOD_STEPRIGHT, currentDesk);
         return retVal;
      }
      else
         return 0;
   }

   currentDeskX += 1;
   setIcon(calculateDesk());   
   stepDesk();
   
   postModuleMessage(MOD_CHANGEDESK, MOD_STEPRIGHT, currentDesk);
   return currentDeskX;
}

/*************************************************
 * Step one desk to the left
 */
int stepLeft()
{
   if((currentDeskX - 1) < 1) {
      if(deskWrap) {
         int retVal = gotoDeskXY(nDesksX, currentDeskY);
         if(retVal != 0)
            postModuleMessage(MOD_CHANGEDESK, MOD_STEPLEFT, currentDesk);
         return retVal;
      }
      else
         return 0;
   }
  
   currentDeskX -= 1;
   setIcon(calculateDesk());
   stepDesk();
  
   postModuleMessage(MOD_CHANGEDESK, MOD_STEPLEFT, currentDesk);
   return currentDeskX;
}

/*************************************************
 * Step one desk down
 */
int stepDown()
{
   if((currentDeskY + 1) > nDesksY) {
      if(deskWrap) {
         int retVal = gotoDeskXY(currentDeskX, 1);
         if(retVal != 0)
            postModuleMessage(MOD_CHANGEDESK, MOD_STEPDOWN, currentDesk);
         return retVal;
      }
      else
         return 0;
   }
  
   currentDeskY += 1;
   setIcon(calculateDesk());
   stepDesk();
  
   postModuleMessage(MOD_CHANGEDESK, MOD_STEPDOWN, currentDesk);
   return currentDeskY;
}

/*************************************************
 * Step one desk up
 */
int stepUp()
{
   if((currentDeskY - 1) < 1) {
      if(deskWrap) {
         int retVal = gotoDeskXY(currentDeskX, nDesksY);
         if(retVal != 0)
            postModuleMessage(MOD_CHANGEDESK, MOD_STEPUP, currentDesk);
         return retVal;
      }
      else
         return 0;
   }

   currentDeskY -= 1;
   setIcon(calculateDesk());
   stepDesk();
  
   postModuleMessage(MOD_CHANGEDESK, MOD_STEPUP, currentDesk);
   return currentDeskY;
}

/*************************************************
 * Goto the specified desktop specifying coordinates
 */
int gotoDeskXY(int deskX, int deskY)
{
   int tmpDesk;
   if(deskX > nDesksX || deskY > nDesksY)
      return 0;
  
   currentDeskX = deskX;
   currentDeskY = deskY;
   tmpDesk = gotoDesk(calculateDesk());
   if(tmpDesk != 0)
      setIcon(tmpDesk);
   
   return tmpDesk;
}

/*************************************************
 * Goto a specified desktop specifying desk number
 */
int gotoDesk(int theDesk)
{
   if(theDesk == currentDesk) // No use trying
      return 0;
   if (theDesk > (nDesksY * nDesksX)) // Hey, we can't go there
      return 0;

   currentDesk	= theDesk;

   nIconD.hIcon = icons[currentDesk];
   Shell_NotifyIcon(NIM_MODIFY, &nIconD);

   /* Calculations for getting the x and y positions */
   currentDeskY = (int) ceil((double)currentDesk/(double)nDesksX);
   currentDeskX = nDesksX + currentDesk - (currentDeskY * nDesksX);

   stepDesk();
   
   postModuleMessage(MOD_CHANGEDESK, currentDesk, currentDesk);
   return theDesk;
}

/*************************************************
 * Sets the icon in the systray and updates the currentDesk variable
 */
void setIcon(int theNumber)
{
   nIconD.hIcon = icons[theNumber];
   Shell_NotifyIcon(NIM_MODIFY, &nIconD);
   currentDesk = theNumber;
}

/*************************************************
 * Calculates currentDesktop 
 */
int calculateDesk()
{
   return (currentDeskY * nDesksX) - (nDesksX - currentDeskX);
}

/*************************************************
 * Forces a window into the foreground. Must be done in this way to avoid
 * the flashing in the taskbar insted of actually changing active window.
 */
void forceForeground(HWND* theWin)
{
   DWORD ThreadID1;
   DWORD ThreadID2;

   /* Nothing to do if already in foreground */
   if(theWin == GetForegroundWindow()) {
      return;
   } else {
      /* Get the thread responsible for VirtuaWin,
         and the thread for the foreground window */
      ThreadID1 = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
      ThreadID2 = GetWindowThreadProcessId(hWnd, NULL);
      /* By sharing input state, threads share their concept of
         the active window */
      if(ThreadID1 != ThreadID2) {
         AttachThreadInput(ThreadID1, ThreadID2, TRUE);
         SetForegroundWindow(hWnd); // Set VirtuaWin active. Don't no why, but it seems to work
         AttachThreadInput(ThreadID1, ThreadID2, FALSE);
         SetForegroundWindow(theWin);
      } else {
         SetForegroundWindow(theWin);
      }
   }
}

/*************************************************
 * Inizialize the window list and gets the current active window
 */
void initData()
{
  int x;
  currentActive = GetForegroundWindow();
  for (x = 0; x < MAXWIN; ++x) {
    winList[x].Handle = NULL;
    winList[x].Active = FALSE;
    winList[x].Sticky = FALSE;
    winList[x].Desk = currentDesk;
  }
}

/*************************************************
 * Checks if a window is a previous saved sticky window
 */
BOOL checkIfSavedSticky(HWND* hwnd)
{
   char className[51];
   int i;
   GetClassName(hwnd, className, 50);
   for(i = 0; i < curSticky; ++i) {
      if (!strncmp(stickyList[i].winClassName, className, 50)) {
         free(stickyList[i].winClassName);
         stickyList[i].winClassName = "\n"; // Remove this from list, it is used
         return TRUE;
      }
   }
   return FALSE;
}

/*************************************************
 * Add a window in the list
 */
__inline void integrateWindow(HWND* hwnd)
{
   int style = GetWindowLong(hwnd, GWL_STYLE);
   int exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
   /* Criterias for a window to be handeled by VirtuaWin */
   if (nWin < MAXWIN-1 &&
       !(style & WS_CHILD) &&
       IsWindowVisible(hwnd) &&
       (!GetParent(hwnd) || GetParent(hwnd) == GetDesktopWindow()) &&
       !(exstyle & WS_EX_TOOLWINDOW) //&&
       /*!GetWindow(hwnd, GW_OWNER)*/)
   {
      char buf[99];
      GetClassName(hwnd, buf, 0);
      winList[nWin].Handle = hwnd;
      if(useDeskAssignment) {
         winList[nWin].Desk = checkIfAssignedDesktop(hwnd);
         if(winList[nWin].Desk != currentDesk)
            ShowWindow(hwnd, SW_HIDE);
      } else {
         winList[nWin].Desk = currentDesk;
      } 
      winList[nWin].Sticky = checkIfSavedSticky(hwnd);
      nWin++;
   }
}

/*************************************************
 * Searches for windows in user list
 */
void findUserWindows() 
{
  HWND tmpHnd;
  int i;
  for (i = 0; i < curUser; ++i) {
    if(userList[i].isClass) {
      tmpHnd = FindWindow(userList[i].winNameClass, NULL);
      if(!tmpHnd)
        userList[i].isClass = FALSE;
    }
    else {
      tmpHnd = FindWindow(NULL, userList[i].winNameClass);
      if(!tmpHnd)
        userList[i].isClass = TRUE;
    }

    if(!inWinList(tmpHnd)) {
      winList[nWin].Handle = tmpHnd;
      winList[nWin].Desk = currentDesk;
      nWin++;
    }
  }
}

/*************************************************
 * Callback function. Integrates all enumerated windows
 */
__inline BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) 
{
  if(nWin >= MAXWIN) {
    KillTimer(hWnd, 0x29a);
    enabled = FALSE;
    MessageBox(hWnd, "Oops! Maximum windows reached. \nVirtuaWin has been disabled. \nMail VirtuaWin@home.se and tell me this.", "VirtuaWin", MB_ICONWARNING);
    return FALSE;
  }
  
  if (!inWinList(hwnd))
    integrateWindow(hwnd);
  return TRUE;
}

/*************************************************
 * Returns true if a window is in the list
 */
__inline BOOL inWinList(HWND* hwnd)
{
  int index;
  if (!hwnd)
    return TRUE; // don't add

  for (index = 0; index < nWin; ++index)
    if (winList[index].Handle == hwnd)
      return TRUE;
  return FALSE;
}

/*************************************************
 * Makes all windows visible
 */
void showAll()
{
  int x;
  for (x = 0; x < MAXWIN; ++x) {
    if (IsWindow(winList[x].Handle)) {
      safeShowWindow(winList[x].Handle, SW_SHOWNA);
    }
  }
}

/*************************************************
 * Compact the window list, removes any destroyed windows
 */
void packList()
{
   int i;
   int j;

   for (i = 0; i < nWin; ++i) {
      // remove killed windows
      if(!IsWindow(winList[i].Handle) || 
         (!IsWindowVisible(winList[i].Handle) && winList[i].Desk == currentDesk)) {
         for (j = i; j < nWin - 1; ++j) {
            memcpy(&winList[j], &winList[j + 1], sizeof(windowType));
         }
         memset(&winList[nWin - 1], 0, sizeof(windowType));
         nWin--;
         continue;
      }

      if(IsWindowVisible(winList[i].Handle)) {
         winList[i].Desk = currentDesk;
         if(winList[i].Handle == currentActive)
            winList[i].Active = TRUE;
         else
            winList[i].Active = FALSE;
      }
   }
}

/*************************************************
 * Creates the window list popup
 */
HMENU createWinList()
{
   HMENU        hMenu;         // menu bar handle
   HMENU        subSticky;     // sticky list
   HMENU        subAssign;     // assign list
   HMENU        subDirect;     // direct access list
   int nOfMenus = 0;
   
   /* create the menus */
  
   if(stickyMenu) {
      hMenu = subSticky = createSortedWinList(1);
      nOfMenus++;
   }
   
   if(directMenu) {
      hMenu = subDirect = createSortedWinList(2);
      nOfMenus++;
   }
   
   if(assignMenu) {
      hMenu = subAssign = createSortedWinList(3);
      nOfMenus++;
   }

   if( nOfMenus > 1 ) {
      // Add lists to submenu holder. 
      hMenu     = CreatePopupMenu();
      if(stickyMenu)
         AppendMenu( hMenu, MF_STRING | MF_POPUP, (DWORD)subSticky, "Sticky" );
      if(directMenu)
         AppendMenu( hMenu, MF_STRING | MF_POPUP, (DWORD)subDirect, "Access" );
      if(assignMenu)
         AppendMenu( hMenu, MF_STRING | MF_POPUP, (DWORD)subAssign, "Assign" );
   }

   return hMenu;
}

/*************************************************
 * This function creates a sorted list of windows for direct access menu.
 * If there are windows on more than one desktop than a separator is
 * inserted into the list between desktops
 * The parameter multiplier is the number the WINMAX is multiplied by to get
 * the menu command ID.
 *
 * Author: Matti Jagula <matti@proekspert.ee>
 * Date: 31.07.00
 */
HMENU createSortedWinList(int multiplier)
{
   typedef struct _MenuItems
   {
         char *name;
         long id;
         long desk;
         BOOL sticky;
   } MenuItem;

   HMENU        hMenu;         // menu bar handle
   char title[35];
   MenuItem *items[MAXWIN], *item;
   char buff[31];
   int i,x,y,c;

   hMenu = NULL;

   hMenu = CreatePopupMenu();

   // create the window list
   for(i = 0; i < nWin; ++i)
   {
      GetWindowText(winList[i].Handle, buff, 30);
      sprintf(title, "%d - %s", winList[i].Desk, buff);
      item = malloc( sizeof(MenuItem) );
      item->name = strdup (title);
      item->desk = winList[i].Desk;
      item->sticky = winList[i].Sticky;
      item->id = i;
      items [i]   = item;
      items [i+1] = NULL;
   }
   items [i+2] = NULL; // just in case

   // sorting using bubble sort
   for (x = 0; x < i; x++ )
   {
      for (y = 0; y<i; y++)
      {
         if( strcmp(items[x]->name, items[y]->name) < 0 )
         {
            item = items [x];
            items[x] = items[y];
            items[y] = item;
         }
      }
   }
   y = 0; c = 0;
   for (x=0; x < i; x++ )
   {
      if (!c || c != items[x]->desk)
      {
         if(c) AppendMenu(hMenu, MF_SEPARATOR, 0, NULL );
         c = items [x]->desk;
      }
      
      AppendMenu( hMenu,
                  MF_STRING | (items[x]->sticky ? MF_CHECKED: 0),
                  multiplier * MAXWIN + (items[x]->id), items[x]->name );
      free ( items[x]->name );
      free ( items[x]);
      items [x] = NULL;
   }
   return hMenu;
}

/*************************************************
 * Translates virtual key codes to "hotkey codes"
 */
WORD hotKey2ModKey(BYTE vModifiers)
{
   WORD mod = 0;
   if (vModifiers & HOTKEYF_ALT)
      mod |= MOD_ALT;
   if (vModifiers & HOTKEYF_CONTROL)
      mod |= MOD_CONTROL;
   if (vModifiers & HOTKEYF_SHIFT)
      mod |= MOD_SHIFT;
   return mod;
}

/*************************************************
 * Toggles the active window's stickiness
 */
void toggleActiveSticky()
{
   HWND tempHnd = GetForegroundWindow();
   int i;

   for (i = 0; i < nWin; ++i) {
      if(tempHnd == winList[i].Handle) {
         if(winList[i].Sticky)
            winList[i].Sticky = FALSE;
         else
            winList[i].Sticky = TRUE;
         return;
      }
   }
}

/*************************************************
 * Check if we have a previous lock file, otherwise it creates it
 */
BOOL tryToLock()
{
   if(access(vwLock, 0 == -1)) {
      FILE* fp;
      if(!(fp = fopen(vwLock, "wc"))) {
         MessageBox(hWnd, "Error writing lock file", "VirtuaWin", MB_ICONWARNING);
         return TRUE;
      } else {
         fprintf(fp, "%s", "VirtuaWin LockFile");
      }
    
      fflush(fp); // Make sure the file is physically written to disk
      fclose(fp);
        
      return TRUE;
   } else {
      return FALSE; // We already had a lock file, probably due to a previous crash
   }
}

/*************************************************
 * Tries to find windows saved before a crash by classname matching
 */
void recoverWindows()
{
  char dummy[80];
  int nOfRec = 0;
  HWND tmpHnd;
  char buff[27];
  FILE* fp;
    
  if(fp = fopen(vwState, "r")) {
    while(!feof(fp)) {
      fscanf(fp, "%79s", &dummy);
      if((strlen(dummy) != 0) && !feof(fp)) {
        tmpHnd = FindWindow(dummy, NULL);
        if(tmpHnd) {
          if(safeShowWindow(tmpHnd, SW_SHOWNA) == TRUE)
            nOfRec++;
        }
      }
    }
    fclose(fp);
  }
    
  sprintf(buff, "%d windows was recovered.", nOfRec);
  MessageBox(hWnd, buff, "VirtuaWin", 0); 
}

/*************************************************
 * Checks if a window is an predifined desktop to go to
 */
int checkIfAssignedDesktop(HWND* aHwnd)
{
   char className[51];
   int i;
   GetClassName(aHwnd, className, 50);
   for(i = 0; i < curAssigned; ++i) {
      if (!strncmp(assignedList[i].winClassName, className, 50)) {
         if(assignOnlyFirst) {
            free(assignedList[i].winClassName);
            assignedList[i].winClassName = "\n"; // Remove this from list, it is used
         }
         if( assignedList[i].desktop > (nDesksX * nDesksY))
            MessageBox(hWnd, "Tried to assign an application to an unavaliable desktop.\nIt will not be assigned.\nCheck desktop assignmet configuration.", "VirtuaWin", 0); 
         else
            return assignedList[i].desktop; // Yes, assign
      }
   }
   return currentDesk; // No assignment, return current
}

/************************************************
 * Wraps ShowWindow() and make sure that we won't hang on crashed applications
 * Instead we notify user by flashing the systray icon
 */
BOOL safeShowWindow(HWND* theHwnd, int theState)
{
   if(SendMessageTimeout(theHwnd, (int)NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, NULL)) {
      ShowWindow(theHwnd, theState);
      ShowOwnedPopups(theHwnd, theState);
      return TRUE;
   } else {
      warningIcon(); // Flash the systray icon
      return FALSE;  // Probably hanged
   }
}

/************************************************
 * Starts a timer that flashes the systray icon 5 times
 */
void warningIcon()
{
   SetTimer(hWnd, 0x30a, 500, FlashProc); // set the timer in ms
}

/************************************************
 * Callback for the flash timer
 */
VOID CALLBACK FlashProc(HWND hwnd, UINT uMsg, UINT idEvent, DWORD dwTime)
{
   static int count = 0;
   count++;
   if(count % 2)
      nIconD.hIcon = NULL; // No icon
   else 
      nIconD.hIcon = icons[currentDesk];
   Shell_NotifyIcon(NIM_MODIFY, &nIconD);
   
   if(count > 10) {
      KillTimer(hWnd, 0x30a);
      nIconD.hIcon = icons[currentDesk];
      Shell_NotifyIcon(NIM_MODIFY, &nIconD);
      count = 0;
   }
}

/************************************************
 * Writes down the current configuration on file
 */
void writeConfig()
{
   FILE* fp;

   if((fp = fopen(vwConfig, "w")) == NULL) {
      MessageBox(NULL, "Error writing config file", NULL, MB_ICONWARNING);
   } else {
      fprintf(fp, "Mouse_warp# %i\n", mouseEnable);
      fprintf(fp, "Mouse_delay# %i\n", configMultiplier);
      fprintf(fp, "Key_support# %i\n", keyEnable);
      fprintf(fp, "Release_focus# %i\n", releaseFocus);
      fprintf(fp, "Keep_active# %i\n", keepActive);
      fprintf(fp, "Control_key_alt# %i\n", modAlt);
      fprintf(fp, "Control_key_shift# %i\n", modShift);
      fprintf(fp, "Control_key_ctrl# %i\n", modCtrl);
      fprintf(fp, "Control_key_win# %i\n", modWin);
      fprintf(fp, "Warp_jump# %i\n", warpLength);
      fprintf(fp, "Switch_minimized# %i\n", minSwitch);
      fprintf(fp, "Taskbar_warp# %i\n", taskBarWarp);
      fprintf(fp, "Desk_Ysize# %i\n", nDesksY);
      fprintf(fp, "Desk_Xsize# %i\n", nDesksX);
      fprintf(fp, "Hot_key_support# %i\n", hotKeyEnable);
      fprintf(fp, "Hot_key_1# %i\n", hotkey1);
      fprintf(fp, "Hot_key_Mod1# %i\n", hotkey1Mod);
      fprintf(fp, "Hot_key_Win1# %i\n", hotkey1Win);
      fprintf(fp, "Hot_key_2# %i\n", hotkey2);
      fprintf(fp, "Hot_key_Mod2# %i\n", hotkey2Mod);
      fprintf(fp, "Hot_key_Win2# %i\n", hotkey2Win);
      fprintf(fp, "Hot_key_3# %i\n", hotkey3);
      fprintf(fp, "Hot_key_Mod3# %i\n", hotkey3Mod);
      fprintf(fp, "Hot_key_Win3# %i\n", hotkey3Win);
      fprintf(fp, "Hot_key_4# %i\n", hotkey4);
      fprintf(fp, "Hot_key_Mod4# %i\n", hotkey4Mod);
      fprintf(fp, "Hot_key_Win4# %i\n", hotkey4Win);
      fprintf(fp, "Hot_key_5# %i\n", hotkey5);
      fprintf(fp, "Hot_key_Mod5# %i\n", hotkey5Mod);
      fprintf(fp, "Hot_key_Win5# %i\n", hotkey5Win);
      fprintf(fp, "Hot_key_6# %i\n", hotkey6);
      fprintf(fp, "Hot_key_Mod6# %i\n", hotkey6Mod);
      fprintf(fp, "Hot_key_Win6# %i\n", hotkey6Win);
      fprintf(fp, "Hot_key_7# %i\n", hotkey7);
      fprintf(fp, "Hot_key_Mod7# %i\n", hotkey7Mod);
      fprintf(fp, "Hot_key_Win7# %i\n", hotkey7Win);
      fprintf(fp, "Hot_key_8# %i\n", hotkey8);
      fprintf(fp, "Hot_key_Mod8# %i\n", hotkey8Mod);
      fprintf(fp, "Hot_key_Win8# %i\n", hotkey8Win);
      fprintf(fp, "Hot_key_9# %i\n", hotkey9);
      fprintf(fp, "Hot_key_Mod9# %i\n", hotkey9Mod);
      fprintf(fp, "Hot_key_Win9# %i\n", hotkey9Win);
      fprintf(fp, "Mouse_control_key_support# %i\n", useMouseKey);
      fprintf(fp, "Mouse_key_alt# %i\n", mouseModAlt);
      fprintf(fp, "Mouse_key_shift# %i\n", mouseModShift);
      fprintf(fp, "Mouse_key_ctrl# %i\n", mouseModCtrl);
      fprintf(fp, "Save_sticky_info# %i\n", saveSticky);
      fprintf(fp, "Refresh_after_warp# %i\n", refreshOnWarp);
      fprintf(fp, "No_mouse_wrap# %i\n", noMouseWrap);
      fprintf(fp, "Sticky_modifier# %i\n", VW_STICKYMOD);
      fprintf(fp, "Sticky_key# %i\n", VW_STICKY);
      fprintf(fp, "Crash_recovery# %i\n", crashRecovery);
      fprintf(fp, "Desktop_cycling# %i\n", deskWrap);
      fprintf(fp, "Invert_Y# %i\n", invertY);
      fprintf(fp, "WinMenu_sticky# %i\n", stickyMenu);
      fprintf(fp, "WinMenu_assign# %i\n", assignMenu);
      fprintf(fp, "WinMenu_direct# %i\n", directMenu);
      fprintf(fp, "Desktop_assignment# %i\n", useDeskAssignment);
      fprintf(fp, "Save_layout# %i\n", saveLayoutOnExit);
      fprintf(fp, "Assign_first# %i\n", assignOnlyFirst);
      fprintf(fp, "UseCyclingKeys# %i\n", cyclingKeysEnabled);
      fprintf(fp, "CycleUp# %i\n", hotCycleUp);
      fprintf(fp, "CycleUpMod# %i\n", hotCycleUpMod);
      fprintf(fp, "CycleDown# %i\n", hotCycleDown);
      fprintf(fp, "CycleDownMod# %i\n", hotCycleDownMod);
      fprintf(fp, "Hot_key_Menu_Support# %i\n", hotkeyMenuEn);
      fprintf(fp, "Hot_key_Menu# %i\n", hotkeyMenu);
      fprintf(fp, "Hot_key_ModMenu# %i\n", hotkeyMenuMod);
      fprintf(fp, "Hot_key_WinMenu# %i\n", hotkeyMenuWin);

      fclose(fp);
   }
   postModuleMessage(MOD_CFGCHANGE, 0, 0);
}

/*************************************************
 * Reads a saved configuration from file
 */
void readConfig()
{
   char dummy[80];
   FILE* fp;

   if((fp = fopen(vwConfig, "r")) == NULL) {
      MessageBox(NULL, "Error reading config file. This is probably due to new user setup.\nA new config file will be created.", NULL, MB_ICONWARNING);
      // Try to create new file
      if((fp = fopen(vwConfig, "w")) == NULL) {
         MessageBox(NULL, "Error writing new config file. Check writepermissions.", NULL, MB_ICONWARNING);
      }
   } else {   
      fscanf(fp, "%s%i", &dummy, &mouseEnable);
      fscanf(fp, "%s%i", &dummy, &configMultiplier);
      fscanf(fp, "%s%i", &dummy, &keyEnable);
      fscanf(fp, "%s%i", &dummy, &releaseFocus);
      fscanf(fp, "%s%i", &dummy, &keepActive);
      fscanf(fp, "%s%i", &dummy, &modAlt);
      fscanf(fp, "%s%i", &dummy, &modShift);
      fscanf(fp, "%s%i", &dummy, &modCtrl);
      fscanf(fp, "%s%i", &dummy, &modWin);
      fscanf(fp, "%s%i", &dummy, &warpLength);
      fscanf(fp, "%s%i", &dummy, &minSwitch);
      fscanf(fp, "%s%i", &dummy, &taskBarWarp);
      fscanf(fp, "%s%i", &dummy, &nDesksY);
      fscanf(fp, "%s%i", &dummy, &nDesksX);
      fscanf(fp, "%s%i", &dummy, &hotKeyEnable);
      fscanf(fp, "%s%i", &dummy, &hotkey1);
      fscanf(fp, "%s%i", &dummy, &hotkey1Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey1Win);
      fscanf(fp, "%s%i", &dummy, &hotkey2);
      fscanf(fp, "%s%i", &dummy, &hotkey2Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey2Win);
      fscanf(fp, "%s%i", &dummy, &hotkey3);
      fscanf(fp, "%s%i", &dummy, &hotkey3Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey3Win);
      fscanf(fp, "%s%i", &dummy, &hotkey4);
      fscanf(fp, "%s%i", &dummy, &hotkey4Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey4Win);
      fscanf(fp, "%s%i", &dummy, &hotkey5);
      fscanf(fp, "%s%i", &dummy, &hotkey5Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey5Win);
      fscanf(fp, "%s%i", &dummy, &hotkey6);
      fscanf(fp, "%s%i", &dummy, &hotkey6Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey6Win);
      fscanf(fp, "%s%i", &dummy, &hotkey7);
      fscanf(fp, "%s%i", &dummy, &hotkey7Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey7Win);
      fscanf(fp, "%s%i", &dummy, &hotkey8);
      fscanf(fp, "%s%i", &dummy, &hotkey8Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey8Win);
      fscanf(fp, "%s%i", &dummy, &hotkey9);
      fscanf(fp, "%s%i", &dummy, &hotkey9Mod);
      fscanf(fp, "%s%i", &dummy, &hotkey9Win);
      fscanf(fp, "%s%i", &dummy, &useMouseKey);
      fscanf(fp, "%s%i", &dummy, &mouseModAlt);
      fscanf(fp, "%s%i", &dummy, &mouseModShift);
      fscanf(fp, "%s%i", &dummy, &mouseModCtrl);
      fscanf(fp, "%s%i", &dummy, &saveSticky);
      fscanf(fp, "%s%i", &dummy, &refreshOnWarp);
      fscanf(fp, "%s%i", &dummy, &noMouseWrap);
      fscanf(fp, "%s%i", &dummy, &VW_STICKYMOD);
      fscanf(fp, "%s%i", &dummy, &VW_STICKY);
      fscanf(fp, "%s%i", &dummy, &crashRecovery);
      fscanf(fp, "%s%i", &dummy, &deskWrap);
      fscanf(fp, "%s%i", &dummy, &invertY);
      fscanf(fp, "%s%i", &dummy, &stickyMenu);
      fscanf(fp, "%s%i", &dummy, &assignMenu);
      fscanf(fp, "%s%i", &dummy, &directMenu);
      fscanf(fp, "%s%i", &dummy, &useDeskAssignment);
      fscanf(fp, "%s%i", &dummy, &saveLayoutOnExit);
      fscanf(fp, "%s%i", &dummy, &assignOnlyFirst);
      fscanf(fp, "%s%i", &dummy, &cyclingKeysEnabled);
      fscanf(fp, "%s%i", &dummy, &hotCycleUp);
      fscanf(fp, "%s%i", &dummy, &hotCycleUpMod);
      fscanf(fp, "%s%i", &dummy, &hotCycleDown);
      fscanf(fp, "%s%i", &dummy, &hotCycleDownMod);
      fscanf(fp, "%s%i", &dummy, &hotkeyMenuEn);
      fscanf(fp, "%s%i", &dummy, &hotkeyMenu);
      fscanf(fp, "%s%i", &dummy, &hotkeyMenuMod);
      fscanf(fp, "%s%i", &dummy, &hotkeyMenuWin);
      
      fclose(fp);
   }
}

/*
 * $Log$
 * Revision 1.5  2000/08/28 21:38:37  jopi
 * Added new functions for menu hot key registration. Fixed bug with needing to have hot keys enabled for menu keys to work and also better error message
 *
 * Revision 1.4  2000/08/19 15:00:26  jopi
 * Added multiple user setup support (Alasdair McCaig) and fixed creation of setup file if it don't exist
 *
 * Revision 1.3  2000/08/18 23:43:08  jopi
 *  Minor modifications by Matti Jagula <matti@proekspert.ee> List of modifications follows: Added window title sorting in popup menus (Assign, Direct, Sticky) Added some controls to Setup Misc tab and support for calling the popup menus from keyboard.
 *
 * Revision 1.2  2000/08/18 21:41:31  jopi
 * Added the code again that removes closed windows, this will avoid having closed child windows reappearing again. Also updated the mail adress
 *
 * Revision 1.1.1.1  2000/06/03 15:38:05  jopi
 * Added first time
 *
 */

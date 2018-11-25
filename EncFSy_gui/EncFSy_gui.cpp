// EncFSy_gui.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "EncFSy.h"
#include "EncFSy_gui.h"

#include <crtdbg.h>
#include <stdio.h>
#include <shlobj.h>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ENCFSYGUI, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ENCFSYGUI));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ENCFSYGUI));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_ENCFSYGUI);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED) {
		SendMessage(hwnd, BFFM_SETSELECTION, (WPARAM)TRUE, lpData);
	}
	return 0;
}

static TCHAR dirname[MAX_PATH];
static int driveIx;

// Message handler for about box.
INT_PTR CALLBACK PasswordDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			WCHAR password[256];
			GetDlgItemText(hDlg, IDC_PASSWORD, password, sizeof password);

			EndDialog(hDlg, LOWORD(wParam));

			WCHAR drive[2];
			drive[0] = driveIx + L'A';
			drive[1] = L'\0';
			EncFSOptions efo;
			ZeroMemory(&efo, sizeof(EncFSOptions));
			efo.Timeout = 30000;
			wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), drive);
			wcscpy_s(efo.RootDirectory, sizeof(efo.RootDirectory) / sizeof(WCHAR), dirname);

			_RPTWN(_CRT_WARN, L"DDP %s %s %s\n", drive, dirname, password);

			char cPassword[256];
			for (int i = 0; i < sizeof cPassword; ++i) {
				cPassword[i] = (char)password[i];
			}
			StartEncFS(efo, cPassword);

			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

BOOL ChooseDirectory(HWND hDlg)
{
	HWND hList;
	hList = GetDlgItem(hDlg, IDC_LIST);
	driveIx = (int)(DWORD)SendMessage(hList, LB_GETCURSEL, 0L, 0L);

	dirname[0] = TEXT('\0');
	/*

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrInitialDir = _TEXT("C:\\");
	ofn.lpstrFile = dirname;
	ofn.nMaxFile = sizeof dirname;
	ofn.lpstrFilter = _TEXT("TXTファイル(*.TXT)\0*.TXT\0") _TEXT("全てのファイル(*.*)\0*.*\0");
	ofn.lpstrDefExt = _TEXT("TXT");
	ofn.lpstrTitle = _TEXT("Select a EncFS directory");
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
	BOOL ret = GetOpenFileNameW(&ofn);
	*/

	BROWSEINFO bInfo;
	LPITEMIDLIST pIDList;

	memset(&bInfo, 0, sizeof(bInfo));
	bInfo.hwndOwner = hDlg;
	bInfo.pidlRoot = NULL;
	bInfo.pszDisplayName = dirname;
	bInfo.lpszTitle = TEXT("Select a EncFS directory");
	bInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE | BIF_NEWDIALOGSTYLE;
	bInfo.lpfn = BrowseCallbackProc;
	bInfo.lParam = (LPARAM)dirname;
	pIDList = SHBrowseForFolder(&bInfo);
	SHGetPathFromIDList(pIDList, dirname);

	DialogBoxW(hInst, MAKEINTRESOURCE(IDD_PASSWORD), hDlg, PasswordDialogProc);

	CoTaskMemFree(pIDList);

	return TRUE;
}

INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		SetMenu(hDlg, LoadMenu(hInst, MAKEINTRESOURCEW(IDC_ENCFSYGUI)));

		HWND hList;
		hList = GetDlgItem(hDlg, IDC_LIST);
		WCHAR drive[2];
		drive[1] = '\0';
		WCHAR letter;
		for (letter = 'A'; letter <= 'Z'; ++letter) {
			drive[0] = letter;
			SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)drive);
		}

		return (INT_PTR)TRUE;
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == ID_SELECT_FOLDER) {
			ChooseDirectory(hDlg);
		}
		//_RPTWN(_CRT_WARN, L"Message %d\n", LOWORD(wParam));
		break;
	case WM_CLOSE:
		EndDialog(hDlg, LOWORD(wParam));
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}
	return (INT_PTR)FALSE;
}

// Message handler for about box.
INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   DialogBoxW(hInst, MAKEINTRESOURCE(IDD_MAIN), nullptr, MainDialogProc);

   /*
   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);
   */

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
			switch (wmId)
			{
			case IDM_MOUNT:
				if (!ChooseDirectory(hWnd)) {
					DestroyWindow(hWnd);
				}
				break;
			case IDM_ABOUT:
				DialogBoxW(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutDialogProc);
				break;
			case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
     case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


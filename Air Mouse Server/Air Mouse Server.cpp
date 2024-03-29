#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "stdafx.h"

#include <WinSock2.h>
#include <thread>

#include<iostream>

#pragma comment(lib, "Ws2_32.lib")

#define EXE_VERSION L"v5.0.1"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 400

#define COMPATIBILITY_CODE 8

#define PORT 6666
#define KEY_PORT 6667
#define PROBE_RESPONSE_PORT 7777

#define MOUSE_MOVE -9
#define KB_INPUT -10

#define MAX_LOADSTRING 100
#define WM_TRAY (WM_USER + 1)


#define STATUS_UNKNOWN -1
#define STATUS_OK 0
#define STATUS_ERROR_BIND 1
#define STATUS_ERROR_OTHER 2

//#include <fstream>
//#include <iostream>
//#include <chrono>
//std::ofstream fs(".\\air-mouse.log");
//using namespace std::chrono;

//	TODO: Allow adding macros
//	TODO: Add an interrupt handler for exit

const char PREFIX[] = { 87, 52, 109, 98, 68 };

const int SCREEN_WIDTH = GetSystemMetrics(SM_CXVIRTUALSCREEN);
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYVIRTUALSCREEN);

HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

NOTIFYICONDATA nid = { 0 };

SOCKET mouse_socket, broadcastListenerSocket, kb_sock;

int mouse_status = STATUS_UNKNOWN;
int kb_status = STATUS_UNKNOWN;

BOOL isRunAsAdministrator() {
	BOOL fIsRunAsAdmin = FALSE;
	DWORD dwError = ERROR_SUCCESS;
	PSID pAdministratorsGroup = NULL;

	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
		dwError = GetLastError();
		goto Cleanup;
	}

	if (!CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin)) {
		dwError = GetLastError();
		goto Cleanup;
	}

Cleanup:
	if (pAdministratorsGroup) {
		FreeSid(pAdministratorsGroup);
		pAdministratorsGroup = NULL;
	}
	if (ERROR_SUCCESS != dwError) {
		throw dwError;
	}
	return fIsRunAsAdmin;
}

void MouseSetup(INPUT *buffer) {
	buffer->type = INPUT_MOUSE;
	buffer->mi.dx = 0;
	buffer->mi.dy = 0;
	buffer->mi.mouseData = 0;
	buffer->mi.dwFlags = MOUSEEVENTF_ABSOLUTE;
	buffer->mi.time = 0;
	buffer->mi.dwExtraInfo = 0;
}

void KeyboardSetup(INPUT *buffer) {
	buffer->type = INPUT_KEYBOARD;
	buffer->ki.dwFlags = 0;
	buffer->ki.wScan = 0;
	buffer->ki.time = 0;
	buffer->ki.dwExtraInfo = 0;
}

void displayMessage(LPCWSTR title, LPCWSTR message, long icon) {
	MessageBox(NULL, message, title, icon);
}

void printStatus(HDC hdc, int x, int y, int status) {
	SetTextColor(hdc, RGB(255, 255, 255));
	TextOut(hdc, x, y, L":", 1);
	x += 15;
	switch (status) {
	case STATUS_OK: {
		SetTextColor(hdc, RGB(0, 150, 136));
		TextOut(hdc, x, y, L"READY", 5);
		break;
	}
	case STATUS_ERROR_BIND: {
		SetTextColor(hdc, RGB(255, 0, 0));
		TextOut(hdc, x, y, L"BIND_ERROR", 10);
		break;
	}
	case STATUS_ERROR_OTHER: {
		SetTextColor(hdc, RGB(255, 0, 0));
		TextOut(hdc, x, y, L"ERROR", 5);
		break;
	}
	case STATUS_UNKNOWN: {
		SetTextColor(hdc, RGB(255, 0, 0));
		TextOut(hdc, x, y, L"UNKNOWN", 7);
		break;
	}
	}
}

void repaint() {
	SendMessage(FindWindow(szWindowClass, szTitle), WM_COMMAND, LOWORD(ID_REPAINT), NULL);
}

void setStatus(int &object, int status) {
	object = status;
	repaint();
}


size_t getPostPrefixIndex(char buff[], size_t size) {
	int i = 0;
	while(i < 5 && i < size) {
		if(PREFIX[i] != buff[i]) {
			return size;
		}
		i++;
	}

	return i;
}

bool invalidPrefix(char buff[], size_t size) {
	for (int i = 0; i < 5; i++) {
		if (i == size || PREFIX[i] != buff[i]) {
			return true;
		}
	}

	return false;
}

void ProbeResponseThread() {
	broadcastListenerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);		//IPPROTO_UDP  (17)  can use 0 to let driver(?) decide
	if (broadcastListenerSocket == INVALID_SOCKET) {
		return;
	}
	

	struct sockaddr_in broadcastAddr;
	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
	broadcastAddr.sin_port = htons(PROBE_RESPONSE_PORT);

	sockaddr_in client_addr;

	int caddr_size = sizeof(client_addr);
	
	char recvBuf[8];
	memset(recvBuf, 0, 8);

	char ver[2] = { COMPATIBILITY_CODE & 0xff, COMPATIBILITY_CODE >> 8 };

	SOCKET announceSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	setsockopt(announceSocket, SOL_SOCKET, SO_BROADCAST, "1", 1);

	sendto(announceSocket, ver, (int) strlen(ver), 0, (SOCKADDR*) &broadcastAddr, sizeof(broadcastAddr));

	closesocket(announceSocket);

	sockaddr_in listen_addr;
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons(PROBE_RESPONSE_PORT);
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// INADDR_ANY=0.0.0.0
	if (bind(broadcastListenerSocket, (SOCKADDR*)&listen_addr, sizeof(listen_addr))) {
		closesocket(broadcastListenerSocket);
		return;
	}

	//closesocket(announceSocket);	// Cannot close!
	while (int size = recvfrom(broadcastListenerSocket, recvBuf, 8, 0, (SOCKADDR*)&client_addr, &caddr_size) != SOCKET_ERROR) {
		if(size > 1) {
			uint16_t clientCode;
			memcpy(&clientCode, &recvBuf, 2);
			if (clientCode > COMPATIBILITY_CODE) {
				std::thread msg_thread(displayMessage, L"Desktop application outdated",
					L"To get latest features and avoid compatibility issues, download the latest version of this application from:\nhttps://goo.gl/m233tM", MB_ICONWARNING);
				msg_thread.detach();
			}
			else if (clientCode < COMPATIBILITY_CODE) {
				std::thread msg_thread(displayMessage, L"Android application outdated",
					L"Please update the app to the latest version from the PlayStore to avoid compatibility issues.", MB_ICONWARNING);
				msg_thread.detach();
			}
		}
		else {
			//printf(inet_ntoa(client_addr.sin_addr));
			/*int si = MultiByteToWideChar(CP_ACP, 0, inet_ntoa(client_addr.sin_addr), -1, NULL, 0);
			WCHAR* message = new WCHAR[si];
			si = MultiByteToWideChar(CP_ACP, 0, inet_ntoa(client_addr.sin_addr), -1, message, si);
			displayMessage(L"Title", message, MB_OK);
			delete[] message;*/

			sendto(broadcastListenerSocket, ver, (int) strlen(ver), 0, (SOCKADDR*) &client_addr, sizeof(client_addr));
		}
		memset(&client_addr, 0, caddr_size);
	}
	closesocket(broadcastListenerSocket);
}

void KeyInputThread() {
	kb_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (kb_sock == INVALID_SOCKET) {
		setStatus(kb_status, STATUS_ERROR_OTHER);
		return;
	}
	sockaddr_in bindaddr;
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(KEY_PORT);
	bindaddr.sin_addr.s_addr = inet_addr("0.0.0.0");
	if (bind(kb_sock, (SOCKADDR*)&bindaddr, sizeof(bindaddr))) {
		setStatus(kb_status, STATUS_ERROR_BIND);
		closesocket(kb_sock);
		return;
	}

	INPUT* inputArr[10];
	int sizes[sizeof(inputArr) / sizeof(inputArr[0])];
	//INPUT **inputArr = (INPUT **)malloc(sizeof(INPUT*) * INPUT_COUNT);
	//printf("Sizes: %d %d\n", sizeof(inputArr), sizeof(inputArr)/ sizeof(inputArr[0]));
	for (int i = 0; i < sizeof(inputArr) / sizeof(inputArr[0]); i++) {
		inputArr[i] = (INPUT*) malloc(sizeof(INPUT));
		sizes[i] = 1;
		MouseSetup(inputArr[i]);
	}

	/* Left MB Pressed */
	inputArr[0]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN);

	/* Left MB Released */
	inputArr[1]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP);

	/* Right MB Pressed */
	inputArr[2]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTDOWN);

	/* Right MB Released */
	inputArr[3]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTUP);

	/* Wheel Down 1 click */
	inputArr[4]->mi.dwFlags = MOUSEEVENTF_WHEEL;
	inputArr[4]->mi.mouseData = -WHEEL_DELTA;	 // WHEEL_DELTA=120 (One wheel click)

	/* Wheel Up 1 click */
	inputArr[5]->mi.dwFlags = MOUSEEVENTF_WHEEL;
	inputArr[5]->mi.mouseData = WHEEL_DELTA;

	INPUT* tmp;

	/* Left click */
	sizes[6] = 2;
	tmp = (INPUT*) realloc(inputArr[6], sizeof(INPUT) * 2);
	if(tmp != NULL) {
		inputArr[6] = tmp;
	}
	MouseSetup(&inputArr[6][0]);
	MouseSetup(&inputArr[6][1]);
	inputArr[6][0] = *inputArr[0];
	inputArr[6][1] = *inputArr[1];

	/* Right click */
	sizes[7] = 2;
	tmp = (INPUT*)realloc(inputArr[7], sizeof(INPUT) * 2);
	if (tmp != NULL) {
		inputArr[7] = tmp;
	}
	MouseSetup(&inputArr[7][0]);
	MouseSetup(&inputArr[7][1]);
	inputArr[7][0] = *inputArr[2];
	inputArr[7][1] = *inputArr[3];

	/* Double click */
	sizes[8] = 4;
	tmp = (INPUT*)realloc(inputArr[8], sizeof(INPUT) * 4);
	if (tmp != NULL) {
		inputArr[8] = tmp;
	}
	MouseSetup(&inputArr[8][0]);
	MouseSetup(&inputArr[8][1]);
	MouseSetup(&inputArr[8][2]);
	MouseSetup(&inputArr[8][3]);
	inputArr[8][0] = *inputArr[0];
	inputArr[8][1] = *inputArr[1];
	inputArr[8][2] = *inputArr[0];
	inputArr[8][3] = *inputArr[1];

	/* Move mouse to center */
	inputArr[9]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE);
	inputArr[9]->mi.dx = 32768;
	inputArr[9]->mi.dy = 32768;


	INPUT kbInput;
	KeyboardSetup(&kbInput);

	/* Keyboard Character Input */
	INPUT kbInputChar;
	kbInputChar.type = INPUT_KEYBOARD;
	kbInputChar.ki.dwFlags = KEYEVENTF_UNICODE;
	kbInputChar.ki.time = 0;
	kbInputChar.ki.dwExtraInfo = 0;
	kbInputChar.ki.wVk = 0;

	INPUT mInput;
	MouseSetup(&mInput);
	mInput.mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE);

	size_t size;
	size_t i = 0;
	char buff[512];

	setStatus(kb_status, STATUS_OK);

	while ((size = recv(kb_sock, buff, 512, 0)) != SOCKET_ERROR) {
		/*if (size == 1) {	// For quickness maybe
			SendInput(sizes[buff[0]], inputArr[buff[0]], inputSize);
			continue;
		}*/
		i = getPostPrefixIndex(buff, size);
		while (i < size) {
			if (buff[i] == KB_INPUT && (i + 2) < size) {
				if (buff[i + 1] == KEYEVENTF_UNICODE) {
					kbInput.ki.dwFlags = KEYEVENTF_UNICODE;
					kbInput.ki.wVk = 0;
					kbInput.ki.wScan = buff[i + 2];
				}
				else {
					kbInput.ki.dwFlags = buff[i + 1];
					kbInput.ki.wVk = ((uint8_t)buff[i + 2]);
				}
				SendInput(1, &kbInput, sizeof(INPUT));
				i += 3;
			}
			else if (buff[i] == MOUSE_MOVE && (i + 2) < size) {
				mInput.mi.dx = (((uint8_t)buff[i + 1]) / 256) * 65535;
				mInput.mi.dy = (((uint8_t)buff[i + 2]) / 256) * 65535;
				SendInput(1, &mInput, sizeof(INPUT));
				i += 3;
			}
			else if(buff[i] >= 0 && buff[i] < 10) {
				SendInput(sizes[buff[i]], inputArr[buff[i]], sizeof(INPUT));
				i++;
			}
			else {
				break;
			}
		}
	}

	//for (int i = 0; i < 14; i++) {
	//	free(inputArr[i]);
	//}
	//free(inputArr);

	closesocket(kb_sock);

	setStatus(kb_status, STATUS_ERROR_OTHER);
}

void MouseInputThread() {
	/*if(fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Starting mouse thread\n";
		fs.flush();
	}*/

	mouse_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mouse_socket == INVALID_SOCKET) {
		setStatus(mouse_status, STATUS_ERROR_OTHER);
		return;
	}
	sockaddr_in bindaddr;
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(PORT);
	bindaddr.sin_addr.s_addr = inet_addr("0.0.0.0");
	/*if (fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Binding mouse socket\n";
		fs.flush();
	}*/
	if (bind(mouse_socket, (SOCKADDR*)&bindaddr, sizeof(bindaddr))) {
		closesocket(mouse_socket);
		setStatus(mouse_status, STATUS_ERROR_BIND);
		return;
	}

	// Touch injection setup
	/*if(InitializeTouchInjection(2, TOUCH_FEEDBACK_NONE)) {
		POINTER_TOUCH_INFO contact;
		memset(&contact, 0, sizeof(POINTER_TOUCH_INFO));
		fewfdwefeaww // Leaving this here on purpose
		contact.pointerInfo.pointerType = PT_TOUCH;
		contact.pointerInfo.pointerId = 0;          //contact 0
		contact.pointerInfo.ptPixelLocation.y = SCREEN_HEIGHT - 2; // Y co-ordinate of touch on screen
		contact.pointerInfo.ptPixelLocation.x = 10; // X co-ordinate of touch on screen

		contact.touchFlags = TOUCH_FLAG_NONE;
		contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
		contact.orientation = 90; // Orientation of 90 means touching perpendicular to screen.
		contact.pressure = 32000;

		contact.rcContact.top = contact.pointerInfo.ptPixelLocation.y - 2;
		contact.rcContact.bottom = contact.pointerInfo.ptPixelLocation.y + 2;
		contact.rcContact.left = contact.pointerInfo.ptPixelLocation.x - 2;
		contact.rcContact.right = contact.pointerInfo.ptPixelLocation.x + 2;

		contact.pointerInfo.pointerFlags = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
		InjectTouchInput(1, &contact); // Injecting the touch down on screen
		Sleep(100);
		contact.pointerInfo.pointerFlags = POINTER_FLAG_UP;
		InjectTouchInput(1, &contact); // Injecting the touch Up from screen
	}*/

	INPUT mouse;
	MouseSetup(&mouse);
	mouse.mi.dwFlags = MOUSEEVENTF_MOVE;

	//atexit();

	size_t size;
	char buff[13];
	float x = 0, y = 0;

	setStatus(mouse_status, STATUS_OK);

	/*if(fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Starting mouse recv loop\n";
		fs.flush();
	}*/

	while (true) {
		size = recv(mouse_socket, buff, 13, 0);
		if(size == SOCKET_ERROR && WSAGetLastError() != WSAEMSGSIZE) {
			break;
		}
		if(invalidPrefix(buff, size)) {
			continue;
		}
		memcpy(&x, buff + 5, 4);
		memcpy(&y, buff + 9, 4);
		mouse.mi.dx = lroundf(x * SCREEN_WIDTH);
		mouse.mi.dy = lroundf(y * SCREEN_HEIGHT);
		SendInput(1, &mouse, sizeof(INPUT));
	}

	/*if(fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Got SOCKET_ERROR with error code " << WSAGetLastError() << ", exited mouse recv loop\n";
		fs.flush();
	}*/

	setStatus(mouse_status, STATUS_ERROR_OTHER);

	closesocket(mouse_socket);
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_CREATE: {
		nid.cbSize = NOTIFYICONDATAW_V3_SIZE;
		nid.hWnd = hWnd;
		nid.uID = IDC_AIR_MOUSE_SERVER;
		nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
		nid.uCallbackMessage = WM_TRAY;
		nid.dwInfoFlags = NIIF_INFO;
		nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_AIR_MOUSE_SERVER));
		nid.hBalloonIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_AIR_MOUSE_SERVER));
		wsprintf(nid.szTip, L"Air Mouse");
		wsprintf(nid.szInfoTitle, L"Air Mouse");
		wsprintf(nid.szInfo, L"Minimized to tray");
		return 0;
	}
	case WM_SYSCOMMAND: {
		switch (wParam) {
		case SC_MINIMIZE: {
			Shell_NotifyIcon(NIM_ADD, &nid);
			ShowWindow(hWnd, SW_HIDE);
			break;
		}
		default:
			DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}
	case WM_TRAY: {
		switch (lParam) {
			case WM_LBUTTONUP: {
				//SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_APPWINDOW);
				ShowWindow(hWnd, SW_RESTORE);
				SetForegroundWindow(hWnd);
				Shell_NotifyIcon(NIM_DELETE, &nid);
				break;
			}
			case WM_RBUTTONUP: {
				HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_AIR_MOUSE_TRAY));
				if (hMenu) {
					HMENU hSubMenu = GetSubMenu(hMenu, 0);
					if (hSubMenu) {
						POINT stPoint;
						GetCursorPos(&stPoint);

						SetForegroundWindow(hWnd);
						TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, stPoint.x, stPoint.y, 0, hWnd, NULL);
					}

					DestroyMenu(hMenu);
				}
				break;
			}
		}
		return 0;
	}
	case WM_COMMAND: {
		// Parse the menu selections:
		switch (LOWORD(wParam)) {
			case ID_REPAINT: {
				InvalidateRect(hWnd, NULL, TRUE);
				break;
			}
			case IDM_TRAY_OPEN: {
				//SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_APPWINDOW);
				ShowWindow(hWnd, SW_RESTORE);
				SetForegroundWindow(hWnd);
				Shell_NotifyIcon(NIM_DELETE, &nid);
				break;
			}
			case IDM_TRAY_QUIT: {
				DestroyWindow(hWnd);
				break;
			}
			case IDM_FILE_QUIT: {
				DestroyWindow(hWnd);
				break;
			}
			case IDM_FILE_MINIMIZE_TO_TRAY: {
				Shell_NotifyIcon(NIM_ADD, &nid);
				ShowWindow(hWnd, SW_HIDE);
				break;
			}
			case ID_HELP_CHECKFORUPDATEDVERSION: {
				ShellExecute(0, 0, L"https://rebrand.ly/air-mouse", 0, 0, SW_SHOW);
				break;
			}
			case IDM_HELP_ANDROIDAPPLICATION: {
				ShellExecute(0, 0, L"https://play.google.com/store/apps/details?id=com.bytasaur.airmouse", 0, 0, SW_SHOW);
				break;
			}
			default:
				return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		SetBkColor(hdc, RGB(48, 48, 48));

		SetTextColor(hdc, RGB(255, 255, 255));
		TextOut(hdc, 10, 10, L"Admin", 5);
		TextOut(hdc, 135, 10, L":", 1);

		if (isRunAsAdministrator()) {
			SetTextColor(hdc, RGB(0, 150, 136));
			TextOut(hdc, 150, 10, L"YES", 3);
		}
		else {
			SetTextColor(hdc, RGB(255, 0, 0));
			TextOut(hdc, 150, 10, L"NO", 2);

			SetTextColor(hdc, RGB(255, 255, 255));
			TextOut(hdc, 180, 9, L"(application will lose control over elevated procesess)", 55);
		}

		SetTextColor(hdc, RGB(255, 255, 255));
		TextOut(hdc, 10, 35, L"Mouse Input", 11);
		printStatus(hdc, 135, 35, mouse_status);

		SetTextColor(hdc, RGB(255, 255, 255));
		TextOut(hdc, 10, 60, L"Keyboard Input", 14);
		printStatus(hdc, 135, 60, kb_status);

		SetTextColor(hdc, RGB(255, 255, 255));
		TextOut(hdc, WINDOW_WIDTH - 120, 10, EXE_VERSION, (int) wcslen(EXE_VERSION));

		/*TCHAR mv[12];
		wsprintf(mv, L"%d", li);
		TextOut(hdc, 10, 90, mv, wcslen(mv));*/

		EndPaint(hWnd, &ps);
		break;
	}
	case WM_DESTROY: {
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

ATOM mRegisterClass(HINSTANCE hInstance) {
	WNDCLASSEXW wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AIR_MOUSE_SERVER));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = CreateSolidBrush(RGB(48, 48, 48));
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_AIR_MOUSE_SERVER);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX), CW_USEDEFAULT, 0, WINDOW_WIDTH, WINDOW_HEIGHT, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd) {
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return TRUE;
}


int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR    lpCmdLine, _In_ int       nCmdShow) {
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_AIR_MOUSE_SERVER, szWindowClass, MAX_LOADSTRING);

	
	// Check if another instance already running
	if(FindWindow(szWindowClass, szTitle)) {
		displayMessage(L"Duplicate instance\n", L"Another instance of the application might be running.", MB_ICONWARNING);
		return FALSE;
	}

	mRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}

	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
		return FALSE;
	}

	/*if (fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Starting message loop\n";
		fs.flush();
	}*/

	std::thread st(ProbeResponseThread);

	std::thread kIt(KeyInputThread);

	std::thread mt(MouseInputThread);

	MSG msg;

	// Main message loop:
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	/*if(fs) {
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << ": Exited message loop\n";
		fs << duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() << "Closing sockets\n";
		fs.flush();
	}*/

	closesocket(mouse_socket);
	closesocket(kb_sock);
	closesocket(broadcastListenerSocket);

	st.join();
	kIt.join();
	mt.join();

	WSACleanup();

	return (int) msg.wParam;
}

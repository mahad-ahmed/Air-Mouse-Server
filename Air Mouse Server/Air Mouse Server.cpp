#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

#define PI 3.14159265358979323846
#define PORT 6666
#define PROBE_RESPONSE_PORT 7777
#define COMPATIBILITY_CODE "2"


//	TODO: Add an interactive GUI
//	TODO: Allow adding macros
//	TODO: Add an interrupt handler for exit
//	TODO: Minimize to system tray

const int SCREEN_WIDTH = GetSystemMetrics(SM_CXSCREEN);
const int SCREEN_HEIGHT = GetSystemMetrics(SM_CYSCREEN);
int mult = 1250;

float translateAngle(float angle) {
	if (angle == 0) {
		return 0;
	}
	if (((int)(angle / PI)) % 2 == 0) {
		return fmod(angle, PI);
	}
	return fmod(angle, PI) - ((int)(abs(angle) / angle)*PI);
}

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

void ServerThread() {
	SOCKET broadcastListenerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);		//IPPROTO_UDP  (17)  can use 0 to let driver(?) decide
	if (broadcastListenerSocket == INVALID_SOCKET) {
		return;
	}
	sockaddr_in listen_addr;
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons(PROBE_RESPONSE_PORT);
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);	// INADDR_ANY=0.0.0.0
	if (bind(broadcastListenerSocket, (SOCKADDR*)&listen_addr, sizeof(listen_addr))) {
		closesocket(broadcastListenerSocket);
		return;
	}
	sockaddr_in client_addr;
	int caddr_size = sizeof(client_addr);
	int size = 0;
	char recvBuf[4];
	memset(recvBuf, 0, 4);
	char ver[5] = COMPATIBILITY_CODE;

	struct sockaddr_in broadcastAddr;	//	Can reuse listen_addr??
	memset(&broadcastAddr, 0, sizeof(broadcastAddr));
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_addr.s_addr = inet_addr("255.255.255.255");
	broadcastAddr.sin_port = htons(PROBE_RESPONSE_PORT);
	SOCKET announceSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	setsockopt(announceSocket, SOL_SOCKET, SO_BROADCAST, "1", 1);
	//printf("string length: %d\n", strlen(ver));
	sendto(announceSocket, ver, strlen(ver), 0, (SOCKADDR*)&broadcastAddr, sizeof(broadcastAddr));
	//closesocket(announceSocket);	// Cannot close!
	while ((size = recvfrom(broadcastListenerSocket, recvBuf, 4, 0, (SOCKADDR*)&client_addr, &caddr_size)) != SOCKET_ERROR) {
		if (size > 1) {
			//printf("\nIF: %s  %d\n", recvBuf, size);
			if (recvBuf[0] == 'v') {
				int clientCode = atoi(&recvBuf[1]);
				int localCode = atoi(ver);
				if (clientCode > localCode) {
					std::thread msg_thread(displayMessage, L"Desktop application outdated",
						L"To get latest features and avoid compatibility issues, download the latest version of this application from:\nhttps://goo.gl/m233tM", MB_ICONWARNING);
					msg_thread.detach();
				}
				else if (clientCode < localCode) {
					std::thread msg_thread(displayMessage, L"Android application outdated",
						L"Please update the app to the latest version from the PlayStore to avoid compatibility issues.", MB_ICONWARNING);
					msg_thread.detach();
				}
			}
			else {
				mult = atoi(recvBuf);
				printf("\nSensitivity changed to %d\n", mult);
			}
		}
		else {
			//printf("\nELSE: %d  %d\n", recvBuf[0], size);
			//printf(inet_ntoa(client_addr.sin_addr));
			//printf("string length: %d\n", strlen(ver));
			sendto(broadcastListenerSocket, ver, strlen(ver), 0, (SOCKADDR*)&client_addr, sizeof(client_addr));
		}
		memset(&client_addr, 0, caddr_size);
	}
	closesocket(broadcastListenerSocket);
}

//void printStatus() {
//	//
//}

void main() {
	//ShowWindow(FindWindowA("ConsoleWindowClass", NULL), false);
	WSADATA wsadata;
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
		exit;
	}

	std::thread st(ServerThread);
	st.detach();

	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		exit;
	}
	sockaddr_in bindaddr;
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_port = htons(PORT);
	bindaddr.sin_addr.s_addr = inet_addr("0.0.0.0");
	if (bind(sock, (SOCKADDR*)&bindaddr, sizeof(bindaddr))) {
		displayMessage(L"Bind error\n", L"Another instance of the application might be running.", MB_ICONWARNING);
		closesocket(sock);
		exit;
	}

	if (!isRunAsAdministrator()) {
		//std::thread msg_thread(displayMessage, L"Running as non-elevated user\n",
		//	L"Application will lose control over elevated processes such as Task Manager.\nRun the application as Administrator to fix this.", MB_ICONINFORMATION);
		////if(msg_thread.joinable()) {
		//msg_thread.detach();
		printf("Running as non-elevated user, the application won't control mouse over some processes like Task Manager.\n\nRun as Administrator to fix this.\n");
	}

	const int HALF_WIDTH = SCREEN_WIDTH / 2;
	const int HALF_HEIGHT = SCREEN_HEIGHT / 2;

	const size_t inputSize = sizeof(INPUT);
	INPUT mouse[1];
	INPUT *inputArr[17];
	int sizes[sizeof(inputArr) / sizeof(inputArr[0])];
	//INPUT **inputArr = (INPUT **)malloc(sizeof(INPUT*) * INPUT_COUNT);
	//printf("Sizes: %d %d\n", sizeof(inputArr), sizeof(inputArr)/ sizeof(inputArr[0]));
	for (int i = 0; i<sizeof(inputArr) / sizeof(inputArr[0]); i++) {
		inputArr[i] = (INPUT *)malloc(inputSize);
		sizes[i] = 1;
		if (i<8) {
			MouseSetup(inputArr[i]);
		}
		else {
			KeyboardSetup(inputArr[i]);
		}
	}
	MouseSetup(mouse);

	mouse->mi.dwFlags = MOUSEEVENTF_MOVE;

	inputArr[0]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTDOWN);

	inputArr[1]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP);

	inputArr[2]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTDOWN);

	inputArr[3]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_RIGHTUP);

	inputArr[4]->mi.dwFlags = MOUSEEVENTF_WHEEL;
	inputArr[4]->mi.mouseData = -WHEEL_DELTA;	 // WHEEL_DELTA=120 (One wheel click)

	inputArr[5]->mi.dwFlags = MOUSEEVENTF_WHEEL;
	inputArr[5]->mi.mouseData = WHEEL_DELTA;

	inputArr[6]->mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE);
	inputArr[6]->mi.dx = (HALF_WIDTH * (0xFFFF / SCREEN_WIDTH));
	inputArr[6]->mi.dy = (HALF_HEIGHT * (0xFFFF / SCREEN_HEIGHT));

	//	Make a struct to hold the INPUT structure array and its size(did but went with this implementation instead for better runtime performance)
	sizes[7] = 4;
	inputArr[7] = (INPUT *)realloc(inputArr[7], inputSize * 4);
	inputArr[7][0].mi.dwFlags = (MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE);
	inputArr[7][0].mi.dx = (100 * (0xFFFF / SCREEN_WIDTH));
	inputArr[7][0].mi.dy = ((SCREEN_HEIGHT - 35) * (0xFFFF / SCREEN_HEIGHT));
	KeyboardSetup(&inputArr[7][1]);
	KeyboardSetup(&inputArr[7][1]);
	inputArr[7][1] = *inputArr[0];
	inputArr[7][2] = *inputArr[1];
	inputArr[7][3] = *inputArr[6];


	inputArr[8]->ki.wVk = VK_MEDIA_PLAY_PAUSE;

	inputArr[9]->ki.wVk = VK_MEDIA_PREV_TRACK;

	inputArr[10]->ki.wVk = VK_MEDIA_NEXT_TRACK;

	inputArr[11]->ki.wVk = VK_SPACE;

	inputArr[12]->ki.wVk = VK_LEFT;

	inputArr[13]->ki.wVk = VK_RIGHT;

	sizes[14] = 2;
	inputArr[14] = (INPUT *)realloc(inputArr[14], inputSize * 2);
	MouseSetup(&inputArr[14][0]);
	MouseSetup(&inputArr[14][1]);
	inputArr[14][0] = *inputArr[0];
	inputArr[14][1] = *inputArr[1];

	sizes[15] = 2;
	inputArr[15] = (INPUT *)realloc(inputArr[15], inputSize * 2);
	MouseSetup(&inputArr[15][0]);
	MouseSetup(&inputArr[15][1]);
	inputArr[15][0] = *inputArr[2];
	inputArr[15][1] = *inputArr[3];

	sizes[16] = 4;
	inputArr[16] = (INPUT *)realloc(inputArr[16], inputSize * 4);
	MouseSetup(&inputArr[16][0]);
	MouseSetup(&inputArr[16][1]);
	MouseSetup(&inputArr[16][2]);
	MouseSetup(&inputArr[16][3]);
	inputArr[16][0] = *inputArr[0];
	inputArr[16][1] = *inputArr[1];
	inputArr[16][2] = *inputArr[0];
	inputArr[16][3] = *inputArr[1];

	/*sizes[14] = 4;
	MouseSetup(&inputArr[14][0]);
	MouseSetup(&inputArr[14][1]);
	inputArr[14] = (INPUT *)realloc(inputArr[14], inputSize * 4);
	inputArr[14][0] = *inputArr[0];
	inputArr[14][1] = *inputArr[1];
	inputArr[14][2] = *inputArr[0];
	inputArr[14][3] = *inputArr[1];*/

	INPUT testInput;
	testInput.type = INPUT_KEYBOARD;
	testInput.ki.dwFlags = KEYEVENTF_UNICODE;
	testInput.ki.time = 0;
	testInput.ki.dwExtraInfo = 0;
	testInput.ki.wVk = 0;

	//atexit();

	size_t size;
	char buff[32];
	char* endPtr;
	float x = 0, y = 0, oldX = 0, oldY = 0;


	//printf("Less whoops!!");
	while ((size = recv(sock, buff, 32, 0)) != SOCKET_ERROR) {
		//printf("%d\n", size);
		//	Orientation values will range from -3.14.. to 3.14
		if (size > 2) {
			oldX = x;
			x = strtod(buff, &endPtr);

			oldY = y;
			y = strtod(endPtr + 1, NULL);

			mouse->mi.dx = translateAngle(x - oldX) * mult;
			mouse->mi.dy = translateAngle(y - oldY) * mult;
			SendInput(1, mouse, inputSize);
		}
		else {
			//SendInput(sizes[(int)buff[0]], inputArr[(int)buff[0]], inputSize);
			if (size == 2) {
				testInput.ki.wScan = buff[0];
				SendInput(1, &testInput, inputSize);
			}
			else if (size == 1) {
				SendInput(sizes[(int)buff[0]], inputArr[(int)buff[0]], inputSize);
			}
		}
		//memset(buff, 0, 32);
	}
	/*for (int i = 0; i < 14; i++) {
	free(inputArr[i]);
	}
	free(inputArr);*/
	WSACleanup();
}

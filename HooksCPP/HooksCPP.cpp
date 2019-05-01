#include "stdafx.h"
#include <easyhook.h>
#include <string>
#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <fstream>


HWND hWnd = 0;
std::string _ipcChannelName;
static int x;
static int y;
UINT16 vkey_state;




BOOL WINAPI GetCursorPos_Hook(LPPOINT lpPoint)
{
	POINT p = POINT();
	p.x = x;
	p.y = y;
	ClientToScreen(hWnd, &p);
	*lpPoint = p;
	return true;
}

HWND WINAPI GetForegroundWindow_Hook()
{
	return hWnd;
}

inline int getBitShiftForVKey(int VKey)
{
	int shift = 0;
	if (VKey <= 6)
	{
		return VKey - 1;
	}
	else
	{
		switch (VKey)
		{
			case 0x41: return 5;
			case 0x44: return 6;
			case 0x53: return 7;
			case 0x47: return 8;
			default: return 9;
		}
	}
}

SHORT WINAPI GetAsyncKeyState_Hook(int vKey)
{
	return (vkey_state & (1 << getBitShiftForVKey(vKey))) == 0 ? 0 : 0b1000000000000000;
}


LRESULT CallWindowProc_Hook(WNDPROC lpPrevWndFunc, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	/*std::ofstream myfile;
	myfile.open("C:\\Projects\\UniversalSplitScreen\\UniversalSplitScreen\\bin\\x86\\Debug\\HooksCPP_Output.txt");
	myfile << Msg << "\n";
	myfile.close();*/

	//USS signature is 1 << 7 or 0b10000000 for WM_MOUSEMOVE(0x0200). If this is detected, allow event to pass
	if (Msg == 0x0200 && ((int)wParam & 0b10000000) > 0)
		return CallWindowProc(lpPrevWndFunc, hWnd, Msg, wParam, lParam);

	// || Msg == 0x00FF
	else if ((Msg >= 0x020B && Msg <= 0x020D) || Msg == 0x0200 || Msg == 0x0021 || Msg == 0x02A1 || Msg == 0x02A3)//Other mouse events. 
		return 0;
	else
	{
		if (false && Msg == 0x0006) //0x0006 is WM_ACTIVATE, which resets the mouse position for starbound [citation needed]
			return CallWindowProc(lpPrevWndFunc, hWnd, Msg, 1, 0);
		else
			return CallWindowProc(lpPrevWndFunc, hWnd, Msg, wParam, lParam);
	}
}



inline int bytesToInt(BYTE* bytes)
{
	return (int)(bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3]);

	/*BYTE *p = bytes;
	int t = 0;
	BYTE* ptr1 = (BYTE*)&t;

	for (int i = 0; i < 4; i++)
	{
		*ptr1 = *p;
		ptr1++;
		p++;
	}

	return t;*/

	//return (int)(*p << 24 | *++p << 16 | *++p << 8 | *++p);

	//int val;
	//std::memcpy(&val, &bytes[offset], sizeof(int));
	//return val;

	//BYTE *p = &bytes[offset];
	//return (int)(*p);
}

void startPipe()
{
	char _pipeNameChars[256];
	sprintf_s(_pipeNameChars, "\\\\.\\pipe\\%s", _ipcChannelName.c_str());

	HANDLE pipe = CreateFile(
		_pipeNameChars,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (pipe == INVALID_HANDLE_VALUE)
	{
		std::cout << "Failed to connect to pipe\n";
		return;
	}

	std::cout << "Connected to pipe\n";

	for (;;)
	{
		BYTE buffer[9];
		DWORD bytesRead = 0;

		BOOL result = ReadFile(
			pipe,
			buffer,
			9 * sizeof(BYTE),
			&bytesRead,
			NULL
		);

		if (result && bytesRead == 9)
		{
			int param1 = bytesToInt(&buffer[1]);

			int param2 = bytesToInt(&buffer[5]);

			//std::cout << "Received message. Msg=" << (int)buffer[0] << ", param1=" << param1 << ", param2=" << param2 << "\n";

			switch (buffer[0])
			{
				case 0x01:
				{
					x = param1;
					y = param2;
					break;
				}
				case 0x02:
				{
					UINT16 shift = (1 << getBitShiftForVKey(param1));
					if (param2 == 0)//Button up
					{
						vkey_state &= (~shift);//Sets to 0
					}
					else//Button down
					{
						vkey_state |= shift;//Sets to 1
					}
					break;
				}
				default:
				{
					break;
				}
			}
		}
		else
		{
			std::cout << "Failed to read message\n";
		}
	}
}

void installHook(LPCSTR moduleHandle, LPCSTR lpProcName, void* InCallback)
{
	HOOK_TRACE_INFO hHook = { NULL };

	NTSTATUS hookResult = LhInstallHook(
		GetProcAddress(GetModuleHandle(moduleHandle), lpProcName),
		InCallback,
		NULL,
		&hHook);

	if (!FAILED(hookResult))
	{
		ULONG ACLEntries[1] = { 0 };
		LhSetExclusiveACL(ACLEntries, 1, &hHook);
		std::cout << "Successfully installed hook " << lpProcName << "\n";
	}
	else
	{
		std::cout << "Failed to install hook " << lpProcName << ". NSTATUS: " << hookResult << "\n";
	}
}

struct UserData
{
	HWND hWnd;
	char ipcChannelName[256];
};

LRESULT CALLBACK GetMsgProc(_In_ int code, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	MSG* lpMsg = (MSG*)lParam;

	UINT Msg = lpMsg->message;

	if ((Msg == 0x0200 && ((int)wParam & 0b10000000) > 0)
		|| !((Msg >= 0x020B && Msg <= 0x020D) || Msg == 0x0200 || Msg == 0x0021 || Msg == 0x02A1 || Msg == 0x02A3))
	{
		return CallNextHookEx(NULL, code, wParam, lParam);
	}
	else
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* inRemoteInfo)
{
	//Cout will go to the games console
	std::cout << "Injected CPP\n";
	std::cout << "Injected by host process ID: " << inRemoteInfo->HostPID << "\n";
	std::cout << "Passed in data size:" << inRemoteInfo->UserDataSize << "\n";

	if (inRemoteInfo->UserDataSize == sizeof(UserData))
	{
		//Get UserData
		UserData userData = *reinterpret_cast<UserData *>(inRemoteInfo->UserData);

		hWnd = userData.hWnd;
		std::cout << "Received hWnd: " << hWnd << "\n";

		std::string ipcChannelName(userData.ipcChannelName);
		_ipcChannelName = ipcChannelName;
		std::cout << "Received IPC channel: " << ipcChannelName << "\n";
		
		//Install hooks
		installHook(TEXT("user32"),	"GetCursorPos",			GetCursorPos_Hook);
		installHook(TEXT("user32"),	"GetForegroundWindow",	GetForegroundWindow_Hook);
		installHook(TEXT("user32"), "GetAsyncKeyState",		GetAsyncKeyState_Hook);
		//installHook(TEXT("user32"), "CallWindowProcW",		CallWindowProc_Hook);
		
		HANDLE hThreadSnap = INVALID_HANDLE_VALUE;
		THREADENTRY32 te32;
		hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		Thread32First(hThreadSnap, &te32);
		do
		{
			SetWindowsHookEx(3, GetMsgProc, NULL, te32.th32ThreadID);
		} while (Thread32Next(hThreadSnap, &te32));

		

		//Start named pipe client
		startPipe();
	}
	else
	{
		std::cout << "Failed getting user data\n";
	}

	return;
}
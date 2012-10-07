#include "burner.h"
#include <shlobj.h>

HWND hRomsDlg = NULL;

int* gameAv = NULL;
bool avOk = false;
int bAvbOnly = 1;

static DWORD dwScanThreadId = 0;
static HANDLE hScanThread = NULL;
static int nOldSelect = 0;

static HANDLE hEvent = NULL;

static void CreateRomDatName(TCHAR* szRomDat)
{
	_stprintf(szRomDat, _T("config\\%s.roms.dat"), szAppExeName);

	return;
}

//Select Directory Dialog//////////////////////////////////////////////////////////////////////////

static BOOL CALLBACK DefInpProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM)
{
	int var;
	static bool chOk;

	switch (Msg) {
		case WM_INITDIALOG: {
			chOk = false;

			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT1, szAppRomPaths[0]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT2, szAppRomPaths[1]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT3, szAppRomPaths[2]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT4, szAppRomPaths[3]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT5, szAppRomPaths[4]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT6, szAppRomPaths[5]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT7, szAppRomPaths[6]);
			SetDlgItemText(hDlg, IDC_ROMSDIR_EDIT8, szAppRomPaths[7]);

			WndInMid(hDlg, hScrnWnd);
			SetFocus(hDlg);											// Enable Esc=close
			break;
		}
		case WM_COMMAND: {
			LPMALLOC pMalloc = NULL;
			BROWSEINFO bInfo;
			ITEMIDLIST* pItemIDList = NULL;
			TCHAR buffer[MAX_PATH];

			if (LOWORD(wParam) == IDOK) {

				for (int i = 0; i < 4; i++) {
					if (GetDlgItemText(hDlg, IDC_ROMSDIR_EDIT1 + i, buffer, sizeof(buffer)) && lstrcmp(szAppRomPaths[i], buffer)) {
						chOk = true;
						lstrcpy(szAppRomPaths[i], buffer);
					}
				}

				SendMessage(hDlg, WM_CLOSE, 0, 0);
				break;
			} else {
				if (LOWORD(wParam) >= IDC_ROMSDIR_BR1 && LOWORD(wParam) <= IDC_ROMSDIR_BR8) {
					var = IDC_ROMSDIR_EDIT1 + LOWORD(wParam) - IDC_ROMSDIR_BR1;
				} else {
					if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDCANCEL) {
						SendMessage(hDlg, WM_CLOSE, 0, 0);
					}
					break;
				}
			}

		    SHGetMalloc(&pMalloc);

			memset(&bInfo, 0, sizeof(bInfo));
			bInfo.hwndOwner = hDlg;
			bInfo.pszDisplayName = buffer;
			bInfo.lpszTitle = _T("Select Directory:");
			bInfo.ulFlags = BIF_EDITBOX | BIF_RETURNONLYFSDIRS;

			pItemIDList = SHBrowseForFolder(&bInfo);

			if (pItemIDList) {
				if (SHGetPathFromIDList(pItemIDList, buffer)) {
					int strLen = _tcslen(buffer);
					if (strLen) {
						if (buffer[strLen - 1] != _T('\\')) {
							buffer[strLen] = _T('\\');
							buffer[strLen + 1] = _T('\0');
						}
						SetDlgItemText(hDlg, var, buffer);
					}
				}
				pMalloc->Free(pItemIDList);
			}
			pMalloc->Release();

			break;
		}
		case WM_CLOSE: {
			EndDialog(hDlg, 0);
			if (chOk) {
				bRescanRoms = true;
				CreateROMInfo();
			}
		}
	}

	return 0;
}


int RomsDirCreate()
{
	DialogBox(hAppInst, MAKEINTRESOURCE(IDD_ROMSDIR), hScrnWnd, DefInpProc);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//Check Romsets Dialog/////////////////////////////////////////////////////////////////////////////

static int WriteGameAvb()
{
	TCHAR szRomDat[MAX_PATH];
	FILE* h;

	CreateRomDatName(szRomDat);

	if ((h = _tfopen(szRomDat, _T("wt"))) == NULL) {
		return 1;
	}

	_ftprintf(h, _T(APP_TITLE) _T(" v%.20s ROMs"), szAppBurnVer);	// identifier
	_ftprintf(h, _T(" 0x%04X "), nBurnDrvCount);					// no of games

	for (unsigned int i = 0; i < nBurnDrvCount; i++) {
		if (gameAv[i] & 2) {
			_fputtc(_T('*'), h);
		} else {
			if (gameAv[i] & 1) {
				_fputtc(_T('+'), h);
			} else {
				_fputtc(_T('-'), h);
			}
		}
	}

	_ftprintf(h, _T(" END"));									// end marker

	fclose(h);

	return 0;
}

static int DoCheck(TCHAR* buffPos)
{
	TCHAR label[256];

	// Check identifier
	memset(label, 0, sizeof(label));
	_stprintf(label, _T(APP_TITLE) _T(" v%.20s ROMs"), szAppBurnVer);
	if ((buffPos = LabelCheck(buffPos, label)) == NULL) {
		return 1;
	}

	// Check no of supported games
	memset(label, 0, sizeof(label));
	memcpy(label, buffPos, 16);
	buffPos += 8;
	unsigned int n = _tcstol(label, NULL, 0);
	if (n != nBurnDrvCount) {
		return 1;
	}

	for (unsigned int i = 0; i < nBurnDrvCount; i++) {
		if (*buffPos == _T('*')) {
			gameAv[i] = 3;
		} else {
			if (*buffPos == _T('+')) {
				gameAv[i] = 1;
			} else {
				if (*buffPos == _T('-')) {
					gameAv[i] = 0;
				} else {
					return 1;
				}
			}
		}

		buffPos++;
	}

	memset(label, 0, sizeof(label));
	_stprintf(label, _T(" END"));
	if (LabelCheck(buffPos, label) == NULL) {
		avOk = true;
		return 0;
	} else {
		return 1;
	}
}

int CheckGameAvb()
{
	TCHAR szRomDat[MAX_PATH];
	FILE* h;
	int bOK;
	int nBufferSize = nBurnDrvCount + 256;
	TCHAR* buffer = (TCHAR*)malloc(nBufferSize * sizeof(TCHAR));
	if (buffer == NULL) {
		return 1;
	}

	memset(buffer, 0, nBufferSize * sizeof(TCHAR));
	CreateRomDatName(szRomDat);

	if ((h = _tfopen(szRomDat, _T("r"))) == NULL) {
		return 1;
	}

	_fgetts(buffer, nBufferSize, h);
	fclose(h);

	bOK = DoCheck(buffer);

	free(buffer);
	return bOK;
}

static int QuitRomsScan()
{
	DWORD dwExitCode;

	GetExitCodeThread(hScanThread, &dwExitCode);

	if (dwExitCode == STILL_ACTIVE) {

		// Signal the scan thread to abort
		SetEvent(hEvent);

		// Wait for the thread to finish
		if (WaitForSingleObject(hScanThread, 10000) != WAIT_OBJECT_0) {
			// If the thread doesn't finish within 10 seconds, forcibly kill it
			TerminateThread(hScanThread, 1);
		}

		CloseHandle(hScanThread);
	}

	CloseHandle(hEvent);

	hEvent = NULL;

	hScanThread = NULL;
	dwScanThreadId = 0;

	BzipClose();

	nBurnDrvSelect = nOldSelect;
	nOldSelect = 0;
	bRescanRoms = false;

	if (avOk) {
		WriteGameAvb();
	}

	return 1;
}

static DWORD WINAPI AnalyzingRoms(LPVOID)					// LPVOID lParam
{
	for (unsigned int z = 0; z < nBurnDrvCount; z++) {
		nBurnDrvSelect = z;

		// See if we need to abort
		if (WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0) {
			ExitThread(0);
		}

		SendDlgItemMessage(hRomsDlg, IDC_WAIT_PROG, PBM_STEPIT, 0, 0);

		switch (BzipOpen(TRUE))	{
			case 0:
				gameAv[z] = 3;
				break;
			case 2:
				gameAv[z] = 1;
				break;
			case 1:
				gameAv[z] = 0;
		}

		BzipClose();
   }

	avOk = true;

	PostMessage(hRomsDlg, WM_CLOSE, 0, 0);

	return 0;
}

static BOOL CALLBACK WaitProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM)		// LPARAM lParam
{
	switch (Msg) {
		case WM_INITDIALOG:
			hRomsDlg = hDlg;
			nOldSelect = nBurnDrvSelect;
			memset(gameAv, 0, sizeof(gameAv));
			SendDlgItemMessage(hDlg, IDC_WAIT_PROG, PBM_SETRANGE, 0, MAKELPARAM(0, nBurnDrvCount));
			SendDlgItemMessage(hDlg, IDC_WAIT_PROG, PBM_SETSTEP, (WPARAM)1, 0);

			ShowWindow(GetDlgItem(hDlg, IDC_WAIT_LABEL_A), TRUE);
			SendMessage(GetDlgItem(hDlg, IDC_WAIT_LABEL_A), WM_SETTEXT, (WPARAM)0, (LPARAM)_T("Scanning ROMs..."));
			ShowWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);

			avOk = false;
			hScanThread = CreateThread(NULL, 0, AnalyzingRoms, NULL, THREAD_TERMINATE, &dwScanThreadId);

			hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			break;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL) {
				PostMessage(hDlg, WM_CLOSE, 0, 0);
			}
			break;

		case WM_CLOSE:
			QuitRomsScan();
			EndDialog(hDlg, 0);
			hRomsDlg = NULL;

	}

	return 0;
}

int CreateROMInfo()
{
	if (gameAv == NULL) {
		gameAv = (int*)malloc(nBurnDrvCount * sizeof(int));
		memset(gameAv, 0, nBurnDrvCount * sizeof(int));
	}

	if (gameAv) {
		if (CheckGameAvb() || bRescanRoms) {
			DialogBox(hAppInst, MAKEINTRESOURCE(IDD_WAIT), hScrnWnd, WaitProc);
		}
	}

	return 1;
}

void FreeROMInfo()
{
	free(gameAv);
	gameAv = NULL;
}

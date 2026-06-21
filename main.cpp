#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include "roman_dict.h" 

#define TIMER_ID 1
#define MAX_WORDS 256

enum GameState {
    STATE_TITLE,
    STATE_PLAYING,
    STATE_RESULT,
    STATE_CREDITS,
    STATE_HOWTO,
    STATE_GITHUB
};

struct WordData {
    TCHAR display[64];
    TCHAR kana[64];
};

WordData g_Words[MAX_WORDS];
int g_WordCount = 0;

struct DropChar {
    TCHAR ch;
    int x, y;
    bool active;
};

GameState g_State = STATE_TITLE;
int g_ScreenWidth = 0;
int g_ScreenHeight = 0;
int g_HighScore = 0, g_LastScore = 0;
DWORD g_StartTime = 0;
int g_TimeLimitMs = 60000;
int g_Combo = 0, g_CorrectCount = 0, g_MissCount = 0;
DropChar g_Drops[64] = {0};

int g_CurrentWordIndex = 0;
const TCHAR* g_CurrentKanaPtr = NULL;
const RomanPattern* g_CurrentTarget = NULL;
bool g_AlivePatterns[30] = {false};
int g_TypedLen = 0;
TCHAR g_TypedDisplay[256] = {0};

HFONT g_hFontLarge = NULL;
HFONT g_hFontMedium = NULL;
HFONT g_hFontSmall = NULL;

TCHAR g_AppDir[MAX_PATH] = {0};

TCHAR g_BgmPath[MAX_PATH] = {0};
TCHAR g_ResultPath[MAX_PATH] = {0};

char* g_CorrectSound = NULL;
char* g_MissSound = NULL;

void LoadSoundFile(const TCHAR* path, char** buffer) {
    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0 && size < 1024 * 1024) { 
            *buffer = (char*)malloc(size);
            DWORD read;
            ReadFile(hFile, *buffer, size, &read, NULL);
        }
        CloseHandle(hFile);
    }
}

void InitAppDir() {
    GetModuleFileName(NULL, g_AppDir, MAX_PATH);
    TCHAR* p = _tcsrchr(g_AppDir, _T('\\'));
    if (p) *p = _T('\0');

    wsprintf(g_ResultPath, _T("%s\\se\\result.wav"), g_AppDir);

    TCHAR path[MAX_PATH];
    wsprintf(path, _T("%s\\se\\correct.wav"), g_AppDir);
    LoadSoundFile(path, &g_CorrectSound);
    wsprintf(path, _T("%s\\se\\miss.wav"), g_AppDir);
    LoadSoundFile(path, &g_MissSound);
}

void LoadWords() {
    TCHAR path[MAX_PATH];
    wsprintf(path, _T("%s\\words.txt"), g_AppDir);
    
    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, NULL);
        if (size > 0 && size < 1024 * 1024) {
            char* buffer = (char*)malloc(size + 2);
            DWORD read;
            ReadFile(hFile, buffer, size, &read, NULL);
            buffer[read] = 0;
            buffer[read+1] = 0;
            
            wchar_t* text = (wchar_t*)buffer;
            if (read >= 2 && text[0] == 0xFEFF) {
                text++; 
            }
            
            g_WordCount = 0;
            TCHAR* p = text;
            while (*p && g_WordCount < MAX_WORDS) {
                TCHAR* line = p;
                while (*p && *p != _T('\r') && *p != _T('\n')) p++;
                if (*p == _T('\r')) { *p = 0; p++; }
                if (*p == _T('\n')) { *p = 0; p++; }
                
                TCHAR* comma = _tcschr(line, _T(','));
                if (comma) {
                    *comma = 0;
                    lstrcpyn(g_Words[g_WordCount].display, line, 64);
                    lstrcpyn(g_Words[g_WordCount].kana, comma + 1, 64);
                    g_WordCount++;
                }
            }
            free(buffer);
        }
        CloseHandle(hFile);
    }
    
    if (g_WordCount == 0) {
        lstrcpy(g_Words[0].display, _T("単語リストなし"));
        lstrcpy(g_Words[0].kana, _T("たんごりすとなし"));
        lstrcpy(g_Words[1].display, _T("テキストファイルを作成"));
        lstrcpy(g_Words[1].kana, _T("てきすとふぁいるをさくせい"));
        g_WordCount = 2;
    }
}

HFONT CreateCEFont(int height, int weight, const TCHAR* faceName) {
    LOGFONT lf = {0};
    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfCharSet = SHIFTJIS_CHARSET;
    lstrcpy(lf.lfFaceName, faceName);
    return CreateFontIndirect(&lf);
}

void DrawCenteredText(HDC hdc, HFONT font, const TCHAR* text, int y, COLORREF color) {
    SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SIZE size;
    GetTextExtentPoint32(hdc, text, lstrlen(text), &size);
    int x = (g_ScreenWidth - size.cx) / 2;
    ExtTextOut(hdc, x, y, 0, NULL, text, lstrlen(text), NULL);
}

void GetRemainingRomaji(TCHAR* remainingRomaji) {
    remainingRomaji[0] = _T('\0');
    if (g_CurrentTarget) {
        for (int i = 0; g_CurrentTarget->typePatterns[i] != NULL; i++) {
            if (g_AlivePatterns[i]) {
                lstrcat(remainingRomaji, g_CurrentTarget->typePatterns[i] + g_TypedLen);
                break;
            }
        }
    }
    const TCHAR* tempPtr = g_CurrentKanaPtr;
    while (*tempPtr != _T('\0')) {
        int matchLen = 0;
        int bestMatchLen = 0;
        int bestMatchIdx = -1;
        for (int i = 0; i < g_RomanDictCount; i++) {
            int patLen = lstrlen(g_RomanDict[i].pattern);
            if (patLen > bestMatchLen && _tcsncmp(tempPtr, g_RomanDict[i].pattern, patLen) == 0) {
                bestMatchLen = patLen;
                bestMatchIdx = i;
            }
        }
        if (bestMatchIdx >= 0) {
            lstrcat(remainingRomaji, g_RomanDict[bestMatchIdx].typePatterns[0]);
            tempPtr += bestMatchLen;
        } else {
            tempPtr++;
        }
    }
}

const RomanPattern* FindPattern(const TCHAR* str, int& matchLen) {
    int bestMatchLen = 0;
    int bestMatchIdx = -1;
    for (int i = 0; i < g_RomanDictCount; i++) {
        int patLen = lstrlen(g_RomanDict[i].pattern);
        if (patLen > bestMatchLen && _tcsncmp(str, g_RomanDict[i].pattern, patLen) == 0) {
            bestMatchLen = patLen;
            bestMatchIdx = i;
        }
    }
    if (bestMatchIdx >= 0) {
        matchLen = bestMatchLen;
        return &g_RomanDict[bestMatchIdx];
    }
    return NULL;
}

void LoadNextKana() {
    if (*g_CurrentKanaPtr == _T('\0')) {
        g_CurrentWordIndex = rand() % g_WordCount;
        g_CurrentKanaPtr = g_Words[g_CurrentWordIndex].kana;
        g_TypedDisplay[0] = _T('\0');
    }
    
    while (*g_CurrentKanaPtr != _T('\0')) {
        int matchLen = 0;
        g_CurrentTarget = FindPattern(g_CurrentKanaPtr, matchLen);
        if (g_CurrentTarget) {
            g_CurrentKanaPtr += matchLen;
            g_TypedLen = 0;
            for (int i = 0; i < 30; i++) g_AlivePatterns[i] = false;
            for (int i = 0; g_CurrentTarget->typePatterns[i] != NULL; i++) {
                g_AlivePatterns[i] = true;
            }
            break;
        } else {
            g_CurrentKanaPtr++;
        }
    }
}

void LoadScore() {
    HANDLE hFile = CreateFile(_T("score.txt"), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[64] = {0};
        DWORD read;
        ReadFile(hFile, buf, sizeof(buf) - 1, &read, NULL);
        sscanf(buf, "%d %d", &g_HighScore, &g_LastScore);
        CloseHandle(hFile);
    }
}

void SaveScore(int current_score) {
    g_LastScore = current_score;
    if (current_score > g_HighScore) g_HighScore = current_score;
    HANDLE hFile = CreateFile(_T("score.txt"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buf[64];
        sprintf(buf, "%d %d", g_HighScore, g_LastScore);
        DWORD written;
        WriteFile(hFile, buf, strlen(buf), &written, NULL);
        CloseHandle(hFile);
    }
}

void StartGame() {
    PlaySound(NULL, NULL, 0);

    g_StartTime = GetTickCount();
    g_TimeLimitMs = 60000;
    g_Combo = 0; g_CorrectCount = 0; g_MissCount = 0;
    srand(GetTickCount());
    
    for (int i = 0; i < 64; i++) g_Drops[i].active = false;
    
    g_CurrentWordIndex = rand() % g_WordCount;
    g_CurrentKanaPtr = g_Words[g_CurrentWordIndex].kana;
    g_TypedDisplay[0] = _T('\0');
    LoadNextKana();
    g_State = STATE_PLAYING;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;
    RECT rect;
    TCHAR buf[128];

    switch (message) {
        case WM_CREATE:
            InitAppDir();
            LoadWords();
            
            g_hFontLarge = CreateCEFont(48, FW_BOLD, _T("MS Gothic"));
            g_hFontMedium = CreateCEFont(32, FW_NORMAL, _T("MS Gothic"));
            g_hFontSmall = CreateCEFont(20, FW_NORMAL, _T("MS Gothic"));
            
            SetTimer(hWnd, TIMER_ID, 30, NULL);
            LoadScore();
            break;

        case WM_TIMER:
        {
            if (g_State == STATE_PLAYING) {
                DWORD elapsed = GetTickCount() - g_StartTime;
                if (elapsed >= g_TimeLimitMs) {
                    int finalScore = g_CorrectCount - g_MissCount;
                    SaveScore(finalScore);
                    
                    g_State = STATE_RESULT;
                    PlaySound(g_ResultPath, NULL, SND_FILENAME | SND_ASYNC);
                }
                for (int i = 0; i < 64; i++) {
                    if (g_Drops[i].active) {
                        g_Drops[i].y += 8;
                        if (g_Drops[i].y > g_ScreenHeight) g_Drops[i].active = false;
                    }
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN:
            if (g_State == STATE_TITLE) {
                int touchX = LOWORD(lParam);
                int touchY = HIWORD(lParam);
                
                if (touchY > g_ScreenHeight - 100) {
                    if (touchX < g_ScreenWidth / 3) {
                        g_State = STATE_CREDITS;
                    } else if (touchX < (g_ScreenWidth * 2) / 3) {
                        g_State = STATE_HOWTO;
                    } else {
                        g_State = STATE_GITHUB;
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } 
            else if (g_State == STATE_CREDITS || g_State == STATE_HOWTO || g_State == STATE_GITHUB) {
                g_State = STATE_TITLE;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;

        case WM_KEYDOWN:
            if (g_State == STATE_TITLE) {
                if (wParam == 'C' || wParam == 'c') {
                    g_State = STATE_CREDITS;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (wParam == 'H' || wParam == 'h') {
                    g_State = STATE_HOWTO;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (wParam == 'G' || wParam == 'g') {
                    g_State = STATE_GITHUB;
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (wParam == VK_RETURN) {
                    StartGame();
                    InvalidateRect(hWnd, NULL, FALSE);
                } else if (wParam == VK_ESCAPE) {
                    PostQuitMessage(0);
                }
            }
            else if (g_State == STATE_CREDITS || g_State == STATE_HOWTO || g_State == STATE_GITHUB) {
                g_State = STATE_TITLE;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            else if (wParam == VK_ESCAPE) {
                if (g_State == STATE_PLAYING || g_State == STATE_RESULT) {
                    g_State = STATE_TITLE;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            } else if (wParam == VK_RETURN) {
                if (g_State == STATE_RESULT) {
                    g_State = STATE_TITLE;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;

        case WM_CHAR:
            if (g_State == STATE_PLAYING && g_CurrentTarget != NULL) {
                TCHAR typedChar = (TCHAR)wParam;

                if (typedChar < 0x20) {
                    break;
                }

                if (typedChar >= 'A' && typedChar <= 'Z') typedChar += 32;

                bool hitAny = false;
                bool isCompleted = false;
                bool nextAlive[30] = {false};
                TCHAR hitChar = 0;

                for (int i = 0; g_CurrentTarget->typePatterns[i] != NULL; i++) {
                    if (g_AlivePatterns[i]) {
                        const TCHAR* currentStr = g_CurrentTarget->typePatterns[i];
                        if (currentStr[g_TypedLen] == typedChar) {
                            hitAny = true;
                            nextAlive[i] = true;
                            hitChar = typedChar;
                            if (currentStr[g_TypedLen + 1] == _T('\0')) isCompleted = true;
                        }
                    }
                }

                if (hitAny) {
                    if (g_CorrectSound) PlaySound((LPCWSTR)g_CorrectSound, NULL, SND_MEMORY | SND_ASYNC);

                    for (int i = 0; i < 30; i++) g_AlivePatterns[i] = nextAlive[i];
                    g_TypedLen++;

                    hdc = GetDC(hWnd);
                    SelectObject(hdc, g_hFontMedium);
                    TCHAR remainingBuf[256];
                    GetRemainingRomaji(remainingBuf);
                    SIZE sTyped, sRem;
                    GetTextExtentPoint32(hdc, g_TypedDisplay, lstrlen(g_TypedDisplay), &sTyped);
                    GetTextExtentPoint32(hdc, remainingBuf, lstrlen(remainingBuf), &sRem);
                    ReleaseDC(hWnd, hdc);
                    
                    int startX = (g_ScreenWidth - (sTyped.cx + sRem.cx)) / 2;

                    for (int i = 0; i < 64; i++) {
                        if (!g_Drops[i].active) {
                            g_Drops[i].active = true;
                            g_Drops[i].ch = hitChar;
                            g_Drops[i].x = startX + sTyped.cx;
                            g_Drops[i].y = (g_ScreenHeight / 2) + 40; 
                            break;
                        }
                    }

                    int len = lstrlen(g_TypedDisplay);
                    g_TypedDisplay[len] = hitChar;
                    g_TypedDisplay[len + 1] = _T('\0');

                    g_CorrectCount++;
                    g_Combo++;

                    if (g_Combo % 25 == 0) {
                        g_TimeLimitMs += (g_Combo / 25) * 1000;
                    }

                    if (isCompleted) LoadNextKana();
                } else {
                    if (g_MissSound) PlaySound((LPCWSTR)g_MissSound, NULL, SND_MEMORY | SND_ASYNC);
                    g_Combo = 0;
                    g_MissCount++;
                }
                
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;

        case WM_PAINT:
        {
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rect);

            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

            FillRect(hMemDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(hMemDC, TRANSPARENT);

            if (g_State == STATE_TITLE) {
                DrawCenteredText(hMemDC, g_hFontLarge, _T("tyceing"), g_ScreenHeight / 2 - 110, RGB(0, 0, 0));
                
                DrawCenteredText(hMemDC, g_hFontMedium, _T("Press Enter to Start"), g_ScreenHeight / 2 + 10, RGB(100, 100, 100));
                wsprintf(buf, _T("High Score: %d / Last: %d"), g_HighScore, g_LastScore);
                DrawCenteredText(hMemDC, g_hFontSmall, buf, g_ScreenHeight / 2 + 60, RGB(0, 0, 0));
                
                SelectObject(hMemDC, g_hFontSmall);
                SetTextColor(hMemDC, RGB(100, 150, 200));
                
                int menuY = g_ScreenHeight - 60;
                SIZE sz;
                
                TCHAR strC[] = _T("[C] クレジット");
                GetTextExtentPoint32(hMemDC, strC, lstrlen(strC), &sz);
                ExtTextOut(hMemDC, (g_ScreenWidth / 6) - (sz.cx / 2), menuY, 0, NULL, strC, lstrlen(strC), NULL);

                TCHAR strH[] = _T("[H] 遊び方");
                GetTextExtentPoint32(hMemDC, strH, lstrlen(strH), &sz);
                ExtTextOut(hMemDC, (g_ScreenWidth / 2) - (sz.cx / 2), menuY, 0, NULL, strH, lstrlen(strH), NULL);

                TCHAR strG[] = _T("[G] GitHub");
                GetTextExtentPoint32(hMemDC, strG, lstrlen(strG), &sz);
                ExtTextOut(hMemDC, (g_ScreenWidth * 5 / 6) - (sz.cx / 2), menuY, 0, NULL, strG, lstrlen(strG), NULL);
            } 
            else if (g_State == STATE_CREDITS) {
                DrawCenteredText(hMemDC, g_hFontLarge, _T("--- CREDITS ---"), g_ScreenHeight / 2 - 160, RGB(0, 0, 0));
                
                DrawCenteredText(hMemDC, g_hFontMedium, _T("Creator"), g_ScreenHeight / 2 - 90, RGB(100, 100, 100));
                DrawCenteredText(hMemDC, g_hFontMedium, _T("keke"), g_ScreenHeight / 2 - 60, RGB(0, 0, 0));

                DrawCenteredText(hMemDC, g_hFontMedium, _T("Typing Dictionary"), g_ScreenHeight / 2 - 20, RGB(100, 100, 100));
                DrawCenteredText(hMemDC, g_hFontMedium, _T("白狐 様"), g_ScreenHeight / 2 + 10, RGB(0, 0, 0));

                DrawCenteredText(hMemDC, g_hFontMedium, _T("Sound Effects"), g_ScreenHeight / 2 + 50, RGB(100, 100, 100));
                DrawCenteredText(hMemDC, g_hFontMedium, _T("効果音ラボ 様"), g_ScreenHeight / 2 + 80, RGB(0, 0, 0));

                DrawCenteredText(hMemDC, g_hFontSmall, _T("Press Any Key or Tap to Return"), g_ScreenHeight - 80, RGB(150, 150, 150));
            }
            else if (g_State == STATE_HOWTO) {
                DrawCenteredText(hMemDC, g_hFontLarge, _T("--- HOW TO PLAY ---"), g_ScreenHeight / 2 - 140, RGB(0, 0, 0));
                DrawCenteredText(hMemDC, g_hFontSmall, _T("1. 画面に表示される文字をローマ字で入力してください"), g_ScreenHeight / 2 - 60, RGB(50, 50, 50));
                DrawCenteredText(hMemDC, g_hFontSmall, _T("2. 様々な入力パターンに対応しています"), g_ScreenHeight / 2 - 20, RGB(50, 50, 50));
                DrawCenteredText(hMemDC, g_hFontSmall, _T("3. ミスなく25コンボ繋ぐごとに制限時間が回復します"), g_ScreenHeight / 2 + 20, RGB(50, 50, 50));
                DrawCenteredText(hMemDC, g_hFontSmall, _T("   (25で1秒、50で2秒…とコンボ数に応じて回復量が増加します)"), g_ScreenHeight / 2 + 50, RGB(50, 50, 50));

                DrawCenteredText(hMemDC, g_hFontSmall, _T("Press Any Key or Tap to Return"), g_ScreenHeight - 80, RGB(150, 150, 150));
            }
            else if (g_State == STATE_GITHUB) {
                DrawCenteredText(hMemDC, g_hFontLarge, _T("--- GitHub ---"), g_ScreenHeight / 2 - 120, RGB(0, 0, 0));
                DrawCenteredText(hMemDC, g_hFontMedium, _T("Source Code"), g_ScreenHeight / 2 - 40, RGB(100, 100, 100));
                DrawCenteredText(hMemDC, g_hFontSmall, _T("https://github.com/keke-1026/tyceing"), g_ScreenHeight / 2 + 10, RGB(0, 100, 200));

                DrawCenteredText(hMemDC, g_hFontSmall, _T("Press Any Key or Tap to Return"), g_ScreenHeight - 80, RGB(150, 150, 150));
            }
            else if (g_State == STATE_PLAYING) {
                SelectObject(hMemDC, g_hFontSmall);
                SetTextColor(hMemDC, RGB(0, 0, 0));
                int remainMs = g_TimeLimitMs - (GetTickCount() - g_StartTime);
                if (remainMs < 0) remainMs = 0;
                
                int hudY = g_ScreenHeight - 90;
                wsprintf(buf, _T("Time: %d.%d"), remainMs / 1000, (remainMs / 100) % 10);
                ExtTextOut(hMemDC, 20, hudY, 0, NULL, buf, lstrlen(buf), NULL);
                
                wsprintf(buf, _T("Score: %d  Combo: %d  Miss: %d"), g_CorrectCount - g_MissCount, g_Combo, g_MissCount);
                ExtTextOut(hMemDC, 20, hudY + 30, 0, NULL, buf, lstrlen(buf), NULL);

                const WordData currentWord = g_Words[g_CurrentWordIndex];
                
                DrawCenteredText(hMemDC, g_hFontLarge, currentWord.display, g_ScreenHeight / 2 - 120, RGB(0, 0, 0));
                DrawCenteredText(hMemDC, g_hFontMedium, currentWord.kana, g_ScreenHeight / 2 - 40, RGB(120, 120, 120));

                TCHAR remainingRomaji[256];
                GetRemainingRomaji(remainingRomaji);
                
                SelectObject(hMemDC, g_hFontMedium);
                SIZE sizeTyped, sizeRem;
                GetTextExtentPoint32(hMemDC, g_TypedDisplay, lstrlen(g_TypedDisplay), &sizeTyped);
                GetTextExtentPoint32(hMemDC, remainingRomaji, lstrlen(remainingRomaji), &sizeRem);
                
                int totalWidth = sizeTyped.cx + sizeRem.cx;
                int startX = (g_ScreenWidth - totalWidth) / 2;
                int romajiY = g_ScreenHeight / 2 + 20;

                SetTextColor(hMemDC, RGB(180, 180, 180));
                ExtTextOut(hMemDC, startX, romajiY, 0, NULL, g_TypedDisplay, lstrlen(g_TypedDisplay), NULL);
                SetTextColor(hMemDC, RGB(0, 0, 0));
                ExtTextOut(hMemDC, startX + sizeTyped.cx, romajiY, 0, NULL, remainingRomaji, lstrlen(remainingRomaji), NULL);

                SelectObject(hMemDC, g_hFontMedium);
                SetTextColor(hMemDC, RGB(200, 200, 200));
                for (int i = 0; i < 64; i++) {
                    if (g_Drops[i].active) {
                        TCHAR dropStr[2] = { g_Drops[i].ch, _T('\0') };
                        ExtTextOut(hMemDC, g_Drops[i].x, g_Drops[i].y, 0, NULL, dropStr, 1, NULL);
                    }
                }
            }
            else if (g_State == STATE_RESULT) {
                DrawCenteredText(hMemDC, g_hFontLarge, _T("--- RESULT ---"), g_ScreenHeight / 2 - 120, RGB(0, 0, 0));
                
                wsprintf(buf, _T("Score: %d"), g_CorrectCount - g_MissCount);
                DrawCenteredText(hMemDC, g_hFontMedium, buf, g_ScreenHeight / 2 - 40, RGB(50, 50, 50));

                int playSeconds = g_TimeLimitMs / 1000;
                int kpsInt = g_CorrectCount / playSeconds;
                int kpsDec = ((g_CorrectCount * 10) / playSeconds) % 10;
                wsprintf(buf, _T("Average KPS: %d.%d"), kpsInt, kpsDec);
                DrawCenteredText(hMemDC, g_hFontMedium, buf, g_ScreenHeight / 2 + 10, RGB(50, 50, 50));

                DrawCenteredText(hMemDC, g_hFontSmall, _T("Press Enter to Title"), g_ScreenHeight - 60, RGB(100, 100, 100));
            }

            BitBlt(hdc, 0, 0, rect.right, rect.bottom, hMemDC, 0, 0, SRCCOPY);

            SelectObject(hMemDC, hOldBitmap);
            DeleteObject(hBitmap);
            DeleteDC(hMemDC);

            EndPaint(hWnd, &ps);
            break;
        }

        case WM_DESTROY:
            PlaySound(NULL, NULL, 0);
            if (g_CorrectSound) free(g_CorrectSound);
            if (g_MissSound) free(g_MissSound);
            
            DeleteObject(g_hFontLarge);
            DeleteObject(g_hFontMedium);
            DeleteObject(g_hFontSmall);
            KillTimer(hWnd, TIMER_ID);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    HWND hWnd;
    MSG msg;
    const TCHAR szClassName[] = _T("SushiBrainClass");

    g_ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    g_ScreenHeight = GetSystemMetrics(SM_CYSCREEN);

    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = szClassName;

    if (!RegisterClass(&wc)) return 0;
    hWnd = CreateWindow(szClassName, NULL, WS_POPUP | WS_VISIBLE, 0, 0, g_ScreenWidth, g_ScreenHeight, NULL, NULL, hInstance, NULL);
    if (!hWnd) return 0;
    UpdateWindow(hWnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
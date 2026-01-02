#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <string>
#include <windows.h>
#include <omp.h>
#include <algorithm>
#include <mmsystem.h> 

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib") 

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;

enum GameState { STATE_MENU, STATE_CONTROLS, STATE_PLAY };
GameState currentGameState = STATE_MENU;
int menuSelection = 0;

struct Texture {
    unsigned char* pixels = nullptr;
    int w = 0, h = 0, c = 0;
};

struct NPC {
    float x, y;
    float dirX = 0, dirY = 1;
    bool isDead = false;
    int dieFrame = -1;
    float walkFrame = 0;
    float dist = 0;
    float moveTimer = 0.0f;
    bool isMoving = false;
};

Texture texWall, texFloor, texGuiIdle, texSoldierStand;
vector<Texture> vGuiAnimation, vSoldierDie, vSoldierWalk;

wstring worldMap = 
    L"################################"
    L"#.......&..........#...........#"
    L"#..####...##...#...#...####....#"
    L"#..#.......&...#...#......#....#"
    L"#..#...#########...####...#....#"
    L"#..........#..........#...#..&.#"
    L"########...#...####...#...#....#"
    L"#..........#...#..&...#...#....#"
    L"#...&......#...#......#........#"
    L"#######.####...########...######"
    L"#..................&...........#"
    L"#..####...##########...####....#"
    L"#..&.#.........#.......#..&....#"
    L"#....#....&....#...&...#.......#"
    L"#..............#.......#.......#"
    L"################################";

int nMapW = 32;
int nMapH = 16;

void DrawPixelText(const string& text, bool selected) {
    if (selected) {
        cout << "\x1b[38;2;255;255;255m\x1b[48;2;200;0;0m  > " << text << " <  \x1b[0m" << endl;
    } else {
        cout << "\x1b[38;2;150;150;150m    " << text << "    \x1b[0m" << endl;
    }
}

HCURSOR LoadCustomCursor(const char* filename) {
    int w, h, channels;
    unsigned char* data = stbi_load(filename, &w, &h, &channels, 4); 
    if (!data) return LoadCursor(NULL, IDC_ARROW);
    HBITMAP hBitmap = CreateBitmap(w, h, 1, 32, data);
    HBITMAP hMonoBitmap = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii = { 0 }; ii.fIcon = FALSE; ii.xHotspot = w / 2; ii.yHotspot = h / 2;
    ii.hbmMask = hMonoBitmap; ii.hbmColor = hBitmap;
    HCURSOR hCustomCursor = CreateIconIndirect(&ii);
    stbi_image_free(data); DeleteObject(hBitmap); DeleteObject(hMonoBitmap);
    return hCustomCursor;
}

int main() {

    ios::sync_with_stdio(false);
    cin.tie(NULL);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HWND hConsole = GetConsoleWindow();
    
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
    printf("\x1b[?25l"); 

    auto loadTex = [](const char* path, Texture& t) {
        t.pixels = stbi_load(path, &t.w, &t.h, &t.c, 3);
        if (!t.pixels) printf("Error loading: %s\n", path);
    };

    loadTex("assets/walls.png", texWall);
    loadTex("assets/floor.png", texFloor);
    loadTex("assets/gui.png", texGuiIdle);
    loadTex("assets/soldier-before4.png", texSoldierStand);

    const char* walkFiles[] = {"assets/soldier-before.png", "assets/soldier-before2.png", "assets/soldier-before3.png", "assets/soldier-before4.png"};
    for(int i=0; i<4; i++) {
        Texture t; loadTex(walkFiles[i], t);
        if(t.pixels) vSoldierWalk.push_back(t);
    }

    for (int i = 2; i <= 14; i++) {
        string path = "assets/gui" + to_string(i) + ".png";
        Texture t; loadTex(path.c_str(), t);
        if (t.pixels) vGuiAnimation.push_back(t);
    }

    for (int i = 1; i <= 5; i++) {
        string name = (i == 1) ? "assets/soldier-die.png" : "assets/soldier-die" + to_string(i) + ".png";
        Texture t; loadTex(name.c_str(), t);
        if (t.pixels) vSoldierDie.push_back(t);
    }

    vector<NPC> enemies;
    for(int x=0; x<nMapW; x++) {
        for(int y=0; y<nMapH; y++) {
            if(worldMap[y*nMapW+x] == '&') {
                enemies.push_back({(float)x + 0.5f, (float)y + 0.5f});
                worldMap[y*nMapW+x] = '.'; 
            }
        }
    }

    HCURSOR hGameCursor = LoadCustomCursor("assets/cursor.png");
    SetClassLongPtr(hConsole, GCLP_HCURSOR, (LONG_PTR)hGameCursor);


    mciSendStringA("close all", NULL, 0, NULL);
    string musicCmd = "open \"assets/doom-music.wav\" type mpegvideo alias bgm";
    if (mciSendStringA(musicCmd.c_str(), NULL, 0, NULL) != 0) {
        mciSendStringA("open \"assets/doom-music.wav\" alias bgm", NULL, 0, NULL);
    }
    mciSendStringA("play bgm repeat", NULL, 0, NULL);

    float fPlayerX = 4.0f, fPlayerY = 4.0f, fPlayerA = 0.0f, fFOV = 3.14159f / 3.0f;
    bool bIsShooting = false;
    int nActiveFrame = -1; 
    bool bKeyWasPressed = false;

    auto tp1 = chrono::high_resolution_clock::now();

    while (true) {
        auto tp2 = chrono::high_resolution_clock::now();
        chrono::duration<float> elapsed = tp2 - tp1;
        tp1 = tp2;
        float fET = elapsed.count();
        

        if (fET > 0.1f) fET = 0.1f;

        if (currentGameState == STATE_MENU) {
            printf("\x1b[H\x1b[31m");
            cout << "  ######   #######  #######  #     # \n";
            cout << "  #     # #      # #       # ##   ## \n";
            cout << "  #     # #      # #       # # # # # \n";
            cout << "  #     # #      # #       # #  #  # \n";
            cout << "  #     # #      # #       # #     # \n";
            cout << "  ######   #######  #######  #     # \n";
            cout << "           CONSOLE-EDITION\x1b[0m\n\n";

            DrawPixelText("PLAY GAME", menuSelection == 0);
            DrawPixelText("CONTROLS", menuSelection == 1);
            DrawPixelText("EXIT GAME", menuSelection == 2);

            if ((GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) && !bKeyWasPressed) { menuSelection = (menuSelection - 1 + 3) % 3; bKeyWasPressed = true; }
            else if ((GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) && !bKeyWasPressed) { menuSelection = (menuSelection + 1) % 3; bKeyWasPressed = true; }
            else if (GetAsyncKeyState(VK_RETURN) & 0x8000 && !bKeyWasPressed) {
                if (menuSelection == 0) currentGameState = STATE_PLAY;
                if (menuSelection == 1) currentGameState = STATE_CONTROLS;
                if (menuSelection == 2) break;
                bKeyWasPressed = true;
                printf("\x1b[2J");
            }
            if (!GetAsyncKeyState(VK_UP) && !GetAsyncKeyState(VK_DOWN) && !GetAsyncKeyState('W') && !GetAsyncKeyState('S') && !GetAsyncKeyState(VK_RETURN)) bKeyWasPressed = false;
            Sleep(10); continue;
        }

        if (currentGameState == STATE_CONTROLS) {
            printf("\x1b[H\n\x1b[33m  === CONTROLS ===\x1b[0m\n\n");
            cout << "  W, S, A, D - Move Player\n  SHIFT      - Sprint\n  MOUSE      - Rotate View\n  ENTER      - Fire Weapon\n  ESC        - Back to Menu\n\n  Press BACKSPACE to return";
            if (GetAsyncKeyState(VK_BACK) & 0x8000) { currentGameState = STATE_MENU; printf("\x1b[2J"); Sleep(200); }
            Sleep(10); continue;
        }

        if (currentGameState == STATE_PLAY) {
            if (GetForegroundWindow() == hConsole) {
                SetCursor(hGameCursor);
                RECT rect; GetWindowRect(hConsole, &rect);
                int centerX = rect.left + (rect.right - rect.left) / 2;
                int centerY = rect.top + (rect.bottom - rect.top) / 2;
                POINT mousePos; GetCursorPos(&mousePos);
                

                fPlayerA += (float)(mousePos.x - centerX) * 0.0012f; 
                SetCursorPos(centerX, centerY);
                
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { currentGameState = STATE_MENU; printf("\x1b[2J"); Sleep(200); }


                float fMoveSpeed = 2.5f;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) fMoveSpeed = 5.5f;

                float moveStep = fMoveSpeed * fET;
                float nextX = fPlayerX;
                float nextY = fPlayerY;


                if (GetAsyncKeyState('W') & 0x8000) {
                    nextX += sinf(fPlayerA) * moveStep;
                    nextY += cosf(fPlayerA) * moveStep;
                }
                if (GetAsyncKeyState('S') & 0x8000) {
                    nextX -= sinf(fPlayerA) * moveStep;
                    nextY -= cosf(fPlayerA) * moveStep;
                }
                if (GetAsyncKeyState('A') & 0x8000) {
                    nextX -= cosf(fPlayerA) * moveStep;
                    nextY += sinf(fPlayerA) * moveStep;
                }
                if (GetAsyncKeyState('D') & 0x8000) {
                    nextX += cosf(fPlayerA) * moveStep;
                    nextY -= sinf(fPlayerA) * moveStep;
                }


                if (worldMap[(int)fPlayerY * nMapW + (int)nextX] != '#') {
                    fPlayerX = nextX;
                }
                if (worldMap[(int)nextY * nMapW + (int)fPlayerX] != '#') {
                    fPlayerY = nextY;
                }


                if ((GetAsyncKeyState(VK_RETURN) & 0x8000) && !bIsShooting) {
                    bIsShooting = true; 
                    nActiveFrame = 0;
                    PlaySoundA("assets/doom-shotgun.wav", NULL, SND_FILENAME | SND_ASYNC);

                    for(auto &en : enemies) {
                        if(en.isDead) continue;
                        float vecX = en.x - fPlayerX, vecY = en.y - fPlayerY;
                        float angleToEN = atan2f(vecX, vecY);
                        float angleDiff = fmodf(angleToEN - fPlayerA + 9.4247f, 6.2831f) - 3.1415f;
                        if(fabs(angleDiff) < 0.25f && en.dist < 12.0f) { en.isDead = true; en.dieFrame = 0; }
                    }
                }
            }

            for(auto &en : enemies) {
                if(en.isDead) { if(en.dieFrame < (int)vSoldierDie.size()-1) en.dieFrame++; continue; }
                en.moveTimer -= fET;
                if(en.moveTimer <= 0) {
                    en.moveTimer = 1.0f + (rand() % 2);
                    int r = rand() % 4;
                    if(r == 0) { en.dirX = 0; en.dirY = 1; en.isMoving = true; }
                    else if(r == 1) { en.dirX = 0; en.dirY = -1; en.isMoving = true; }
                    else if(r == 2) { en.dirX = 1; en.dirY = 0; en.isMoving = true; }
                    else { en.isMoving = false; }
                }
                if(en.isMoving) {

                    float nX = en.x + en.dirX * 2.0f * fET, nY = en.y + en.dirY * 2.0f * fET;
                    if(worldMap[(int)nY * nMapW + (int)nX] == '.') { en.x = nX; en.y = nY; en.walkFrame += fET * 10.0f; }
                    else { en.moveTimer = 0; }
                }
            }

            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hOut, &csbi);
            int nScreenWidth = csbi.srWindow.Right - csbi.srWindow.Left;
            int nScreenHeight = (csbi.srWindow.Bottom - csbi.srWindow.Top + 1) * 2;
            int nGuiH = 45;

            vector<string> lines(nScreenHeight / 2);
            #pragma omp parallel for
            for (int y = 0; y < nScreenHeight / 2; y++) {
                string currentLine = ""; currentLine.reserve(nScreenWidth * 45);
                for (int x = 0; x < nScreenWidth; x++) {
                    auto render = [&](int py) -> unsigned char* {
                        if (py >= nScreenHeight - nGuiH) {
                            Texture* curG = (bIsShooting && nActiveFrame < (int)vGuiAnimation.size() && nActiveFrame >= 0) ? &vGuiAnimation[nActiveFrame] : &texGuiIdle;
                            int tx = (int)((float)x / nScreenWidth * (curG->w - 1));
                            float tr = (float)(py - (nScreenHeight - nGuiH)) / nGuiH;
                            int ty = (int)(curG->h * 0.42f + tr * (curG->h * 0.58f));
                            unsigned char* p = &curG->pixels[(max(0, min(ty, curG->h-1)) * curG->w + tx) * 3];
                            if (!(p[0] > 250 && p[2] > 250)) return p;
                        }
                        
                        float fRayA = (fPlayerA - fFOV/2.0f) + ((float)x/nScreenWidth)*fFOV;
                        float fEyeX = sinf(fRayA), fEyeY = cosf(fRayA), fDist = 0; 
                        bool bHit = false; float fSampX = 0;
                        while(!bHit && fDist < 16.0f) {
                            fDist += 0.08f;
                            int nTestX = (int)(fPlayerX + fEyeX * fDist), nTestY = (int)(fPlayerY + fEyeY * fDist);
                            if (nTestX < 0 || nTestX >= nMapW || nTestY < 0 || nTestY >= nMapH) { bHit = true; fDist = 16.0f; }
                            else if (worldMap[nTestY * nMapW + nTestX] == '#') { 
                                bHit = true; 
                                float fHX = fPlayerX + fEyeX * fDist, fHY = fPlayerY + fEyeY * fDist;
                                float fMX = (float)nTestX + 0.5f, fMY = (float)nTestY + 0.5f;
                                float fTA = atan2f(fHY - fMY, fHX - fMX);
                                if (fTA >= -0.785f && fTA < 0.785f) fSampX = fHY - (float)nTestY;
                                else if (fTA >= 0.785f && fTA < 2.356f) fSampX = fHX - (float)nTestX;
                                else if (fTA >= -2.356f && fTA < -0.785f) fSampX = fHX - (float)nTestX;
                                else fSampX = fHY - (float)nTestY;
                            }
                        }

                        for(auto &en : enemies) {
                            float vX = en.x - fPlayerX, vY = en.y - fPlayerY;
                            en.dist = sqrtf(vX*vX + vY*vY);
                            float aEN = atan2f(vX, vY) - fPlayerA;
                            if(aEN < -3.14159f) aEN += 6.28318f; if(aEN > 3.14159f) aEN -= 6.28318f;
                            if(fabs(aEN) < fFOV * 0.5f && en.dist > 0.4f && en.dist < fDist) {
                                float fCeil = (nScreenHeight/2.0f) - nScreenHeight/en.dist;
                                float fFloor = (nScreenHeight/2.0f) + nScreenHeight/en.dist;
                                float fH = fFloor - fCeil;
                                float fMid = (0.5f * (aEN / (fFOV/2.0f)) + 0.5f) * nScreenWidth;
                                float fOff = en.isDead ? en.dieFrame * (fH/8.0f) : 0;
                                if(x >= fMid - fH/2.0f && x < fMid + fH/2.0f && py >= fCeil + fOff && py < fFloor + fOff) {
                                    Texture* st = en.isDead ? &vSoldierDie[en.dieFrame] : (en.isMoving ? &vSoldierWalk[(int)en.walkFrame % 4] : &texSoldierStand);
                                    int sx = (int)((x - (fMid - fH/2.0f)) / fH * (st->w-1));
                                    int sy = (int)((py - (fCeil + fOff)) / fH * (st->h-1));
                                    unsigned char* sp = &st->pixels[(max(0, min(sy, st->h-1))*st->w + sx)*3];
                                    if(!(sp[0] > 240 && sp[2] > 240)) return sp;
                                }
                            }
                        }

                        int nC = (int)((nScreenHeight/2.0) - nScreenHeight/fDist);
                        int nF = nScreenHeight - nC;
                        if (py < nC) { static unsigned char sky[] = {135, 206, 235}; return sky; }
                        else if (py < nF) {
                            int tx = (int)(fmod(fSampX, 1.0f) * (texWall.w - 1));
                            int ty = (int)(((float)py - nC) / (nF - nC) * (texWall.h - 1));
                            return &texWall.pixels[(max(0, min(ty, texWall.h - 1)) * texWall.w + tx) * 3];
                        } else {
                            float fFD = (nScreenHeight / 2.0f) / ((float)py - nScreenHeight / 2.0f);
                            int fx = (int)(abs(fPlayerX + fEyeX * fFD) * (texFloor.w - 1)) % texFloor.w;
                            int fy = (int)(abs(fPlayerY + fEyeY * fFD) * (texFloor.h - 1)) % texFloor.h;
                            return &texFloor.pixels[(fy * texFloor.w + fx) * 3];
                        }
                    };
                    unsigned char* p1 = render(y * 2);
                    unsigned char* p2 = render(y * 2 + 1);
                    char buf[64];
                    int len = sprintf(buf, "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm\xDF", p1[0], p1[1], p1[2], p2[0], p2[1], p2[2]);
                    currentLine.append(buf, len);
                }
                lines[y] = currentLine + "\x1b[0m";
            }
            string frame = "\x1b[H";
            for (int i = 0; i < (int)lines.size(); i++) frame += lines[i] + (i < (int)lines.size() - 1 ? "\n" : "");
            fwrite(frame.data(), 1, frame.size(), stdout);

            if (bIsShooting) {
                nActiveFrame++;
                if (nActiveFrame >= (int)vGuiAnimation.size()) { 
                    bIsShooting = false; 
                    nActiveFrame = -1; 
                }
            }
        }
    }
    mciSendStringA("close bgm", NULL, 0, NULL);
    return 0;
}
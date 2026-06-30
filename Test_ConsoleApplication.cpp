
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <iostream>
#include <audiopolicy.h>
#include <TlHelp32.h>   // 用于遍历进程
#include <string>       // 用于处理字符串
#include <ctime>
#include <bits/stdc++.h>
using namespace std;

// 根据进程 ID 获取进程名，返回 ANSI 字符串（方便 cout 输出）
string GetProcessName(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return "Unknown";

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                // 将宽字符进程名转换成 std::string
                int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                std::string name(len, 0);
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &name[0], len, NULL, NULL);
                // 去除末尾的 '\0'（string 构造已经自动处理了，但为了保险可以 pop_back）
                if (!name.empty() && name.back() == '\0') name.pop_back();
                CloseHandle(snapshot);
                return name;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return "Unknown";
}

// 检查 Edge 浏览器是否正在播放音频
bool IsEdgePlaying(IAudioSessionManager2* pSessionManager) {
    if (!pSessionManager) return false;

    IAudioSessionEnumerator* pEnumerator = nullptr;
    if (FAILED(pSessionManager->GetSessionEnumerator(&pEnumerator)))
        return false;

    bool playing = false;
    int count = 0;
    pEnumerator->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* pControl = nullptr;
        if (FAILED(pEnumerator->GetSession(i, &pControl))) continue;

        IAudioSessionControl2* pControl2 = nullptr;
        if (SUCCEEDED(pControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pControl2))) {
            DWORD pid = 0;
            pControl2->GetProcessId(&pid);
            if (pid != 0) {
                std::string name = GetProcessName(pid);
                if (name == "msedge.exe") {
                    AudioSessionState state;
                    pControl2->GetState(&state);
                    if (state == AudioSessionStateActive) {
                        playing = true;
                    }
                }
            }
            pControl2->Release();
        }
        pControl->Release();
        if (playing) break; // 找到就提前结束
    }
    pEnumerator->Release();
    return playing;
}


void Remind() {
    // 播放三次 2000Hz 的刺耳声，每次持续 400 毫秒，间隔 200 毫秒
    for (int i = 0; i < 6; ++i) {
        Beep(2000, 400);   // 2000Hz，0.4秒
        Sleep(200);        // 间隔0.2秒
    }
}

int main()
{
    cout << "版本号：MindTether v1.0.0" << endl;
    // 1. 初始化 COM 环境
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    // 2. 创建音频设备枚举器（这是通往扬声器的入口）
    IMMDeviceEnumerator* pEnumerator = nullptr;     //指针，类型为IMMDeviceEnumerator，作用是找到本地电脑上的音频设备，扬声器/耳机
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator
    );
    if (SUCCEEDED(hr)) {
        cout << "枚举器创建成功！" << endl;
    }
    else {
        cout << "创建失败，错误码: " << hex << hr << endl;
    }

    // 获取默认的声音渲染设备（扬声器/耳机）
    IMMDevice* pDevice = nullptr;
    pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    /*
    eRender：表示要找的是“输出/播放”设备（扬声器），不是录音设备。
    eConsole：表示是给普通桌面程序用的设备角色。
    &pDevice：把拿到的设备对象指针写进 pDevice。
    */
        // 从设备激活会话管理器
    IAudioSessionManager2* pSessionManager = nullptr;
    hr = pDevice->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL,
        NULL,
        (void**)&pSessionManager
    );
    if (SUCCEEDED(hr)) {
        cout << "会话管理器获取成功！" << endl;
    }
    else {
        cout << "获取会话管理器失败，错误码: " << hex << hr << endl;
    }
    
    // 从管理器拿到会话枚举器
    IAudioSessionEnumerator* pSessionEnumerator = nullptr;
    hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr)) {
        cout << "获取会话枚举器失败, hr=" << hex << hr << endl;
        return -1;
    }

    // 获取会话总数
    int sessionCount = 0;
    pSessionEnumerator->GetCount(&sessionCount);
    cout << "当前共有 " << sessionCount << " 个音频会话" << endl;

    // 变量初始化
    int REMIND_INTERVAL_SEC = 10 * 60; //休息时间 10分钟，测试时可改为10秒
jixv:
    bool wasPlaying = IsEdgePlaying(pSessionManager); // 先测一次，获取初态
    time_t pauseStartTime = time(nullptr);
    time_t lastRemindTime = 0;
    cout << "开始监控..." << endl;
    if (!wasPlaying) {
        printf("初始状态未播放，立即开始计时...\n");
        pauseStartTime = time(NULL);
        lastRemindTime = time(NULL);
    }
    while (true)
    {
        bool isPlaying = IsEdgePlaying(pSessionManager);
        // 状态切换处理
        if (wasPlaying && !isPlaying)
        {
            pauseStartTime = time(nullptr);   // 记录本次暂停开始时刻
            lastRemindTime = 0;               // 重置提醒计时，让第一次提醒从0开始
            cout << "暂停播放，开始计时..." << endl;
        }
        else if (!wasPlaying && isPlaying)
        {
            cout << "恢复播放，计时重置。" << endl;
            // 不需要重置变量，因为下次暂停会重新赋值
        }
        // 如果当前未播放，判断是否需要提醒
        if (!isPlaying)
        {
            time_t now = time(nullptr);
            if (lastRemindTime == 0) // 初次进入暂停，或刚从恢复转为暂停
                lastRemindTime = now; // 把当前时间作为上次提醒时间
            if (difftime(now, lastRemindTime) >= REMIND_INTERVAL_SEC)
            {
                bool playing = IsEdgePlaying(pSessionManager);
                cout << "Edge 正在播放吗？" << (playing ? "是" : "否") << endl;
                cout << "即将播放提醒音..." << endl;
                Remind();
                cout << "提醒音播放完毕。" << endl;
                lastRemindTime = now; // 更新，防止连续提醒
                break;
            }
        }
        wasPlaying = isPlaying;
        Sleep(5000); // 每5秒检测一次
    }

    cout << "1、我将会继续学习" << endl;
    cout << "2、我还要再休息5分钟" << endl;
    cout << "3、我完成本次学习了，进程退出" << endl;
    int x; cin >> x;
    if (x == 1) {
        goto jixv;         //代码跳转到检测视频状态开始
    }
    else if (x == 2) {
        REMIND_INTERVAL_SEC = 5 * 60;   // 休息时间临时改为 5 分钟
        goto jixv;
    }
    else if (x == 3) {
        // 清理
        pSessionEnumerator->Release();
        pSessionManager->Release();
        pDevice->Release();
        pEnumerator->Release();
        return 0;
    }
    return 0;
}


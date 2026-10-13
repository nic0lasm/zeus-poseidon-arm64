# Fonctions Windows importées par `Zeus.exe` 2.1 (CD)

Relevé par `tools/pe_triage.py --imports` sur `original/install/Zeus.exe`. C'est la liste des
fonctions que la couche hôte (`src/win32/`) doit fournir au code d'origine. L'exécutable GOG
pourra en différer légèrement : à régénérer à sa réception.

Total : 215 fonctions dans 11 bibliothèques.

| Bibliothèque | Nombre | Rôle | Remplacement prévu |
|---|---|---|---|
| `kernel32.dll` | 84 | fichiers, mémoire, temps, environnement | bibliothèque standard C++ / POSIX |
| `mss32.dll` | 50 | son : Miles Sound System | SDL2 audio + décodeur MP3 |
| `user32.dll` | 46 | fenêtre, messages, souris, clavier | SDL2 |
| `binkw32.dll` | 14 | vidéos Bink | décodeur vidéo (jalon 3) |
| `gdi32.dll` | 9 | palette, quelques dessins | SDL2 |
| `ijl10.dll` | 4 | décodage JPEG | ImageIO de macOS ou décodeur embarqué |
| `comdlg32.dll` | 3 | boîtes de dialogue de fichiers | SDL2 ou dialogue natif |
| `ole32.dll` | 2 | initialisation COM | sans effet |
| `shell32.dll` | 1 | ouverture d'un lien | sans effet ou `open` |
| `ddraw.dll` | 1 | affichage (surfaces DirectDraw, interfaces COM) | SDL2, tampon 16 bits |
| `winmm.dll` | 1 | horloge | SDL2 |

## `kernel32.dll` (84)

`CloseHandle`, `CompareFileTime`, `CompareStringA`, `CompareStringW`, `CopyFileA`, `CreateDirectoryA`, `CreateFileA`, `DeleteFileA`, `ExitProcess`, `FindClose`, `FindFirstFileA`, `FindNextFileA`, `FlushFileBuffers`, `FreeEnvironmentStringsA`, `FreeEnvironmentStringsW`, `GetACP`, `GetCommandLineA`, `GetCPInfo`, `GetCurrentDirectoryA`, `GetCurrentProcess`, `GetDiskFreeSpaceA`, `GetDriveTypeA`, `GetEnvironmentStrings`, `GetEnvironmentStringsW`, `GetEnvironmentVariableA`, `GetFileAttributesA`, `GetFileInformationByHandle`, `GetFileSize`, `GetFileType`, `GetFullPathNameA`, `GetLastError`, `GetLocalTime`, `GetModuleFileNameA`, `GetModuleHandleA`, `GetOEMCP`, `GetPrivateProfileStringA`, `GetProcAddress`, `GetStartupInfoA`, `GetStdHandle`, `GetStringTypeA`, `GetStringTypeW`, `GetSystemTime`, `GetTickCount`, `GetTimeZoneInformation`, `GetVersion`, `GetVersionExA`, `GetVolumeInformationA`, `GlobalMemoryStatus`, `HeapAlloc`, `HeapCreate`, `HeapDestroy`, `HeapFree`, `HeapReAlloc`, `HeapSize`, `InitializeCriticalSection`, `IsBadCodePtr`, `IsBadReadPtr`, `IsBadWritePtr`, `LCMapStringA`, `LCMapStringW`, `LoadLibraryA`, `MultiByteToWideChar`, `OutputDebugStringA`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `RaiseException`, `ReadFile`, `RemoveDirectoryA`, `RtlUnwind`, `SetCurrentDirectoryA`, `SetEndOfFile`, `SetEnvironmentVariableA`, `SetFileAttributesA`, `SetFilePointer`, `SetHandleCount`, `SetStdHandle`, `SetUnhandledExceptionFilter`, `Sleep`, `TerminateProcess`, `UnhandledExceptionFilter`, `VirtualAlloc`, `VirtualFree`, `WideCharToMultiByte`, `WriteFile`

## `mss32.dll` (50)

`_AIL_3D_provider_attribute@12`, `_AIL_3D_sample_playback_rate@4`, `_AIL_3D_sample_status@4`, `_AIL_allocate_3D_sample_handle@4`, `_AIL_allocate_sample_handle@4`, `_AIL_close_3D_provider@4`, `_AIL_close_stream@4`, `_AIL_end_3D_sample@4`, `_AIL_end_sample@4`, `_AIL_enumerate_3D_providers@12`, `_AIL_file_read@8`, `_AIL_get_preference@4`, `_AIL_init_sample@4`, `_AIL_mem_free_lock@4`, `_AIL_open_3D_listener@4`, `_AIL_open_3D_provider@4`, `_AIL_open_stream@12`, `_AIL_release_3D_sample_handle@4`, `_AIL_release_sample_handle@4`, `_AIL_sample_status@4`, `_AIL_serve@0`, `_AIL_set_3D_orientation@28`, `_AIL_set_3D_position@16`, `_AIL_set_3D_room_type@8`, `_AIL_set_3D_sample_effects_level@8`, `_AIL_set_3D_sample_file@8`, `_AIL_set_3D_sample_loop_count@8`, `_AIL_set_3D_sample_playback_rate@8`, `_AIL_set_3D_sample_volume@8`, `_AIL_set_filter_sample_preference@12`, `_AIL_set_preference@8`, `_AIL_set_sample_file@12`, `_AIL_set_sample_loop_count@8`, `_AIL_set_sample_pan@8`, `_AIL_set_sample_processor@12`, `_AIL_set_sample_reverb@16`, `_AIL_set_sample_volume@8`, `_AIL_set_stream_loop_count@8`, `_AIL_set_stream_position@8`, `_AIL_set_stream_volume@8`, `_AIL_shutdown@0`, `_AIL_start_3D_sample@4`, `_AIL_start_sample@4`, `_AIL_start_stream@4`, `_AIL_startup@0`, `_AIL_stream_position@4`, `_AIL_stream_status@4`, `_AIL_stream_volume@4`, `_AIL_WAV_info@8`, `_AIL_waveOutOpen@16`

## `user32.dll` (46)

`AdjustWindowRectEx`, `BeginPaint`, `ClientToScreen`, `CreateWindowExA`, `DefWindowProcA`, `DispatchMessageA`, `EndPaint`, `FindWindowA`, `GetAsyncKeyState`, `GetClientRect`, `GetCursorPos`, `GetDC`, `GetDesktopWindow`, `GetForegroundWindow`, `GetMenu`, `GetMessageA`, `GetSystemMetrics`, `GetWindowLongA`, `GetWindowRect`, `InvalidateRect`, `IsIconic`, `LoadBitmapA`, `LoadCursorA`, `LoadIconA`, `MessageBoxA`, `OpenIcon`, `PeekMessageA`, `PostMessageA`, `PostQuitMessage`, `PtInRect`, `RegisterClassA`, `RegisterWindowMessageA`, `ReleaseDC`, `SendMessageA`, `SetCursor`, `SetFocus`, `SetForegroundWindow`, `SetRect`, `SetWindowLongA`, `SetWindowPos`, `ShowCursor`, `ShowWindow`, `SystemParametersInfoA`, `TranslateMessage`, `UpdateWindow`, `WaitMessage`

## `binkw32.dll` (14)

`_BinkBufferClose@4`, `_BinkClose@4`, `_BinkCopyToBuffer@28`, `_BinkDDSurfaceType@4`, `_BinkDoFrame@4`, `_BinkGetError@0`, `_BinkGetSummary@8`, `_BinkNextFrame@4`, `_BinkOpen@8`, `_BinkOpenMiles@4`, `_BinkPause@8`, `_BinkService@4`, `_BinkSetSoundSystem@8`, `_BinkWait@4`

## `gdi32.dll` (9)

`DeleteObject`, `GetDeviceCaps`, `GetDIBits`, `GetObjectA`, `GetPaletteEntries`, `GetStockObject`, `GetTextExtentPointA`, `SetBkColor`, `TextOutA`

## `ijl10.dll` (4)

`#2`, `#3`, `#4`, `#5`

## `comdlg32.dll` (3)

`CommDlgExtendedError`, `GetOpenFileNameA`, `GetSaveFileNameA`

## `ole32.dll` (2)

`CoInitialize`, `CoUninitialize`

## `shell32.dll` (1)

`ShellExecuteA`

## `ddraw.dll` (1)

`DirectDrawCreate`

## `winmm.dll` (1)

`timeGetTime`

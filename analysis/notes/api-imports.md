# Fonctions Windows importées par `Zeus.exe` (édition GOG)

Produit par `tools/api_imports.py` à partir de `tools/pe_triage.py --imports` et du code de
`src/win32/`. C'est la liste des fonctions que la couche hôte fournit au code d'origine.

Une fonction **non écrite** arrête le programme en la nommant le jour où le jeu l'appelle.
Seules les fonctions de son et de vidéo non écrites sont sans effet et renvoient 0.

Total : 202 fonctions dans 10 bibliothèques, dont 178 écrites.

| Bibliothèque | Nombre | Écrites | Rôle | Réalisation |
|---|---|---|---|---|
| `kernel32.dll` | 82 | 79 | fichiers, mémoire, temps, environnement | `kernel32.cpp`, fichiers par `vfs.cpp` |
| `mss32.dll` | 50 | 30 | son : Miles Sound System | `mss.cpp` sur le mixeur de `audio.cpp` (SDL2, AudioToolbox) |
| `user32.dll` | 45 | 45 | fenêtre, messages, souris, clavier | `user32.cpp` et `desktop.cpp` (SDL2) |
| `binkw32.dll` | 14 | 13 | vidéos Bink | `bink.cpp`, décodage par ffmpeg |
| `gdi32.dll` | 3 | 3 | capacités d'écran, palette | `user32.cpp` |
| `ijl10.dll` | 3 | 3 | décodage JPEG (par numéro : 2 `ijlInit`, 3 `ijlFree`, 4 `ijlRead`) | `media.cpp`, par ImageIO de macOS |
| `ole32.dll` | 2 | 2 | initialisation COM | sans effet |
| `ddraw.dll` | 1 | 1 | affichage (surfaces DirectDraw, interfaces COM) | `ddraw.cpp`, tampon 16 bits 5-5-5 |
| `shell32.dll` | 1 | 1 | ouverture d'un lien | sans effet |
| `winmm.dll` | 1 | 1 | horloge | `user32.cpp` |

## `kernel32.dll` (82)

`CloseHandle`, `CompareFileTime`, `CompareStringA`, `CompareStringW`, `CopyFileA`, `CreateDirectoryA`, `CreateFileA`, `DeleteFileA`, `ExitProcess`, `FindClose`, `FindFirstFileA`, `FindNextFileA`, `FlushFileBuffers`, `FreeEnvironmentStringsA`, `FreeEnvironmentStringsW`, `GetACP`, `GetCommandLineA`, `GetCPInfo`, `GetCurrentDirectoryA`, `GetCurrentProcess`, `GetDiskFreeSpaceA`, `GetDriveTypeA`, `GetEnvironmentStrings`, `GetEnvironmentStringsW`, `GetEnvironmentVariableA`, `GetFileAttributesA`, `GetFileInformationByHandle`, `GetFileSize`, `GetFileType`, `GetFullPathNameA`, `GetLastError`, `GetLocalTime`, `GetModuleFileNameA`, `GetModuleHandleA`, `GetOEMCP`, `GetPrivateProfileStringA`, `GetProcAddress`, `GetStartupInfoA`, `GetStdHandle`, `GetStringTypeA`, `GetStringTypeW`, `GetSystemTime`, `GetTickCount`, `GetTimeZoneInformation`, `GetVersion`, `GetVersionExA`, `GetVolumeInformationA`, `HeapAlloc`, `HeapCreate`, `HeapDestroy`, `HeapFree`, `HeapReAlloc`, `HeapSize`, `IsBadCodePtr`, `IsBadReadPtr`, `IsBadWritePtr`, `LCMapStringA`, `LCMapStringW`, `LoadLibraryA`, `MultiByteToWideChar`, `OutputDebugStringA`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `RaiseException`, `ReadFile`, `RemoveDirectoryA`, `RtlUnwind`, `SetCurrentDirectoryA`, `SetEndOfFile`, `SetEnvironmentVariableA`, `SetFileAttributesA`, `SetFilePointer`, `SetHandleCount`, `SetStdHandle`, `SetUnhandledExceptionFilter`, `Sleep`, `TerminateProcess`, `UnhandledExceptionFilter`, `VirtualAlloc`, `VirtualFree`, `WideCharToMultiByte`, `WriteFile`

Non écrites : `CompareStringW`, `RaiseException`, `RtlUnwind`.

## `mss32.dll` (50)

`_AIL_3D_provider_attribute@12`, `_AIL_3D_sample_playback_rate@4`, `_AIL_3D_sample_status@4`, `_AIL_allocate_3D_sample_handle@4`, `_AIL_allocate_sample_handle@4`, `_AIL_close_3D_provider@4`, `_AIL_close_stream@4`, `_AIL_end_3D_sample@4`, `_AIL_end_sample@4`, `_AIL_enumerate_3D_providers@12`, `_AIL_file_read@8`, `_AIL_get_preference@4`, `_AIL_init_sample@4`, `_AIL_mem_free_lock@4`, `_AIL_open_3D_listener@4`, `_AIL_open_3D_provider@4`, `_AIL_open_stream@12`, `_AIL_release_3D_sample_handle@4`, `_AIL_release_sample_handle@4`, `_AIL_sample_status@4`, `_AIL_serve@0`, `_AIL_set_3D_orientation@28`, `_AIL_set_3D_position@16`, `_AIL_set_3D_room_type@8`, `_AIL_set_3D_sample_effects_level@8`, `_AIL_set_3D_sample_file@8`, `_AIL_set_3D_sample_loop_count@8`, `_AIL_set_3D_sample_playback_rate@8`, `_AIL_set_3D_sample_volume@8`, `_AIL_set_filter_sample_preference@12`, `_AIL_set_preference@8`, `_AIL_set_sample_file@12`, `_AIL_set_sample_loop_count@8`, `_AIL_set_sample_pan@8`, `_AIL_set_sample_processor@12`, `_AIL_set_sample_reverb@16`, `_AIL_set_sample_volume@8`, `_AIL_set_stream_loop_count@8`, `_AIL_set_stream_position@8`, `_AIL_set_stream_volume@8`, `_AIL_shutdown@0`, `_AIL_start_3D_sample@4`, `_AIL_start_sample@4`, `_AIL_start_stream@4`, `_AIL_startup@0`, `_AIL_stream_position@4`, `_AIL_stream_status@4`, `_AIL_stream_volume@4`, `_AIL_WAV_info@8`, `_AIL_waveOutOpen@16`

Sans effet pour l'instant (renvoient 0) : `_AIL_3D_provider_attribute@12`, `_AIL_3D_sample_playback_rate@4`, `_AIL_3D_sample_status@4`, `_AIL_allocate_3D_sample_handle@4`, `_AIL_close_3D_provider@4`, `_AIL_end_3D_sample@4`, `_AIL_open_3D_listener@4`, `_AIL_release_3D_sample_handle@4`, `_AIL_set_3D_orientation@28`, `_AIL_set_3D_position@16`, `_AIL_set_3D_room_type@8`, `_AIL_set_3D_sample_effects_level@8`, `_AIL_set_3D_sample_file@8`, `_AIL_set_3D_sample_loop_count@8`, `_AIL_set_3D_sample_playback_rate@8`, `_AIL_set_3D_sample_volume@8`, `_AIL_set_filter_sample_preference@12`, `_AIL_set_sample_processor@12`, `_AIL_set_sample_reverb@16`, `_AIL_start_3D_sample@4`.

## `user32.dll` (45)

`AdjustWindowRectEx`, `BeginPaint`, `ClientToScreen`, `CreateWindowExA`, `DefWindowProcA`, `DispatchMessageA`, `EndPaint`, `FindWindowA`, `GetAsyncKeyState`, `GetClientRect`, `GetCursorPos`, `GetDC`, `GetDesktopWindow`, `GetForegroundWindow`, `GetMenu`, `GetMessageA`, `GetSystemMetrics`, `GetWindowLongA`, `GetWindowRect`, `InvalidateRect`, `IsIconic`, `LoadCursorA`, `LoadIconA`, `MessageBoxA`, `OpenIcon`, `PeekMessageA`, `PostMessageA`, `PostQuitMessage`, `PtInRect`, `RegisterClassA`, `RegisterWindowMessageA`, `ReleaseDC`, `SendMessageA`, `SetCursor`, `SetFocus`, `SetForegroundWindow`, `SetRect`, `SetWindowLongA`, `SetWindowPos`, `ShowCursor`, `ShowWindow`, `SystemParametersInfoA`, `TranslateMessage`, `UpdateWindow`, `WaitMessage`

## `binkw32.dll` (14)

`_BinkBufferClose@4`, `_BinkClose@4`, `_BinkCopyToBuffer@28`, `_BinkDDSurfaceType@4`, `_BinkDoFrame@4`, `_BinkGetError@0`, `_BinkGetSummary@8`, `_BinkNextFrame@4`, `_BinkOpen@8`, `_BinkOpenMiles@4`, `_BinkPause@8`, `_BinkService@4`, `_BinkSetSoundSystem@8`, `_BinkWait@4`

Sans effet pour l'instant (renvoient 0) : `_BinkOpenMiles@4`.

## `gdi32.dll` (3)

`GetDeviceCaps`, `GetPaletteEntries`, `GetStockObject`

## `ijl10.dll` (3)

`#2`, `#3`, `#4`

## `ole32.dll` (2)

`CoInitialize`, `CoUninitialize`

## `ddraw.dll` (1)

`DirectDrawCreate`

## `shell32.dll` (1)

`ShellExecuteA`

## `winmm.dll` (1)

`timeGetTime`

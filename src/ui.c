/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
/* SPDX-License-Identifier: MPL-2.0 */
/* ui.c -- GDI drawing kit and theme for Shiyin. */
#include "shiyin.h"

/* SPDX-License-Identifier: MPL-2.0 */
/* ui.c additions: interface language table. */

typedef struct { const wchar_t *zh; const wchar_t *en; } SyStr;

static const SyStr g_strs[] = {
    { L"\u5df2\u6dfb\u52a0\u5230\u300c\u6253\u5f00\u65b9\u5f0f\u300d\u5217\u8868\u3002\n\n\u5728\u97f3\u9891\u6587\u4ef6\u4e0a\u70b9\u53f3\u952e \u2192 \u6253\u5f00\u65b9\u5f0f\uff0c\u5e94\u80fd\u770b\u5230\u300c\u62fe\u97f3\u300d\uff1b\u82e5\u5217\u8868\u6ca1\u6709\u7acb\u5373\u5237\u65b0\uff0c\u91cd\u542f\u8d44\u6e90\u7ba1\u7406\u5668\u6216\u6ce8\u9500\u4e00\u6b21\u5373\u53ef\u3002", L"Added to the Open with list.\n\nRight-click an audio file and pick Open with; Shiyin should be listed. If the list does not refresh, restart Explorer or sign out once." },
    { L"\u5df2\u8bbe\u4e3a\u9ed8\u8ba4\u64ad\u653e\u5668\uff1aMP3 / FLAC / WAV / AIFF / AU\u3002\n\nWindows 95/98 \u7acb\u5373\u751f\u6548\uff1bWindows 10 \u53ca\u4ee5\u540e\u7cfb\u7edf\u4e0d\u5141\u8bb8\u7a0b\u5e8f\u76f4\u63a5\u6539\u9ed8\u8ba4\uff0c\u53ea\u4f1a\u51fa\u73b0\u5728\u300c\u6253\u5f00\u65b9\u5f0f\u300d\u91cc\u3002", L"Set as the default player for MP3 / FLAC / WAV / AIFF / AU.\n\nEffective immediately on Windows 95/98; Windows 10 and later do not allow programs to change defaults, so the entry appears under Open with." },
    { L"\u5df2\u53d6\u6d88\u6587\u4ef6\u5173\u8054\uff08\u53ea\u6e05\u7406\u672c\u7a0b\u5e8f\u5199\u5165\u7684\u9879\uff09\u3002", L"Association removed (only entries written by this program)." },
    { L" \u00b7 \u7acb\u4f53\u58f0", L" \u00b7 Stereo" },
    { L" \u00b7 \u5355\u58f0\u9053", L" \u00b7 Mono" },
    { L"\u652f\u6301 MP3 / FLAC / WAV / AIFF / AU\u3002Windows 95/98/XP \u4f1a\u76f4\u63a5\u5199\u5165\u5173\u8054\uff1bWindows 10/11 \u4e0d\u5141\u8bb8\u7a0b\u5e8f\u6539\u9ed8\u8ba4\uff0c\u4f1a\u6253\u5f00\u7cfb\u7edf\u8bbe\u7f6e\u8ba9\u4f60\u9009\u62e9\u62fe\u97f3\u3002\u53d6\u6d88\u52fe\u9009\u53ea\u79fb\u9664\u672c\u7a0b\u5e8f\u5199\u5165\u7684\u9879\u3002", L"MP3 / FLAC / WAV / AIFF / AU. Windows 95/98/XP writes the association directly; Windows 10/11 does not allow programs to change defaults and opens the system settings page instead. Unchecking only removes what this program wrote." },
    { L"\u8bed\u8a00\uff1a\u4e2d\u6587", L"Language: Chinese" },
    { L"\u8bed\u8a00\uff1aEnglish", L"Language: English" },
    { L"\u70b9\u4e00\u4e0b\u5207\u6362\u754c\u9762\u8bed\u8a00", L"Click to switch the interface language" },
    { L"\u6d4b\u8bd5", L"Test" },
    { L"\u97f3\u9891\u5f15\u64ce\u672a\u80fd\u542f\u52a8\uff08\u65e0\u6cd5\u521b\u5efa\u64ad\u653e\u7ebf\u7a0b\uff09", L"Audio engine failed to start (cannot create the playback thread)" },
    { L"\u5217\u8868\u5faa\u73af", L"Repeat all" },
    { L"\u5355\u66f2\u5faa\u73af", L"Repeat one" },
    { L"\u968f\u673a\u64ad\u653e", L"Shuffle" },
    { L"\u987a\u5e8f\u64ad\u653e", L"In order" },
    { L"\u5217\u8868\u64ad\u653e\u5b8c\u6bd5", L"End of the playlist" },
    { L"\u6682\u4e0d\u652f\u6301\u8be5\u683c\u5f0f\uff08AAC/M4A/WMA/OGG\uff09\uff0c\u8bf7\u5148\u8f6c\u6362\u4e3a FLAC \u6216 MP3", L"Unsupported format (AAC/M4A/WMA/OGG); convert to FLAC or MP3 first" },
    { L"\u6ca1\u6709\u627e\u5230\u53ef\u64ad\u653e\u7684\u97f3\u9891\u6587\u4ef6", L"No playable audio files found" },
    { L"\u62d6\u52a8/\u79fb\u52a8\u524d\u8bf7\u5148\u6309\u201c\u624b\u52a8\u201d\u6392\u5e8f", L"Switch to manual order before dragging" },
    { L"\u97f3\u4e50\u6587\u4ef6 (*.mp3;*.flac;*.wav;*.aiff;*.au)", L"Music files (*.mp3;*.flac;*.wav;*.aiff;*.au)" },
    { L"\u6240\u6709\u6587\u4ef6 (*.*)", L"All files (*.*)" },
    { L"\u6dfb\u52a0\u97f3\u4e50\u6587\u4ef6", L"Add music files" },
    { L"\u65e0\u6cd5\u6253\u5f00\u6587\u4ef6\u5939\u9009\u62e9\u5668", L"Cannot open the folder picker" },
    { L"\u7cfb\u7edf\u4e0d\u652f\u6301\u6587\u4ef6\u5939\u9009\u62e9\uff0c\u8bf7\u76f4\u63a5\u628a\u6587\u4ef6\u5939\u62d6\u5165\u7a97\u53e3", L"This system cannot pick folders; drag the folder onto the window" },
    { L"\u6dfb\u52a0\u6587\u4ef6", L"Add files" },
    { L"\u6587\u4ef6\u5939", L"Folder" },
    { L"\u65e0\u5c01\u9762", L"No cover" },
    { L"\u672a\u5728\u64ad\u653e", L"Not playing" },
    { L"\u62d6\u5165\u97f3\u4e50\u6587\u4ef6\u6216\u6587\u4ef6\u5939\u5f00\u59cb", L"Drop music files or a folder to start" },
    { L"\u672a\u77e5\u6b4c\u624b", L"Unknown artist" },
    { L"\u683c\u5f0f", L"Format" },
    { L"\u91c7\u6837\u7387", L"Sample rate" },
    { L"  \u00b7  \u7acb\u4f53\u58f0", L"  \u00b7  Stereo" },
    { L"  \u00b7  \u5355\u58f0\u9053", L"  \u00b7  Mono" },
    { L"\u65f6\u957f", L"Length" },
    { L"\u5927\u5c0f", L"Size" },
    { L"\u641c\u7d22\u6b4c\u66f2\u3001\u6b4c\u624b\u3001\u4e13\u8f91", L"Search songs, artists, albums" },
    { L"\u6807\u9898 \u25bc", L"Title \u25bc" },
    { L"\u6807\u9898", L"Title" },
    { L"\u4e13\u8f91 \u25bc", L"Album \u25bc" },
    { L"\u4e13\u8f91", L"Album" },
    { L"\u65f6\u957f \u25bc", L"Length \u25bc" },
    { L"\u8fd8\u6ca1\u6709\u97f3\u4e50", L"No music yet" },
    { L"\u6dfb\u52a0\u6587\u4ef6\u3001\u6587\u4ef6\u5939\uff0c\u6216\u76f4\u63a5\u62d6\u5165\u7a97\u53e3", L"Add files or a folder, or drop them here" },
    { L"\u8bbe\u7f6e", L"Settings" },
    { L"\u8fd4\u56de", L"Back" },
    { L"\u6587\u4ef6\u5173\u8054", L"File association" },
    { L"\u53ea\u6709\u5728\u8fd9\u91cc\u52fe\u9009\u624d\u4f1a\u5199\u5165\u6ce8\u518c\u8868\uff0c\u542f\u52a8\u8f6f\u4ef6\u4e0d\u4f1a\u81ea\u52a8\u4fee\u6539\u3002", L"Only the checkbox here writes to the registry; starting the player never changes it." },
    { L"\u5c06\u62fe\u97f3\u8bbe\u4e3a\u97f3\u9891\u6587\u4ef6\u7684\u9ed8\u8ba4\u6253\u5f00\u7a0b\u5e8f", L"Make Shiyin the default app for audio files" },
    { L"\u6700\u5c0f\u5316\u65f6\u9690\u85cf\u5230\u6258\u76d8", L"Hide to tray when minimised" },
    { L"\u6700\u5c0f\u5316\u65f6\u663e\u793a\u8ff7\u4f60\u6a21\u5f0f", L"Show compact player when minimised" },
    { L"\u5173\u95ed\u7a97\u53e3\u65f6\u6536\u8fdb\u6258\u76d8", L"Close to tray" },
    { L"\u8ff7\u4f60\u7a97\u53e3\u603b\u5728\u6700\u524d", L"Keep the compact player on top" },
    { L"\u64ad\u653e\u65f6\u6de1\u5165\u6de1\u51fa", L"Fade in and out" },
    { L"\u65e0\u7f1d\u8854\u63a5\uff08\u66f2\u95f4\u4e0d\u4e2d\u65ad\uff09", L"Gapless playback" },
    { L"\u5916\u89c2\uff1a\u6df1\u8272", L"Theme: dark" },
    { L"\u5916\u89c2\uff1a\u6d45\u8272", L"Theme: light" },
    { L"\u5916\u89c2\uff1a\u8ddf\u968f\u65f6\u95f4", L"Theme: follow the clock" },
    { L"\u70b9\u4e00\u4e0b\u5207\u6362\u6df1\u8272 / \u6d45\u8272 / \u8ddf\u968f\u65f6\u95f4", L"Click to cycle dark / light / follow the clock" },
    { L"\u53d6\u6d88\u52fe\u9009\u53ea\u79fb\u9664\u672c\u7a0b\u5e8f\u5199\u5165\u7684\u9879\u3002", L"Unchecking only removes what this program wrote." },
    { L"\u65e0\u6cd5\u5199\u5165\u6ce8\u518c\u8868\uff1a\u5f53\u524d\u8fdb\u7a0b\u6743\u9650\u53d7\u9650\uff08\u4f4e\u5b8c\u6574\u6027\uff09", L"Cannot write to the registry: the process runs at low integrity" },
    { L"\u4f4e\u5b8c\u6574\u6027\u8fdb\u7a0b\u6ca1\u6709\u6ce8\u518c\u8868\u5199\u6743\u9650\uff0c\u70b9\u53f3\u4fa7\u6309\u94ae\u53ef\u91cd\u65b0\u4ee5\u6b63\u5e38\u6743\u9650\u542f\u52a8", L"Low-integrity processes cannot write to the registry; use the button on the right to restart with normal rights" },
    { L"\u5df2\u767b\u8bb0\u4e3a\u5019\u9009\u7a0b\u5e8f\uff0c\u8bf7\u5728\u7cfb\u7edf\u8bbe\u7f6e\u4e2d\u9009\u62e9\u300c\u62fe\u97f3\u300d", L"Registered as a candidate; choose Shiyin in the system settings" },
    { L"\u5f53\u524d\u72b6\u6001\uff1a\u5df2\u8bbe\u4e3a\u9ed8\u8ba4\u6253\u5f00\u65b9\u5f0f", L"Status: set as the default app" },
    { L"\u5f53\u524d\u72b6\u6001\uff1a\u672a\u8bbe\u7f6e\uff08\u7cfb\u7edf\u9ed8\u8ba4\u7a0b\u5e8f\u4e0d\u53d8\uff09", L"Status: not set (system defaults unchanged)" },
    { L"\u4ee5\u6b63\u5e38\u6743\u9650\u91cd\u542f", L"Restart as admin" },
    { L"\u672a\u9009\u62e9\u66f2\u76ee", L"No track selected" },
    { L"\u8fd8\u539f", L"Restore" },
    { L"\u6ca1\u6709\u6b4c\u8bcd", L"No lyrics" },
    { L"\u628a\u540c\u540d .lrc \u6587\u4ef6\u653e\u5728\u6b4c\u66f2\u65c1\u8fb9\u5373\u53ef\u663e\u793a", L"Place a matching .lrc file next to the track" },
    { L"\u62fe\u97f3", L"Shiyin" },
    { L"SHIYIN 1.0  \u00b7  \u672c\u5730\u97f3\u4e50\u5e93", L"SHIYIN 1.0  \u00b7  local library" },
    { L"\u6b4c\u8bcd", L"Lyrics" },
    { L"\u5df2\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939\uff0c\u8bf7\u53cc\u51fb Shiyin.exe", L"Folder opened; double-click Shiyin.exe" },
    { L"\u62fe\u97f3 \u00b7 Shiyin", L"Shiyin" },
    { L"\u9690\u85cf\u7a97\u53e3", L"Hide window" },
    { L"\u663e\u793a\u7a97\u53e3", L"Show window" },
    { L"\u6682\u505c", L"Pause" },
    { L"\u64ad\u653e", L"Play" },
    { L"\u4e0a\u4e00\u9996", L"Previous" },
    { L"\u4e0b\u4e00\u9996", L"Next" },
    { L"\u9000\u51fa", L"Quit" },
    { L"\u7cfb\u7edf\u6258\u76d8\u4e0d\u53ef\u7528\uff0c\u65e0\u6cd5\u9690\u85cf\u7a97\u53e3", L"The tray is unavailable; cannot hide the window" },
    { L"\u5df2\u53d6\u6d88\u9ed8\u8ba4\u6253\u5f00\u65b9\u5f0f", L"Default app cleared" },
    { L"\u8bf7\u5728\u7cfb\u7edf\u8bbe\u7f6e\u4e2d\u9009\u62e9\u62fe\u97f3", L"Choose Shiyin in the system settings" },
    { L"\u6743\u9650\u53d7\u9650\uff0c\u8bf7\u4ece\u8d44\u6e90\u7ba1\u7406\u5668\u8fd0\u884c", L"Permissions are restricted; run from Explorer" },
    { L"\u5199\u5165\u6ce8\u518c\u8868\u5931\u8d25", L"Registry write failed" },
    { L"\u5df2\u8bbe\u4e3a\u9ed8\u8ba4\u6253\u5f00\u65b9\u5f0f", L"Set as the default app" },
    { L"\u5173\u95ed\u7a97\u53e3\u5c06\u6536\u8fdb\u6258\u76d8", L"Closing will hide to the tray" },
    { L"\u5173\u95ed\u7a97\u53e3\u5c06\u9000\u51fa\u7a0b\u5e8f", L"Closing will exit the program" },
    { L"\u5df2\u5f00\u542f\u65e0\u7f1d\u8854\u63a5", L"Gapless playback on" },
    { L"\u5df2\u5173\u95ed\u65e0\u7f1d\u8854\u63a5", L"Gapless playback off" },
    { L"\u5df2\u5f00\u542f\u6de1\u5165\u6de1\u51fa", L"Fades on" },
    { L"\u5df2\u5173\u95ed\u6de1\u5165\u6de1\u51fa", L"Fades off" },
    { L"\u8ff7\u4f60\u7a97\u53e3\u5c06\u7f6e\u9876", L"Compact player will stay on top" },
    { L"\u8ff7\u4f60\u7a97\u53e3\u4e0d\u518d\u7f6e\u9876", L"Compact player will not stay on top" },
    { L"\u6700\u5c0f\u5316\u540e\u663e\u793a\u8ff7\u4f60\u6a21\u5f0f", L"Minimising shows the compact player" },
    { L"\u6700\u5c0f\u5316\u540e\u4e0d\u663e\u793a\u8ff7\u4f60\u6a21\u5f0f", L"Minimising keeps the taskbar entry" },
    { L"\u6700\u5c0f\u5316\u540e\u5c06\u9690\u85cf\u5230\u6258\u76d8", L"Minimising hides to the tray" },
    { L"\u6700\u5c0f\u5316\u540e\u5c06\u7559\u5728\u4efb\u52a1\u680f", L"Minimising keeps the window in the taskbar" },
    { L"\u62d6\u62fd\u6392\u5e8f\u524d\u8bf7\u5148\u6062\u590d\u624b\u52a8\u987a\u5e8f", L"Restore manual order before dragging" },
    { L"\u641c\u7d22\u65f6\u4e0d\u80fd\u62d6\u62fd\u6392\u5e8f", L"Cannot reorder while searching" },
    { L"M3U \u64ad\u653e\u5217\u8868 (*.m3u)", L"M3U playlist (*.m3u)" },
    { L"\u65e0\u6cd5\u5199\u5165\u64ad\u653e\u5217\u8868", L"Cannot write the playlist" },
    { L"\u64ad\u653e\u5217\u8868\u5df2\u5bfc\u51fa", L"Playlist exported" },
    { L"\u65e0\u6cd5\u8bfb\u53d6\u64ad\u653e\u5217\u8868", L"Cannot read the playlist" },
    { L"\u5df2\u52a0\u5165\u6b4c\u5355\uff1a", L"Added to the library: " },
    { L"\u5df2\u5728\u6b4c\u5355\u4e2d\uff1a", L"Already in the library: " },
    { L"\u4ece\u5217\u8868\u79fb\u9664", L"Remove from list" },
    { L"\u6253\u5f00\u6240\u5728\u6587\u4ef6\u5939", L"Open folder" },
    { L"\u590d\u5236\u8def\u5f84", L"Copy path" },
    { L"\u6dfb\u52a0\u6587\u4ef6...", L"Add files..." },
    { L"\u4e0a\u79fb", L"Move up" },
    { L"\u4e0b\u79fb", L"Move down" },
    { L"\u6062\u590d\u624b\u52a8\u987a\u5e8f", L"Restore manual order" },
    { L"\u9690\u85cf\u5230\u6258\u76d8", L"Hide to tray" },
    { L"\u5bfc\u5165\u64ad\u653e\u5217\u8868...", L"Import playlist..." },
    { L"\u5bfc\u51fa\u64ad\u653e\u5217\u8868...", L"Export playlist..." },
    { L"\u6e05\u7a7a\u5217\u8868", L"Clear list" },
    { L"\u8f93\u51fa\u8bbe\u5907", L"Output device" },
    { L"\u7cfb\u7edf\u9ed8\u8ba4", L"System default" },
    { L"\u5173\u95ed\u5b9a\u65f6", L"Clear timer" },
    { L"15 \u5206\u949f\u540e\u505c\u6b62", L"Stop after 15 minutes" },
    { L"30 \u5206\u949f\u540e\u505c\u6b62", L"Stop after 30 minutes" },
    { L"60 \u5206\u949f\u540e\u505c\u6b62", L"Stop after 60 minutes" },
    { L"\u7761\u7720\u5b9a\u65f6", L"Sleep timer" },
    { L"\u6dfb\u52a0\u5230\u300c\u6253\u5f00\u65b9\u5f0f\u300d", L"Add to Open with" },
    { L"\u8bbe\u4e3a\u9ed8\u8ba4\u64ad\u653e\u5668", L"Set as default player" },
    { L"\u53d6\u6d88\u6587\u4ef6\u5173\u8054", L"Remove association" },
    { L"\u7761\u7720\u5b9a\u65f6\uff1a\u5df2\u505c\u6b62\u64ad\u653e", L"Sleep timer: playback stopped" },
    { L"\u65e0\u6cd5\u89e3\u7801\u8fd9\u4e2a\u6587\u4ef6", L"Cannot decode this file" },
    { L"\u6253\u4e0d\u5f00\u58f0\u5361\u8f93\u51fa\uff08\u8bbe\u5907\u88ab\u5360\u7528\u6216\u683c\u5f0f\u4e0d\u652f\u6301\uff09", L"Cannot open the audio device (busy or unsupported format)" },
    { L"\u5185\u5b58\u4e0d\u8db3\uff0c\u65e0\u6cd5\u64ad\u653e", L"Out of memory" },
    { L"\u58f0\u5361\u6ca1\u6709\u54cd\u5e94\uff0c\u64ad\u653e\u5df2\u505c\u6b62", L"The audio device stopped responding; playback halted" },
    { L"\u64ad\u653e\u5931\u8d25", L"Playback failed" },
    { L"\u8f93\u51fa\u8bbe\u5907\uff1a\u7cfb\u7edf\u9ed8\u8ba4\uff08\u4e0b\u6b21\u64ad\u653e\u751f\u6548\uff09", L"Output device: system default (from the next track)" },
    { L"\u5df2\u5207\u6362\u8f93\u51fa\u8bbe\u5907\uff08\u4e0b\u6b21\u64ad\u653e\u751f\u6548\uff09", L"Output device changed (from the next track)" },
    { L"\u5df2\u53d6\u6d88\u7761\u7720\u5b9a\u65f6", L"Sleep timer cleared" },
    { L"\u64ad\u653e\u5217\u8868 (*.m3u;*.m3u8;*.pls)", L"Playlist (*.m3u;*.m3u8;*.pls)" },
    { L"\u64ad\u653e\u5217\u8868\u91cc\u6ca1\u6709\u53ef\u7528\u7684\u6587\u4ef6", L"The playlist has no usable files" },
    { L"\u82e5\u5217\u8868\u6ca1\u6709\u7acb\u5373\u5237\u65b0\uff0c\u91cd\u542f\u8d44\u6e90\u7ba1\u7406\u5668\u6216\u6ce8\u9500\u4e00\u6b21\u5373\u53ef\u3002", L"If the list does not refresh, restart Explorer or sign out once." },
    { L"\u5df2\u6536\u8fdb\u6258\u76d8\uff0c\u53f3\u952e\u56fe\u6807\u53ef\u9000\u51fa", L"Hidden to the tray; right-click the icon to quit" },
    { L"\u5df2\u53d6\u6d88\u6587\u4ef6\u5173\u8054\u3002", L"Association removed." },
    { L"\u5df2\u8bbe\u4e3a\u9ed8\u8ba4\u64ad\u653e\u5668\u3002", L"Set as the default player." },
    { L"\u5df2\u6dfb\u52a0\u5230\u300c\u6253\u5f00\u65b9\u5f0f\u300d\u3002", L"Added to Open with." },
    { L"\u6d4b\u8bd5", L"Test" },
    { L"\u62fe\u97f3\u97f3\u4e50\u6587\u4ef6", L"Shiyin music file" },
    { L"\u62fe\u97f3 \u00b7 \u672c\u5730\u97f3\u4e50\u64ad\u653e\u5668", L"Shiyin \u00b7 local music player" },
    { L"\u672a\u77e5", L"Unknown" },
};

static int tr_eq(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int g_lang;   /* 0 = Chinese, 1 = English */

const wchar_t *tr(const wchar_t *s)
{
    int i;
    if (!s || g_lang == 0)
        return s;
    for (i = 0; i < (int)(sizeof(g_strs) / sizeof(g_strs[0])); i++) {
        if (tr_eq(g_strs[i].zh, s))
            return g_strs[i].en;
    }
    return s;
}


int g_dpi = 96;

#define MAX_FONTS 24
static HFONT g_fonts[MAX_FONTS];
static int   g_fpx[MAX_FONTS];
static int   g_fwt[MAX_FONTS];
static int   g_fn;
static char  g_face[LF_FACESIZE];

/* GDI object cache.  Windows 95 has a tiny per-process GDI heap, and the UI
 * repaints several times per second while playing, so brushes and pens are
 * created once and reused instead of being churned every frame. */
#define OBJ_CACHE 96
static struct { COLORREF c; HBRUSH h; } g_brushes[OBJ_CACHE];
static int g_brush_n;
static struct { COLORREF c; int w; HPEN h; } g_pens[OBJ_CACHE];
static int g_pen_n;

static HBRUSH ui_brush(COLORREF c)
{
    int i;
    for (i = 0; i < g_brush_n; i++)
        if (g_brushes[i].c == c)
            return g_brushes[i].h;
    if (g_brush_n < OBJ_CACHE) {
        HBRUSH h = CreateSolidBrush(c);
        g_brushes[g_brush_n].c = c;
        g_brushes[g_brush_n].h = h;
        g_brush_n++;
        return h;
    }
    {
        HBRUSH old = g_brushes[0].h;
        g_brushes[0].h = CreateSolidBrush(c);
        g_brushes[0].c = c;
        if (old)
            DeleteObject(old);
        return g_brushes[0].h;
    }
}

static HPEN ui_pen(COLORREF c, int w)
{
    int i;
    for (i = 0; i < g_pen_n; i++)
        if (g_pens[i].c == c && g_pens[i].w == w)
            return g_pens[i].h;
    if (g_pen_n < OBJ_CACHE) {
        HPEN h = CreatePen(PS_SOLID, w, c);
        g_pens[g_pen_n].c = c;
        g_pens[g_pen_n].w = w;
        g_pens[g_pen_n].h = h;
        g_pen_n++;
        return h;
    }
    {
        HPEN old = g_pens[0].h;
        g_pens[0].h = CreatePen(PS_SOLID, w, c);
        g_pens[0].c = c;
        g_pens[0].w = w;
        if (old)
            DeleteObject(old);
        return g_pens[0].h;
    }
}
static int   g_uni = 1;

int S(int px)
{
    return (px * g_dpi + 48) / 96;
}

static int face_exists(HDC dc, const char *name)
{
    HFONT f = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, name);
    char got[LF_FACESIZE];
    HFONT old;
    int ok = 0;
    if (!f)
        return 0;
    old = (HFONT)SelectObject(dc, f);
    got[0] = 0;
    if (GetTextFaceA(dc, LF_FACESIZE, got) > 0) {
        int i;
        ok = 1;
        for (i = 0; name[i] || got[i]; i++) {
            char a = name[i], b = got[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) {
                ok = 0;
                break;
            }
        }
    }
    SelectObject(dc, old);
    DeleteObject(f);
    return ok;
}

void ui_init(void)
{
    HDC dc = GetDC(NULL);
    static const char *cands[] = {
        "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", "Tahoma",
        "MS Shell Dlg", "MS Sans Serif", 0
    };
    int i;

    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    if (g_dpi < 96)
        g_dpi = 96;
    if (g_dpi > 240)
        g_dpi = 240;

    for (i = 0; cands[i]; i++) {
        if (face_exists(dc, cands[i])) {
            int k = 0;
            while (cands[i][k]) {
                g_face[k] = cands[i][k];
                k++;
            }
            g_face[k] = 0;
            break;
        }
    }
    if (!g_face[0]) {
        g_face[0] = 'M'; g_face[1] = 'S'; g_face[2] = ' '; g_face[3] = 'S';
        g_face[4] = 'a'; g_face[5] = 'n'; g_face[6] = 's'; g_face[7] = ' ';
        g_face[8] = 'S'; g_face[9] = 'e'; g_face[10] = 'r'; g_face[11] = 'i';
        g_face[12] = 'f'; g_face[13] = 0;
    }
    {
        SIZE sz;
        memset(&sz, 0, sizeof(sz));
        if (!GetTextExtentPoint32W(dc, L"\u6D4B\u8BD5", 2, &sz))
            g_uni = 0;
        if (sz.cx <= 0)
            g_uni = 0;
    }
    ReleaseDC(NULL, dc);
}

HFONT ui_font(int px, int weight)
{
    int i;
    for (i = 0; i < g_fn; i++)
        if (g_fpx[i] == px && g_fwt[i] == weight)
            return g_fonts[i];
    if (g_fn >= MAX_FONTS) {
        DeleteObject(g_fonts[0]);
        for (i = 1; i < MAX_FONTS; i++) {
            g_fonts[i - 1] = g_fonts[i];
            g_fpx[i - 1] = g_fpx[i];
            g_fwt[i - 1] = g_fwt[i];
        }
        g_fn = MAX_FONTS - 1;
    }
    {
        HFONT f = CreateFontA(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, g_face);
        g_fonts[g_fn] = f;
        g_fpx[g_fn] = px;
        g_fwt[g_fn] = weight;
        g_fn++;
        return f;
    }
}

void ui_done(void)
{
    int i;
    for (i = 0; i < g_fn; i++)
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
    g_fn = 0;
    for (i = 0; i < g_brush_n; i++)
        if (g_brushes[i].h)
            DeleteObject(g_brushes[i].h);
    g_brush_n = 0;
    for (i = 0; i < g_pen_n; i++)
        if (g_pens[i].h)
            DeleteObject(g_pens[i].h);
    g_pen_n = 0;
}

void ui_fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r;
    HBRUSH b = ui_brush(c);
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;
    FillRect(dc, &r, b);
}

void ui_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c)
{
    HPEN p = ui_pen(c, 1);
    HPEN old = (HPEN)SelectObject(dc, p);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
}

void ui_round(HDC dc, int x, int y, int w, int h, int r, COLORREF fill, COLORREF border)
{
    HBRUSH b = 0;
    HPEN p = 0;
    HBRUSH ob;
    HPEN op;
    if (w <= 0 || h <= 0)
        return;
    if (fill != CLR_INVALID)
        b = ui_brush(fill);
    if (border != CLR_INVALID)
        p = ui_pen(border, 1);
    ob = (HBRUSH)SelectObject(dc, b ? b : GetStockObject(NULL_BRUSH));
    op = (HPEN)SelectObject(dc, p ? p : GetStockObject(NULL_PEN));
    RoundRect(dc, x, y, x + w, y + h, r * 2, r * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    (void)b;
    (void)p;
}

void ui_gradient(HDC dc, int x, int y, int w, int h, COLORREF top, COLORREF bottom)
{
    int i, bands = h > 96 ? 96 : (h > 0 ? h : 1);
    int tr = GetRValue(top), tg = GetGValue(top), tb = GetBValue(top);
    int br = GetRValue(bottom), bg = GetGValue(bottom), bb = GetBValue(bottom);
    for (i = 0; i < bands; i++) {
        int y0 = y + (h * i) / bands;
        int y1 = y + (h * (i + 1)) / bands;
        int r = tr + (br - tr) * i / (bands > 1 ? bands - 1 : 1);
        int g = tg + (bg - tg) * i / (bands > 1 ? bands - 1 : 1);
        int b = tb + (bb - tb) * i / (bands > 1 ? bands - 1 : 1);
        ui_fill(dc, x, y0, w, y1 - y0, RGB(r, g, b));
    }
}

void ui_text(HDC dc, const wchar_t *s, int len, RECT *r, COLORREF color, HFONT f, UINT flags)
{
    HFONT old;
    s = tr(s);
    if (!s)
        return;
    old = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    if (g_uni) {
        int h = DrawTextW(dc, s, len, r, flags);
        /* Windows 95 may expose W entry points that fail; drop to ANSI. */
        if (h == 0 && s[0] && !(flags & DT_CALCRECT))
            g_uni = 0;
        else {
            SelectObject(dc, old);
            return;
        }
    }
    {
        char buf[2048];
        w_ansi(s, buf, sizeof(buf));
        DrawTextA(dc, buf, -1, r, flags);
    }
    SelectObject(dc, old);
}

void ui_text_at(HDC dc, const wchar_t *s, int len, int x, int y, COLORREF color, HFONT f)
{
    RECT r;
    s = tr(s);
    r.left = x;
    r.top = y;
    r.right = x + 4096;
    r.bottom = y + S(64);
    ui_text(dc, s, len, &r, color, f, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

int ui_text_w(HDC dc, const wchar_t *s, int len, HFONT f)
{
    SIZE sz;
    HFONT old;
    memset(&sz, 0, sizeof(sz));
    if (!s || !dc || !f)
        return 0;
    if (len < 0)
        len = w_len(s);
    if (!len)
        return 0;
    old = (HFONT)SelectObject(dc, f);
    if (g_uni) {
        if (!GetTextExtentPoint32W(dc, s, len, &sz))
            g_uni = 0;
    }
    if (!g_uni) {
        char buf[2048];
        w_ansi(s, buf, sizeof(buf));
        GetTextExtentPoint32A(dc, buf, (int)strlen(buf), &sz);
    }
    SelectObject(dc, old);
    return sz.cx;
}

/* ------------------------------------------------------------------ */
/*  icons                                                             */
/* ------------------------------------------------------------------ */

static void tri(HDC dc, int x, int y, int size, COLORREF c, int dir)
{
    POINT p[3];
    HBRUSH b = ui_brush(c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    if (dir > 0) {
        p[0].x = x + (size * 30) / 100; p[0].y = y + (size * 16) / 100;
        p[1].x = x + (size * 30) / 100; p[1].y = y + (size * 84) / 100;
        p[2].x = x + (size * 82) / 100; p[2].y = y + (size * 50) / 100;
    } else {
        p[0].x = x + (size * 70) / 100; p[0].y = y + (size * 16) / 100;
        p[1].x = x + (size * 70) / 100; p[1].y = y + (size * 84) / 100;
        p[2].x = x + (size * 18) / 100; p[2].y = y + (size * 50) / 100;
    }
    Polygon(dc, p, 3);
    SelectObject(dc, ob);
}

static void arrow_head(HDC dc, int x, int y, int size, COLORREF c, int dir)
{
    /* dir: 0 right, 1 down, 2 left, 3 up */
    POINT p[3];
    HBRUSH b = ui_brush(c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    int s = size;
    if (dir == 0) {
        p[0].x = x; p[0].y = y - s; p[1].x = x + s; p[1].y = y; p[2].x = x; p[2].y = y + s;
    } else if (dir == 1) {
        p[0].x = x - s; p[0].y = y; p[1].x = x; p[1].y = y + s; p[2].x = x + s; p[2].y = y;
    } else if (dir == 2) {
        p[0].x = x; p[0].y = y - s; p[1].x = x - s; p[1].y = y; p[2].x = x; p[2].y = y + s;
    } else {
        p[0].x = x - s; p[0].y = y; p[1].x = x; p[1].y = y - s; p[2].x = x + s; p[2].y = y;
    }
    Polygon(dc, p, 3);
    SelectObject(dc, ob);
}

void ui_glyph(HDC dc, int glyph, int x, int y, int size, COLORREF c)
{
    int s = size;
    switch (glyph) {
    case GLYPH_ORDER: {
        POINT p[3];
        int sh = s * 14 / 100;
        int ymid = y + s / 2;
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        if (sh < 2)
            sh = 2;
        ui_fill(dc, x + s * 10 / 100, ymid - sh / 2, s * 48 / 100, sh, c);
        p[0].x = x + s * 54 / 100; p[0].y = y + s * 24 / 100;
        p[1].x = x + s * 54 / 100; p[1].y = y + s * 76 / 100;
        p[2].x = x + s * 88 / 100; p[2].y = ymid;
        Polygon(dc, p, 3);
        SelectObject(dc, ob);
        break;
    }
    case GLYPH_PLAY:
        tri(dc, x, y, s, c, 1);
        break;
    case GLYPH_PAUSE: {
        int w = (s * 17) / 100;
        ui_fill(dc, x + (s * 27) / 100, y + (s * 18) / 100, w, (s * 64) / 100, c);
        ui_fill(dc, x + (s * 56) / 100, y + (s * 18) / 100, w, (s * 64) / 100, c);
        break;
    }
    case GLYPH_PREV: {
        POINT p[3];
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        ui_fill(dc, x + s * 14 / 100, y + s * 20 / 100, s * 9 / 100, s * 60 / 100, c);
        p[0].x = x + s * 78 / 100; p[0].y = y + s * 16 / 100;
        p[1].x = x + s * 78 / 100; p[1].y = y + s * 84 / 100;
        p[2].x = x + s * 30 / 100; p[2].y = y + s * 50 / 100;
        Polygon(dc, p, 3);
        SelectObject(dc, ob);
        break;
    }
    case GLYPH_NEXT: {
        POINT p[3];
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        p[0].x = x + s * 22 / 100; p[0].y = y + s * 16 / 100;
        p[1].x = x + s * 22 / 100; p[1].y = y + s * 84 / 100;
        p[2].x = x + s * 70 / 100; p[2].y = y + s * 50 / 100;
        Polygon(dc, p, 3);
        ui_fill(dc, x + s * 77 / 100, y + s * 20 / 100, s * 9 / 100, s * 60 / 100, c);
        SelectObject(dc, ob);
        break;
    }
    case GLYPH_SHUFFLE: {
        HPEN p = ui_pen(c, (s > 26 ? 2 : 1));
        HPEN op = (HPEN)SelectObject(dc, p);
        POINT a[4], b[4];
        a[0].x = x + s / 10; a[0].y = y + s / 3;
        a[1].x = x + s * 42 / 100; a[1].y = y + s / 3;
        a[2].x = x + s * 62 / 100; a[2].y = y + s * 2 / 3;
        a[3].x = x + s * 84 / 100; a[3].y = y + s * 2 / 3;
        b[0].x = x + s / 10; b[0].y = y + s * 2 / 3;
        b[1].x = x + s * 42 / 100; b[1].y = y + s * 2 / 3;
        b[2].x = x + s * 62 / 100; b[2].y = y + s / 3;
        b[3].x = x + s * 84 / 100; b[3].y = y + s / 3;
        Polyline(dc, a, 4);
        Polyline(dc, b, 4);
        SelectObject(dc, op);
        arrow_head(dc, x + s * 88 / 100, y + s * 2 / 3, s / 10, c, 0);
        arrow_head(dc, x + s * 88 / 100, y + s / 3, s / 10, c, 0);
        break;
    }
    case GLYPH_REPEAT:
    case GLYPH_REPEAT1: {
        HPEN pn = ui_pen(c, (s > 24 ? 2 : 1));
        HPEN op = (HPEN)SelectObject(dc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        /* graceful rectangle loop with a gap at the top right */
        MoveToEx(dc, x + s * 24 / 100, y + s * 30 / 100, NULL);
        LineTo(dc, x + s * 72 / 100, y + s * 30 / 100);
        MoveToEx(dc, x + s * 80 / 100, y + s * 38 / 100, NULL);
        LineTo(dc, x + s * 80 / 100, y + s * 70 / 100);
        MoveToEx(dc, x + s * 76 / 100, y + s * 70 / 100, NULL);
        LineTo(dc, x + s * 22 / 100, y + s * 70 / 100);
        MoveToEx(dc, x + s * 18 / 100, y + s * 62 / 100, NULL);
        LineTo(dc, x + s * 18 / 100, y + s * 38 / 100);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        arrow_head(dc, x + s * 76 / 100, y + s * 30 / 100, s / 7, c, 0);
        arrow_head(dc, x + s * 22 / 100, y + s * 70 / 100, s / 7, c, 2);
        if (glyph == GLYPH_REPEAT1) {
            ui_fill(dc, x + s * 47 / 100, y + s * 40 / 100, s * 7 / 100, s * 20 / 100, c);
            ui_fill(dc, x + s * 38 / 100, y + s * 60 / 100, s * 24 / 100, s * 8 / 100, c);
        }
        break;
    }
    case GLYPH_VOL:
    case GLYPH_VOL_MUTE: {
        POINT p[6];
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        HPEN op = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        p[0].x = x + s * 8 / 100;  p[0].y = y + s * 36 / 100;
        p[1].x = x + s * 28 / 100; p[1].y = y + s * 36 / 100;
        p[2].x = x + s * 50 / 100; p[2].y = y + s * 14 / 100;
        p[3].x = x + s * 50 / 100; p[3].y = y + s * 86 / 100;
        p[4].x = x + s * 28 / 100; p[4].y = y + s * 64 / 100;
        p[5].x = x + s * 8 / 100;  p[5].y = y + s * 64 / 100;
        Polygon(dc, p, 6);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        if (glyph == GLYPH_VOL) {
            HPEN pn = ui_pen(c, (s > 20 ? 2 : 1));
            HPEN po = (HPEN)SelectObject(dc, pn);
            HBRUSH bo = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
            Arc(dc, x + s * 46 / 100, y + s * 32 / 100, x + s * 74 / 100, y + s * 68 / 100,
                x + s * 74 / 100, y + s * 32 / 100, x + s * 74 / 100, y + s * 68 / 100);
            Arc(dc, x + s * 52 / 100, y + s * 18 / 100, x + s * 92 / 100, y + s * 82 / 100,
                x + s * 92 / 100, y + s * 18 / 100, x + s * 92 / 100, y + s * 82 / 100);
            SelectObject(dc, bo);
            SelectObject(dc, po);
        } else {
            HPEN pn = ui_pen(c, (s > 24 ? 2 : 1));
            HPEN op = (HPEN)SelectObject(dc, pn);
            MoveToEx(dc, x + s * 58 / 100, y + s * 40 / 100, NULL);
            LineTo(dc, x + s * 82 / 100, y + s * 62 / 100);
            MoveToEx(dc, x + s * 82 / 100, y + s * 40 / 100, NULL);
            LineTo(dc, x + s * 58 / 100, y + s * 62 / 100);
            SelectObject(dc, op);
        }
        break;
    }
    case GLYPH_SEARCH: {
        HPEN pn = ui_pen(c, (s > 22 ? 2 : 1));
        HPEN op = (HPEN)SelectObject(dc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, x + s * 16 / 100, y + s * 14 / 100, x + s * 68 / 100, y + s * 66 / 100);
        MoveToEx(dc, x + s * 62 / 100, y + s * 62 / 100, NULL);
        LineTo(dc, x + s * 84 / 100, y + s * 84 / 100);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        break;
    }
    case GLYPH_PLUS:
        ui_fill(dc, x + s * 44 / 100, y + s * 20 / 100, s * 12 / 100, s * 60 / 100, c);
        ui_fill(dc, x + s * 20 / 100, y + s * 44 / 100, s * 60 / 100, s * 12 / 100, c);
        break;
    case GLYPH_FOLDER: {
        POINT p[6];
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        int h = s / 2;
        p[0].x = x + s * 12 / 100; p[0].y = y + s * 30 / 100;
        p[1].x = x + s * 42 / 100; p[1].y = y + s * 30 / 100;
        p[2].x = x + s * 50 / 100; p[2].y = y + s * 22 / 100;
        p[3].x = x + s * 88 / 100; p[3].y = y + s * 22 / 100;
        p[4].x = x + s * 88 / 100; p[4].y = y + s * 30 / 100 + h;
        p[5].x = x + s * 12 / 100; p[5].y = y + s * 30 / 100 + h;
        Polygon(dc, p, 6);
        SelectObject(dc, ob);
        break;
    }
    case GLYPH_NOTE:
    case GLYPH_MUSIC: {
        HBRUSH b = ui_brush(c);
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        Ellipse(dc, x + s * 18 / 100, y + s * 58 / 100, x + s * 46 / 100, y + s * 78 / 100);
        SelectObject(dc, ob);
        ui_fill(dc, x + s * 42 / 100, y + s * 20 / 100, s * 8 / 100, s * 50 / 100, c);
        ui_line(dc, x + s * 50 / 100, y + s * 20 / 100, x + s * 68 / 100, y + s * 26 / 100, c);
        break;
    }
    case GLYPH_TRASH: {
        ui_round(dc, x + s * 24 / 100, y + s * 26 / 100, s * 52 / 100, s * 56 / 100, s / 12, CLR_INVALID, c);
        ui_fill(dc, x + s * 18 / 100, y + s * 20 / 100, s * 64 / 100, s / 10, c);
        ui_fill(dc, x + s * 40 / 100, y + s * 12 / 100, s * 20 / 100, s / 12, c);
        break;
    }
    default:
        break;
    }
}

void ui_eq(HDC dc, int x, int y, int size, COLORREF c, int phase)
{
    static const int pat[4][3] = {
        { 3, 8, 5 }, { 6, 4, 9 }, { 8, 6, 3 }, { 4, 9, 6 }
    };
    int i;
    int bar = size / 5;
    if (bar < 2)
        bar = 2;
    for (i = 0; i < 3; i++) {
        int h = size * pat[(phase / 2 + i) % 4][i] / 10;
        if (h < 2)
            h = 2;
        if (h > size)
            h = size;
        ui_fill(dc, x + i * (bar + bar / 2), y + size - h, bar, h, c);
    }
}

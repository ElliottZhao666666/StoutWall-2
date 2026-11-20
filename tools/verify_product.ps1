#requires -Version 7.0
<#
.SYNOPSIS
Verifies the local Win32 product after building, using an isolated test gallery.
.EXAMPLE
./tools/verify_product.ps1 -GalleryFixture ./build/tests/isolated-current-20261121-01/Gallery
.NOTES
Run from the interactive Windows desktop in PowerShell 7. Stage a pre-extracted
test gallery under build/tests; only the index and dated DDT/EDT pairs are copied.
Use -TempParent if the Windows temp path is not ASCII. Each run leaves its fresh
data root for inspection. Exit code 0 means all automated checks passed; 1 means
failure, including missing prerequisites. A running legacy instance is left alone.

The script checks child windows on reopen, disabled seven-day entries, gallery preview
click notifications, both theme package formats and same-name refusal. Use a
fixture with at least two dated pairs for theme export. Packages stay in the
test data root and are never installed. The source, relocation and iOS checks
only open and close their windows.

The tray test sends the shell's restore notification to the same hidden instance.
Check the physical tray icon and large-image rendering separately. Do not operate
startup controls or apply a desktop wallpaper.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $GalleryFixture,
    [string] $TempParent = [IO.Path]::GetTempPath(),
    [ValidateRange(5, 120)] [int] $TimeoutSeconds = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$nativeCode = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace StoutWallVerification {
    public sealed class Session : IDisposable {
        private IntPtr job, process, remote;
        public uint Id { get; private set; }

        [StructLayout(LayoutKind.Sequential)] struct StartupInfo {
            public int Size;
            public IntPtr Reserved, Desktop, Title;
            public int X, Y, Width, Height, XChars, YChars, Fill, Flags;
            public short Show, ReservedSize;
            public IntPtr ReservedPointer, StdIn, StdOut, StdErr;
        }
        [StructLayout(LayoutKind.Sequential)] struct ProcessInfo {
            public IntPtr Process, Thread;
            public uint Pid, Tid;
        }
        [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
            public long ProcessTime, JobTime;
            public uint Flags;
            public UIntPtr MinWorkingSet, MaxWorkingSet;
            public uint ActiveProcesses;
            public UIntPtr Affinity;
            public uint Priority, Scheduling;
        }
        [StructLayout(LayoutKind.Sequential)] struct JobLimits {
            public BasicLimits Basic;
            public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes;
            public UIntPtr ProcessMemory, JobMemory, PeakProcess, PeakJob;
        }
        delegate bool Visit(IntPtr h, IntPtr parameter);

        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateJobObjectW(IntPtr attributes, IntPtr name);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job, int kind, ref JobLimits limits, int size);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)] static extern bool CreateProcessW(
            string exe, StringBuilder command, IntPtr processAttrs, IntPtr threadAttrs, bool inherit,
            uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo info);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr h);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint type, uint protect);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, [Out] byte[] bytes, UIntPtr size, out UIntPtr read);

        [DllImport("user32.dll")] static extern bool EnumWindows(Visit visit, IntPtr state);
        [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, Visit visit, IntPtr state);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
        [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
        [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
        [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h, uint relation);
        [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] static extern int GetClassNameW(IntPtr h, StringBuilder text, int size);
        [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)] static extern int GetWindowTextW(IntPtr h, StringBuilder text, int size);
        [DllImport("user32.dll", ExactSpelling=true, SetLastError=true)] static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
        [DllImport("user32.dll", ExactSpelling=true, SetLastError=true)] static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
        [DllImport("user32.dll", ExactSpelling=true, SetLastError=true)] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll", EntryPoint="SendMessageTimeoutW", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)] static extern IntPtr SendText(IntPtr h, uint m, IntPtr w, StringBuilder l, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll", ExactSpelling=true)] static extern int GetWindowLongW(IntPtr h, int index);
        [DllImport("user32.dll", ExactSpelling=true)] static extern IntPtr GetParent(IntPtr h);

        static Exception Failed(string step) { return new Win32Exception(Marshal.GetLastWin32Error(), step); }

        public Session(string exe, string directory, string dataRoot, string config) {
            job = CreateJobObjectW(IntPtr.Zero, IntPtr.Zero);
            if (job == IntPtr.Zero) throw Failed("CreateJobObjectW");
            ProcessInfo info = new ProcessInfo();
            try {
                JobLimits limits = new JobLimits();
                limits.Basic.Flags = 0x2008; // KILL_ON_JOB_CLOSE | ACTIVE_PROCESS
                // Theme export runs 7za and cabarc in sequence, inside this job.
                limits.Basic.ActiveProcesses = 2;
                if (!SetInformationJobObject(job, 9, ref limits, Marshal.SizeOf(limits))) throw Failed("SetInformationJobObject");
                StartupInfo startup = new StartupInfo();
                startup.Size = Marshal.SizeOf(startup);
                var args = new StringBuilder("\"" + exe + "\" --test-mode --ui-test --data-root \"" + dataRoot + "\" --config \"" + config + "\"");
                // Assign the job before running even the first user-mode instruction.
                if (!CreateProcessW(exe, args, IntPtr.Zero, IntPtr.Zero, false, 4, IntPtr.Zero, directory, ref startup, out info))
                    throw Failed("CreateProcessW (suspended)");
                process = info.Process;
                Id = info.Pid;
                if (!AssignProcessToJobObject(job, process)) throw Failed("AssignProcessToJobObject (test instance left suspended)");
                if (ResumeThread(info.Thread) == uint.MaxValue) throw Failed("ResumeThread");
            } catch {
                Dispose();
                throw;
            } finally {
                if (info.Thread != IntPtr.Zero) CloseHandle(info.Thread);
            }
        }
        public bool Wait(uint milliseconds) {
            uint result = WaitForSingleObject(process, milliseconds);
            if (result != 0 && result != 258) throw Failed("WaitForSingleObject");
            return result == 0;
        }
        public uint ExitCode() {
            uint code;
            if (!GetExitCodeProcess(process, out code)) throw Failed("GetExitCodeProcess");
            return code;
        }
        public void Dispose() {
            if (process != IntPtr.Zero) {
                // The original process handle avoids killing another process after PID reuse.
                if (WaitForSingleObject(process, 0) == 258) TerminateProcess(process, 1);
                WaitForSingleObject(process, 3000);
                CloseHandle(process);
                process = IntPtr.Zero;
            }
            if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
        }
        bool Owns(IntPtr h) {
            uint pid;
            return h != IntPtr.Zero && GetWindowThreadProcessId(h, out pid) != 0 && pid == Id && !Wait(0);
        }
        void Check(IntPtr h) {
            if (!Owns(h)) throw new InvalidOperationException("Window does not belong to the live test PID");
        }
        public IntPtr[] Windows(IntPtr parent) {
            var windows = new List<IntPtr>();
            Visit visit = (h, unused) => {
                uint pid;
                if (GetWindowThreadProcessId(h, out pid) != 0 && pid == Id) windows.Add(h);
                return true;
            };
            if (parent == IntPtr.Zero) EnumWindows(visit, IntPtr.Zero);
            else { Check(parent); EnumChildWindows(parent, visit, IntPtr.Zero); }
            return windows.ToArray();
        }
        public bool Visible(IntPtr h) { return Owns(h) && IsWindowVisible(h); }
        public bool Enabled(IntPtr h) { return Owns(h) && IsWindowEnabled(h); }
        public bool Alive(IntPtr h) { return Owns(h); }
        public int ControlId(IntPtr h) { Check(h); return GetDlgCtrlID(h); }
        public IntPtr Owner(IntPtr h) { Check(h); return GetWindow(h, 4); } // GW_OWNER
        public string Class(IntPtr h) {
            Check(h);
            var text = new StringBuilder(128);
            GetClassNameW(h, text, text.Capacity);
            return text.ToString();
        }
        public string Title(IntPtr h) {
            Check(h);
            var text = new StringBuilder(256);
            GetWindowTextW(h, text, text.Capacity);
            return text.ToString();
        }
        long Send(IntPtr h, uint message, int arg, IntPtr data) {
            Check(h);
            IntPtr result;
            if (SendMessageTimeoutW(h, message, new IntPtr(arg), data, 0x23, 2000, out result) == IntPtr.Zero)
                throw Failed("SendMessageTimeoutW: " + message);
            return result.ToInt64();
        }
        string TextMessage(IntPtr h, uint message, int arg) {
            Check(h);
            var text = new StringBuilder(4096);
            IntPtr result;
            if (SendText(h, message, new IntPtr(arg), text, 0x23, 2000, out result) == IntPtr.Zero)
                throw Failed("SendMessageTimeoutW (text): " + message);
            return text.ToString();
        }
        public string Text(IntPtr h) { return TextMessage(h, 0x000D, 4096); }
        public int CheckedState(IntPtr button) { return (int)Send(button, 0x00F0, 0, IntPtr.Zero); } // BM_GETCHECK
        public bool IsImageControl(IntPtr h) {
            return Class(h).Equals("Static", StringComparison.OrdinalIgnoreCase) &&
                (GetWindowLongW(h, -16) & 0x11F) == 0x10D; // SS_OWNERDRAW | SS_NOTIFY
        }
        public void ClickPreview(IntPtr preview) {
            if (!Visible(preview) || !Enabled(preview) || !IsImageControl(preview))
                throw new InvalidOperationException("Expected visible preview control");
            // Deliver the same STN_CLICKED notification as the Static control.
            Post(GetParent(preview), 0x0111, ControlId(preview), preview); // STN_CLICKED = 0
        }
        public void SetEdit(IntPtr edit, string value) {
            if (!Visible(edit) || !Enabled(edit) || !Class(edit).Equals("Edit", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Expected visible edit control");
            IntPtr result;
            if (SendText(edit, 0x000C, IntPtr.Zero, new StringBuilder(value), 0x23, 2000, out result) == IntPtr.Zero)
                throw Failed("WM_SETTEXT");
            if (result == IntPtr.Zero || Text(edit) != value) throw new InvalidOperationException("Theme edit did not take effect");
        }
        public int ComboItemIndex(IntPtr combo, string label) {
            if (!Class(combo).Equals("ComboBox", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Expected combo box");
            IntPtr result;
            if (SendText(combo, 0x0158, new IntPtr(-1), new StringBuilder(label), 0x23, 2000, out result) == IntPtr.Zero)
                throw Failed("CB_FINDSTRINGEXACT");
            return unchecked((int)result.ToInt64());
        }
        public void SelectComboItem(IntPtr combo, string label) {
            if (!Visible(combo) || !Enabled(combo)) throw new InvalidOperationException("Combo box is not available");
            int index = ComboItemIndex(combo, label);
            if (index < 0 || Send(combo, 0x014E, index, IntPtr.Zero) != index)
                throw new InvalidOperationException("Theme option unavailable: " + label);
            Send(GetParent(combo), 0x0111, (1 << 16) | ControlId(combo), combo); // CBN_SELCHANGE
        }
        public int RowCount(IntPtr list) { return (int)Send(list, 0x018B, 0, IntPtr.Zero); }
        public int SelectedRow(IntPtr list) { return (int)Send(list, 0x0188, 0, IntPtr.Zero); }
        public string RowText(IntPtr list, int row) {
            long size = Send(list, 0x018A, row, IntPtr.Zero); // LB_GETTEXTLEN
            if (size < 0) return ""; // The worker may be refreshing the list.
            if (size >= 4096) throw new InvalidOperationException("Unexpected gallery row length");
            return TextMessage(list, 0x0189, row);
        }
        void Post(IntPtr h, uint message, int arg, IntPtr data) {
            Check(h);
            if (!PostMessageW(h, message, new IntPtr(arg), data)) throw Failed("PostMessageW: " + message);
        }
        public void Click(IntPtr h, string caption) {
            if (!Visible(h) || !Enabled(h) || !Class(h).Equals("Button", StringComparison.OrdinalIgnoreCase) || Text(h) != caption)
                throw new InvalidOperationException("Expected visible button: " + caption);
            Post(h, 0x00F5, 0, IntPtr.Zero); // BM_CLICK; asynchronous for modal dialogs
        }
        public void ConfirmYes(IntPtr dialog, IntPtr button) {
            if (Class(dialog) != "#32770" || !Visible(button) || !Enabled(button) || ControlId(button) != 6)
                throw new InvalidOperationException("Exit dialog Yes button not available");
            Post(dialog, 0x0111, 6, button); // WM_COMMAND, IDYES, BN_CLICKED
        }
        public void CloseThemeResult(IntPtr dialog, IntPtr theme, string title) {
            if (Class(dialog) != "#32770" || Owner(dialog) != theme || Title(dialog) != title || !Visible(dialog))
                throw new InvalidOperationException("Expected the theme export result");
            Post(dialog, 0x0010, 0, IntPtr.Zero); // WM_CLOSE
        }
        public void CloseChild(IntPtr child, IntPtr main) {
            if (child == main || Class(child) != "StoutWallOriginalWindow") throw new InvalidOperationException("Not a product child window");
            Post(child, 0x0010, 0, IntPtr.Zero); // WM_CLOSE
        }
        public void HideMainToTray(IntPtr main) {
            if (!Visible(main) || Owner(main) != IntPtr.Zero || Class(main) != "StoutWallOriginalWindow")
                throw new InvalidOperationException("Expected visible product main window");
            Post(main, 0x0010, 0, IntPtr.Zero); // WM_CLOSE
        }
        public void SimulateTrayRestore(IntPtr main) {
            if (Visible(main) || Owner(main) != IntPtr.Zero || Class(main) != "StoutWallOriginalWindow")
                throw new InvalidOperationException("Expected hidden product main window");
            Post(main, 0x8000 + 31, 1, new IntPtr(0x0202)); // WM_LBUTTONUP on tray icon 1
        }

        // Tab pointer messages are not marshalled. The product is x86, so write
        // the x86 TCITEMW layout in the test process, regardless of the host bitness.
        IntPtr TabBuffer() {
            if (remote == IntPtr.Zero) {
                remote = VirtualAllocEx(process, IntPtr.Zero, new UIntPtr(1024), 0x3000, 4);
                if (remote == IntPtr.Zero) throw Failed("VirtualAllocEx (tab buffer)");
            }
            return remote;
        }
        byte[] ReadBuffer(IntPtr address, int count) {
            var bytes = new byte[count];
            UIntPtr size = new UIntPtr((uint)count), read;
            if (!ReadProcessMemory(process, address, bytes, size, out read) || read != size) throw Failed("ReadProcessMemory (tab data)");
            return bytes;
        }
        public int TabCount(IntPtr tab) { return (int)Send(tab, 0x1304, 0, IntPtr.Zero); }
        public int SelectedTab(IntPtr tab) { return (int)Send(tab, 0x130B, 0, IntPtr.Zero); }
        public string TabText(IntPtr tab, int index) {
            var bytes = new byte[1024];
            uint bufferAddress = unchecked((uint)IntPtr.Add(TabBuffer(), 32).ToInt64());
            Buffer.BlockCopy(BitConverter.GetBytes(1), 0, bytes, 0, 4); // TCIF_TEXT
            Buffer.BlockCopy(BitConverter.GetBytes(bufferAddress), 0, bytes, 12, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(256), 0, bytes, 16, 4);
            UIntPtr size = new UIntPtr((uint)bytes.Length), written;
            if (!WriteProcessMemory(process, TabBuffer(), bytes, size, out written) || written != size) throw Failed("WriteProcessMemory (TCITEMW)");
            if (Send(tab, 0x133C, index, TabBuffer()) == 0) throw new InvalidOperationException("TCM_GETITEMW failed");
            uint text = BitConverter.ToUInt32(ReadBuffer(TabBuffer(), 24), 12);
            if (text == 0) return "";
            if (text == bufferAddress)
                return Encoding.Unicode.GetString(ReadBuffer(IntPtr.Add(TabBuffer(), 32), 512)).Split('\0')[0];
            // The tab can return a pointer to its own string instead of copying.
            IntPtr address = IntPtr.Size == 8 ? new IntPtr((long)text) : new IntPtr(unchecked((int)text));
            var title = new StringBuilder();
            for (int i = 0; i < 256; ++i) {
                char c = (char)BitConverter.ToUInt16(ReadBuffer(IntPtr.Add(address, i * 2), 2), 0);
                if (c == '\0') return title.ToString();
                title.Append(c);
            }
            throw new InvalidOperationException("Unterminated tab title");
        }
        public void SelectTab(IntPtr tab, int index) {
            if (!Visible(tab) || !Enabled(tab) || index < 0 || index >= TabCount(tab))
                throw new InvalidOperationException("Tab is not available");
            IntPtr parent = GetParent(tab);
            Check(parent);
            SendMessageW(parent, 0x8000 + 0x51, new IntPtr(index), IntPtr.Zero);
            if (SelectedTab(tab) != index) throw new InvalidOperationException("UI test tab selection did not take effect");
        }
    }
}
'@

function Assert-That([bool] $Condition, [string] $Message) {
    if (!$Condition) { throw $Message }
}

function Wait-For([string] $Description, [scriptblock] $Check) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ($session.Wait(0)) {
            throw "PID $($session.Id) exited with code $($session.ExitCode()) while waiting for $Description."
        }
        $value = & $Check
        if ($value) { return $value }
        Start-Sleep -Milliseconds 100
    } while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds)
    throw "Timeout waiting for $Description (PID $($session.Id))."
}

function Get-TestWindow([string] $Title, [string] $Class = 'StoutWallOriginalWindow', [switch] $Gallery) {
    foreach ($window in $session.Windows([IntPtr]::Zero)) {
        if (!$session.Visible($window) -or $session.Class($window) -ne $Class) { continue }
        if ($Gallery) {
            if ((Find-Control $window ListBox -Visible) -and (Find-Control $window Button '刷新' -Visible)) { $window }
        } elseif ($session.Title($window) -ceq $Title) {
            $window
        }
    }
}

function Find-Control([IntPtr] $Parent, [string] $Class, [string] $Caption = '', [switch] $Visible) {
    $controls = @($session.Windows($Parent) | Where-Object {
        $session.Class($_) -eq $Class -and (!$Visible -or $session.Visible($_)) -and
            (!$Caption -or $session.Text($_) -ceq $Caption)
    })
    Assert-That ($controls.Count -le 1) "Ambiguous $Class '$Caption' under test window $Parent."
    if ($controls.Count -eq 1) { $controls[0] }
}

function Click-Allowed([IntPtr] $Parent, [string] $Caption) {
    $allowed = @('选择壁纸源...', '壁纸库位置...', '查看/管理壁纸库', '导出为Windows 主题包',
        '获取 iOS 版', '退出软件', '收藏', '取消收藏', '手动选择', '…', '整个壁纸库', '开始导出',
        'Windows 7/Vista (*.themepack)', 'Windows 11/10/8.1/8 (*.deskthemepack)',
        '透明效果', '自动主题色', '深色模式 (Win11/10)', '乱序更换')
    Assert-That ($Caption -cin $allowed) "Button outside the acceptance allowlist: $Caption"
    $button = Wait-For "enabled button '$Caption'" {
        $found = Find-Control $Parent Button $Caption -Visible
        if ($found -and $session.Enabled($found)) { $found }
    }
    $session.Click($button, $Caption)
}

function Get-PageMarkers([IntPtr] $Tab) {
    $preview = @($session.Windows($Tab) | Where-Object {
        $session.Class($_) -eq 'Static' -and $session.ControlId($_) -eq $currentPreviewId
    })
    Assert-That ($preview.Count -eq 1) 'Current wallpaper preview control not found.'
    $result = @($preview[0], (Find-Control $Tab Button '选择运行模式'), (Find-Control $Tab Button '退出软件'),
        (Find-Control $Tab Button '查看/管理壁纸库'), (Find-Control $Tab Button '获取 iOS 版'))
    Assert-That (@($result | Where-Object { $_ }).Count -eq 5) 'One or more page markers are missing.'
    return $result
}

function Test-Page([int] $Page) {
    if ($session.SelectedTab($tab) -ne $Page) { return $false }
    for ($index = 0; $index -lt $markers.Count; $index++) {
        if ($session.Visible($markers[$index]) -ne ($index -eq $Page)) { return $false }
    }
    return $true
}

function Select-Page([int] $Page) {
    if ($session.SelectedTab($tab) -ne $Page) { $session.SelectTab($tab, $Page) }
    $null = Wait-For "page '$($tabs[$Page])' and its control visibility" { Test-Page $Page }
}

function Open-Child([string] $Button, [string] $Title = '', [switch] $Gallery) {
    $existing = @(Get-TestWindow $Title -Gallery:$Gallery | Where-Object { $_ -ne $main })
    Assert-That ($existing.Count -eq 0) "Child window for '$Button' was already open."
    Click-Allowed $main $Button
    return Wait-For "child window '$Button'" {
        $found = @(Get-TestWindow $Title -Gallery:$Gallery | Where-Object { $_ -ne $main })
        Assert-That ($found.Count -le 1) "Multiple windows for '$Button'."
        if ($found.Count -eq 1) {
            Assert-That ($session.Owner($found[0]) -eq $main -and !$session.Enabled($main)) "'$Button' did not block its owner."
            $found[0]
        }
    }
}

function Close-Child([IntPtr] $Child, [string] $Name) {
    $owner = $session.Owner($Child)
    $session.CloseChild($Child, $main)
    $null = Wait-For "closing '$Name'" { !$session.Alive($Child) }
    Assert-That ($owner -and $session.Visible($owner) -and $session.Enabled($owner)) "Owner was not restored after closing '$Name'."
}

function Test-ChildReopen([string] $Button, [string] $Title, [string] $MarkerClass, [string] $MarkerCaption = '') {
    for ($opening = 1; $opening -le 2; $opening++) {
        $child = Open-Child $Button $Title
        $null = Wait-For "'$Title' control on opening $opening" { Find-Control $child $MarkerClass $MarkerCaption -Visible }
        Close-Child $child $Title
    }
    Write-Host "PASS: '$Title' closes and reopens."
}

function Test-SevenDayUnavailable {
    Select-Page 0
    $button = Find-Control $main Button '七日壁纸'
    Assert-That ($button -and (!$session.Visible($button) -or !$session.Enabled($button))) 'Seven-day view entry became available.'
    Select-Page 1
    $radio = Find-Control $main Button '7日壁纸模式' -Visible
    Assert-That ($radio -and !$session.Enabled($radio) -and $session.CheckedState($radio) -eq 0) 'Seven-day mode must be disabled and unchecked.'
    Write-Host 'PASS: Disabled seven-day mode and unavailable seven-day view entry.'
}

function Test-GalleryViewer([IntPtr] $Gallery) {
    $preview = Find-Control $Gallery Static -Visible
    Assert-That ([bool] $preview) 'Gallery preview not found.'
    for ($opening = 1; $opening -le 2; $opening++) {
        Assert-That (@(Get-TestWindow '查看大图').Count -eq 0) 'Viewer was already open before the preview click.'
        $session.ClickPreview($preview)
        $viewer = Wait-For "gallery viewer on opening $opening" {
            $found = @(Get-TestWindow '查看大图')
            Assert-That ($found.Count -le 1) 'Multiple viewer windows.'
            if ($found.Count -eq 1) { $found[0] }
        }
        $null = Wait-For 'viewer image control' { Find-Control $viewer Static -Visible }
        Assert-That ($session.Owner($viewer) -eq $Gallery -and !$session.Enabled($Gallery) -and !$session.Enabled($main)) 'Viewer did not preserve the nested modal chain.'
        Close-Child $viewer '查看大图'
        Assert-That (!$session.Enabled($main)) 'Main window unlocked while the gallery was still open.'
    }
    Write-Host 'PASS: Gallery preview click opens and reopens the viewer.'
}

function Quit-Product {
    Select-Page 2
    Click-Allowed $main '退出软件'
    $dialog = Wait-For 'exit confirmation owned by the main window' {
        $found = @(Get-TestWindow '要退出本软件吗？' '#32770' | Where-Object { $session.Owner($_) -eq $main })
        Assert-That ($found.Count -le 1) 'Multiple exit confirmations.'
        if ($found.Count -eq 1) { $found[0] }
    }
    $body = Find-Control $dialog Static '退出本软件后，您的桌面壁纸将不会更新。' -Visible
    Assert-That ([bool] $body) 'Exit confirmation body does not match.'
    $yes = @($session.Windows($dialog) | Where-Object {
        $session.Class($_) -eq 'Button' -and $session.ControlId($_) -eq 6 -and $session.Visible($_)
    })
    Assert-That ($yes.Count -eq 1) 'Exit confirmation does not have a unique Yes button.'
    $session.ConfirmYes($dialog, $yes[0])
    Assert-That ($session.Wait([uint32]($TimeoutSeconds * 1000))) 'Timed out waiting for normal exit.'
    $code = $session.ExitCode()
    Assert-That ($code -eq 0) "Product exited with code $code."
    Write-Host "PASS: Settings exit and confirmation; PID $($session.Id) exited with code 0."
}

function Read-Gallery([IntPtr] $List) {
    $count = $session.RowCount($List)
    Assert-That ($count -ge 0 -and $count -le 500) "Unexpected gallery row count: $count."
    $rows = [ordered]@{}
    for ($row = 0; $row -lt $count; $row++) {
        $text = $session.RowText($List, $row)
        if ($text -cnotmatch '^([0-9]{8})(\[收藏\])?$') { return }
        $date = $Matches[1]
        Assert-That (!$rows.Contains($date)) "Duplicate gallery date: $date."
        $rows[$date] = $Matches.ContainsKey(2)
    }
    if ($rows.Count) { return $rows }
}

function Open-LoadedGallery {
    $window = Open-Child '查看/管理壁纸库' -Gallery
    $list = Find-Control $window ListBox -Visible
    Assert-That ([bool] $list) 'Gallery list not found.'
    $rows = Wait-For 'dated gallery rows with an initialized selection' {
        if ($session.Enabled($list) -and $session.SelectedRow($list) -ge 0) { Read-Gallery $list }
    }
    Assert-That ($rows.Count -eq $pairs.Count) 'Gallery row count differs from the fixture.'
    foreach ($pair in $pairs) {
        Assert-That ($rows.Contains($pair.Name.Substring(6))) "Fixture date not shown: $($pair.Name)."
    }
    return @{ Window = $window; List = $list; Rows = $rows }
}

function Test-CustomThemeWindow([IntPtr] $Theme) {
    Click-Allowed $Theme '手动选择'
    Assert-That (@(Get-TestWindow '').Count -eq 0) 'Unexpected untitled window before opening custom theme selection.'
    Click-Allowed $Theme '…'
    $picker = Wait-For 'custom theme selection window' {
        $found = @(Get-TestWindow '' | Where-Object {
            (Find-Control $_ ListBox -Visible) -and !(Find-Control $_ Button '刷新' -Visible)
        })
        Assert-That ($found.Count -le 1) 'Multiple custom theme selection windows.'
        if ($found.Count -eq 1) { $found[0] }
    }
    $list = Find-Control $picker ListBox -Visible
    Assert-That ($session.Owner($picker) -eq $Theme -and !$session.Enabled($Theme) -and !$session.Enabled($main)) 'Theme picker did not preserve the nested modal chain.'
    $rows = Wait-For 'custom theme dates with the first row selected' {
        if ($session.RowCount($list) -eq $pairs.Count -and $session.SelectedRow($list) -eq 0) { Read-Gallery $list }
    }
    foreach ($pair in $pairs) {
        Assert-That ($rows.Contains($pair.Name.Substring(6))) "Fixture date missing from custom selection: $($pair.Name)."
    }
    Close-Child $picker '手动选择'
    Assert-That (!$session.Enabled($main)) 'Main window unlocked while the theme form was still open.'
    Write-Host 'PASS: Custom theme picker opens and shows the isolated gallery dates.'
}

function Set-ThemeCheck([IntPtr] $Theme, [string] $Caption, [bool] $Checked) {
    $button = Find-Control $Theme Button $Caption -Visible
    Assert-That ($button -and $session.Enabled($button)) "Theme option '$Caption' is unavailable."
    if ($session.CheckedState($button) -ne [int] $Checked) { Click-Allowed $Theme $Caption }
    $null = Wait-For "theme option '$Caption' checked=$Checked" { $session.CheckedState($button) -eq [int] $Checked }
}

function Set-ThemeCombo([IntPtr] $Theme, [string] $Option) {
    $controls = @($session.Windows($Theme) | Where-Object {
        $session.Class($_) -eq 'ComboBox' -and $session.Visible($_) -and $session.ComboItemIndex($_, $Option) -ge 0
    })
    Assert-That ($controls.Count -eq 1) "Expected one theme combo with '$Option'."
    $session.SelectComboItem($controls[0], $Option)
}

function Read-ThemeDialog([IntPtr] $Theme) {
    $dialog = Wait-For 'theme export result' {
        $found = @($session.Windows([IntPtr]::Zero) | Where-Object {
            $session.Visible($_) -and $session.Class($_) -eq '#32770' -and
                $session.Owner($_) -eq $Theme -and $session.Title($_) -eq '导出主题包'
        })
        Assert-That ($found.Count -le 1) 'Multiple theme export result dialogs.'
        if ($found.Count -eq 1) { $found[0] }
    }
    $body = @($session.Windows($dialog) | Where-Object { $session.Class($_) -eq 'Static' -and $session.Visible($_) } |
        ForEach-Object { $session.Text($_) } | Where-Object { $_ }) -join ' '
    $session.CloseThemeResult($dialog, $Theme, '导出主题包')
    $null = Wait-For 'theme result dismissed' { !$session.Alive($dialog) }
    return $body
}

function Read-ThemePackage([string] $Path, [string] $Inspection) {
    Assert-That (Test-Path -LiteralPath $Path -PathType Leaf) "Missing export: $Path"
    $expand = Join-Path ([Environment]::SystemDirectory) 'expand.exe'
    $listing = @(& $expand -D $Path)
    Assert-That ($LASTEXITCODE -eq 0) "Cannot list theme cabinet: $Path"
    $images = @(foreach ($line in $listing) {
        if ($line -match '(?<![A-Za-z0-9_])(img[0-9]+\.jpg)\s*$') { $Matches[1] }
    })
    $null = New-Item -ItemType Directory -Path $Inspection
    $null = & $expand '-F:main.theme' $Path $Inspection
    Assert-That ($LASTEXITCODE -eq 0) "Cannot extract main.theme from $Path."
    $themeFiles = @(Get-ChildItem -LiteralPath $Inspection -Recurse -File -Filter 'main.theme')
    Assert-That ($themeFiles.Count -eq 1) "Expected one main.theme in $Path."
    $sections = @{}
    $section = $null
    foreach ($line in (Get-Content -LiteralPath $themeFiles[0].FullName -Encoding UTF8)) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            $section = $Matches[1]
            $sections[$section] = @{}
        } elseif ($section -and $line -match '^\s*([^;#=]+?)\s*=\s*(.*?)\s*$') {
            $sections[$section][$Matches[1]] = $Matches[2]
        }
    }
    return @{ Images = $images; Sections = $sections }
}

function Assert-ThemePackage([string] $Path, [bool] $Modern, [string] $Name, [string] $OutputDir) {
    $package = Read-ThemePackage $Path (Join-Path $OutputDir "$Name-inspect")
    $expectedImages = @(for ($i = 0; $i -lt $pairs.Count; $i++) { "img$i.jpg" })
    Assert-That ((($package.Images | Sort-Object) -join ',') -ceq (($expectedImages | Sort-Object) -join ',')) "Wrong wallpaper entries in $Path."
    $sections = $package.Sections
    $values = @{
        Theme = @{ DisplayName = $Name; BrandImage = 'brand.png' }
        Slideshow = @{ ImagesRootPath = 'DesktopBackground'; Interval = '600000'; Shuffle = '1' }
        'Control Panel\Desktop' = @{ TileWallpaper = '0'; WallpaperStyle = '6' }
        MasterThemeSelector = @{ MTSM = $(if ($Modern) { 'RJSPBS' } else { 'DABJDKT' }) }
        VisualStyles = $(if ($Modern) { @{ AppMode = 'Dark'; SystemMode = 'Dark'; AutoColorization = '1' } }
            else { @{ Transparency = '0'; ColorizationColor = '0X6B74B8FC' } })
    }
    foreach ($group in $values.Keys) {
        Assert-That ($sections.ContainsKey($group)) "Missing theme section [$group] in $Path."
        foreach ($key in $values[$group].Keys) {
            Assert-That ($sections[$group][$key] -eq $values[$group][$key]) "Unexpected [$group] $key in $Path."
        }
    }
}

function Test-ThemeExport([IntPtr] $Theme, [bool] $Modern, [string] $OutputDir) {
    $kind = if ($Modern) { 'Windows 11/10/8.1/8 (*.deskthemepack)' } else { 'Windows 7/Vista (*.themepack)' }
    $name = if ($Modern) { 'ui-modern' } else { 'ui-win7' }
    $extension = if ($Modern) { '.deskthemepack' } else { '.themepack' }
    $path = Join-Path $OutputDir ($name + $extension)
    # ReplicaUi creates the name and directory edits in layout order.
    $edits = @($session.Windows($Theme) | Where-Object { $session.Class($_) -eq 'Edit' -and $session.Visible($_) } |
        Sort-Object { $session.ControlId($_) })
    Assert-That ($edits.Count -eq 2) 'Expected the theme name and directory edits.'
    $session.SetEdit($edits[0], $name)
    $session.SetEdit($edits[1], $OutputDir)

    Set-ThemeCheck $Theme '整个壁纸库' $true
    Click-Allowed $Theme $kind
    $kindRadio = Find-Control $Theme Button $kind -Visible
    $null = Wait-For "theme kind '$kind' selected" { $session.CheckedState($kindRadio) -eq 1 }
    $scopeRadio = Find-Control $Theme Button '整个壁纸库' -Visible
    Assert-That ($session.CheckedState($scopeRadio) -eq 1) 'Whole-gallery theme scope was lost on switching format.'
    Set-ThemeCombo $Theme '10分钟'
    Set-ThemeCombo $Theme '适应'
    Set-ThemeCombo $Theme '(16:9)1920x1080'
    Set-ThemeCheck $Theme '乱序更换' $true
    if ($Modern) {
        Set-ThemeCheck $Theme '自动主题色' $true
        Set-ThemeCheck $Theme '深色模式 (Win11/10)' $true
    } else {
        Set-ThemeCheck $Theme '透明效果' $false
        Set-ThemeCombo $Theme 'Win7默认蓝(天空)'
    }

    Assert-That ($session.Text($edits[0]) -ceq $name -and $session.Text($edits[1]) -ceq $OutputDir) 'Theme output changed before export.'
    Click-Allowed $Theme '开始导出'
    $body = Read-ThemeDialog $Theme
    Assert-That ($body.IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0 -and
        $body -match 'exported|导出(?:成功|完成)') "Unexpected theme export result: $body"
    Assert-ThemePackage $path $Modern $name $OutputDir
    $originalHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $originalWrite = (Get-Item -LiteralPath $path).LastWriteTimeUtc
    Write-Host "PASS: UI exported and verified $extension ($path)."

    Assert-That ($session.Text($edits[0]) -ceq $name -and $session.Text($edits[1]) -ceq $OutputDir) 'Theme output changed before repeating export.'
    Click-Allowed $Theme '开始导出'
    $body = Read-ThemeDialog $Theme
    Assert-That ($body -match 'Win32 error (80|183)[)]|同名|已经?存在') "Expected a same-name error, got: $body"
    Assert-That ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ceq $originalHash -and
        (Get-Item -LiteralPath $path).LastWriteTimeUtc -eq $originalWrite) "Same-name export changed $path."
    Write-Host "PASS: Same-name export refused and original $extension preserved."
}

function Get-OrdinaryItem([string] $Path, [switch] $Temp) {
    $item = Get-Item -LiteralPath $Path -Force
    Assert-That ($item -is [IO.FileSystemInfo]) "Not a filesystem path: $Path"
    $cursor = $item
    while ($null -ne $cursor) {
        # The checkout is in a synced folder. Its cloud placeholders are valid
        # inputs; a junction/symlink may redirect outside the selected tree.
        Assert-That (!$cursor.LinkTarget) "Redirected path rejected: $($cursor.FullName)"
        if ($Temp) {
            Assert-That (!($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint)) "Temp path contains a reparse point: $($cursor.FullName)"
        }
        $cursor = if ($cursor -is [IO.DirectoryInfo]) { $cursor.Parent } else { $cursor.Directory }
    }
    return $item
}

function Wait-ForGalleryLoad {
    $null = Wait-For 'isolated gallery loaded and configuration saved' {
        if (!(Test-Path -LiteralPath $config -PathType Leaf)) { return $false }
        # WM_GETTEXT works for this read-only Edit even while page 3 is hidden.
        foreach ($control in $session.Windows($tab)) {
            if ($session.Class($control) -ne 'Edit') { continue }
            $text = $session.Text($control)
            if ($text.Contains($galleryPath) -and $text -match '壁纸数量：\s*[1-9][0-9]*') { return $true }
        }
        return $false
    }
}

function Assert-Win32Build([string] $Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        Assert-That ($reader.BaseStream.Length -ge 64 -and $reader.ReadUInt16() -eq 0x5A4D) 'Expected an MZ executable.'
        $reader.BaseStream.Position = 0x3C
        $offset = $reader.ReadInt32()
        Assert-That ($offset -ge 64 -and $offset -le $reader.BaseStream.Length - 6) 'Invalid PE header offset.'
        $reader.BaseStream.Position = $offset
        Assert-That ($reader.ReadUInt32() -eq 0x4550 -and $reader.ReadUInt16() -eq 0x014C) 'Expected a Win32 x86 executable.'
    } finally {
        $reader.Dispose()
    }
}

$tabs = @('当前壁纸', '运行模式', '设置', '壁纸库', '更多内容')
$dataRoot = $null
$status = 1
try {
    Assert-That ($IsWindows -and [Environment]::UserInteractive -and
        [Diagnostics.Process]::GetCurrentProcess().SessionId -ne 0) 'Run from an interactive Windows desktop.'
    $repo = Split-Path -Parent $PSScriptRoot
    $product = Get-OrdinaryItem (Join-Path $repo 'build/win32/StoutWall.exe')
    Assert-That ($product -is [IO.FileInfo]) 'The Win32 build is missing.'
    Assert-Win32Build $product.FullName
    $fixture = Get-OrdinaryItem $GalleryFixture
    $fixtureBase = (Join-Path $repo 'build/tests').TrimEnd('\') + '\'
    Assert-That ($fixture -is [IO.DirectoryInfo] -and $fixture.FullName.StartsWith($fixtureBase, [StringComparison]::OrdinalIgnoreCase)) 'Stage a pre-extracted test gallery inside build/tests and pass it as -GalleryFixture.'
    $indexFile = Get-OrdinaryItem (Join-Path $fixture.FullName 'Stout_WallGallery_Index.DDT')
    Assert-That ($indexFile -is [IO.FileInfo] -and $indexFile.Length -gt 0) 'The pre-extracted gallery index is missing or empty.'
    $payload = @(Get-ChildItem -LiteralPath $fixture.FullName -File -Force | Where-Object { $_.Name -match '^Image_[0-9]{8}\.(DDT|EDT)$' })
    $pairs = @($payload | Group-Object BaseName)
    Assert-That ($pairs.Count -ge 1 -and $pairs.Count -le 500) 'The fixture needs 1 to 500 dated wallpaper pairs.'
    if ($pairs.Count -ge 2) {
        foreach ($dependency in @('ForTheme.EDT', '7za.exe', 'cabarc.exe')) {
            $helper = Get-OrdinaryItem (Join-Path $product.DirectoryName $dependency)
            Assert-That ($helper -is [IO.FileInfo] -and $helper.Length -gt 0) "Theme export dependency missing: $dependency"
        }
    }
    foreach ($pair in $pairs) {
        $extensions = @($pair.Group.Extension | Sort-Object -Unique)
        Assert-That ($pair.Count -eq 2 -and $extensions.Count -eq 2) "Incomplete fixture pair: $($pair.Name)."
    }
    foreach ($file in $payload) {
        Assert-That ($file.Length -gt 0 -and !$file.LinkTarget) "Invalid fixture file: $($file.Name)."
    }
    $parent = Get-OrdinaryItem $TempParent -Temp
    Assert-That ($parent -is [IO.DirectoryInfo] -and $parent.FullName -cmatch '^[A-Za-z]:\\[\x20-\x7E]*$') 'Select an existing local ASCII path with -TempParent.'
    $candidate = Join-Path $parent.FullName ('stoutwall-verify-' + [Guid]::NewGuid().ToString('N'))
    Assert-That ($candidate.Length -le 180 -and !(Test-Path -LiteralPath $candidate)) 'Unable to allocate a short, fresh ASCII test path.'

    # ReplicaUi assigns IDs from 100, skipping controls without a native class.
    $layout = Get-Content -LiteralPath (Join-Path $repo 'src/OriginalLayout.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $nativeTypes = @('Lib3.2', 'Lib3.3', 'Lib3.4', 'Lib3.5', 'Lib3.6', 'Lib3.7', 'Lib3.8', 'Lib3.9',
        'Lib3.10', 'Lib3.11', 'Lib3.12', 'Lib3.15', 'Lib3.16', 'Lib3.18')
    $mainNames = @($layout.controls | Where-Object { $_.window -ceq '_启动窗口' -and $_.type -in $nativeTypes } | ForEach-Object name)
    $previewOffset = [Array]::IndexOf($mainNames, '图片框EX1')
    Assert-That ($previewOffset -ge 0) 'Current wallpaper preview missing from layout.'
    $currentPreviewId = 100 + $previewOffset

    if (!('StoutWallVerification.Session' -as [type])) { Add-Type -TypeDefinition $nativeCode }
    $dataRoot = (New-Item -ItemType Directory -Path $candidate).FullName
    Write-Host "Isolated data root: $dataRoot"
    $galleryPath = (New-Item -ItemType Directory -Path (Join-Path $dataRoot 'Gallery')).FullName
    foreach ($file in (@($indexFile) + $payload)) {
        Copy-Item -LiteralPath $file.FullName -Destination $galleryPath
    }
    $config = Join-Path $dataRoot 'StoutWall.cfg'
    $expected = $null
    $testedDate = $null

    for ($run = 1; $run -le 2; $run++) {
        $session = $null
        $main = $null
        $markers = @()
        $exitAttempted = $false
        try {
            $session = [StoutWallVerification.Session]::new($product.FullName, $product.DirectoryName, $dataRoot, $config)
            Write-Host "Run ${run}: PID $($session.Id)."
            $main = Wait-For 'main window' {
                $found = @(Get-TestWindow '胖哥每日壁纸')
                Assert-That ($found.Count -le 1) 'Multiple main windows in the test instance.'
                if ($found.Count -eq 1) { $found[0] }
            }
            $tab = Find-Control $main SysTabControl32 -Visible
            Assert-That ([bool] $tab -and $session.TabCount($tab) -eq $tabs.Count) 'Expected five tabs.'
            for ($page = 0; $page -lt $tabs.Count; $page++) {
                $title = $session.TabText($tab, $page)
                Assert-That ($title -ceq $tabs[$page]) "Page $page is '$title'; expected '$($tabs[$page])'."
            }
            $markers = @(Get-PageMarkers $tab)
            Assert-That (Test-Page 0) 'Initial page is not 当前壁纸 or its visibility is incorrect.'
            Write-Host "PASS: Initial tab, titles and order (run $run)."
            Wait-ForGalleryLoad

            if ($run -eq 1) {
                Test-SevenDayUnavailable
                Select-Page 2
                Test-ChildReopen '选择壁纸源...' '选择新壁纸获取来源...' Button '确定'
                Test-ChildReopen '壁纸库位置...' '请指定新的壁纸库位置:' Button '确定修改/迁移'
                Select-Page 3
                $themeOutput = (New-Item -ItemType Directory -Path (Join-Path $dataRoot 'theme-output')).FullName
                foreach ($modern in @($false, $true)) {
                    $theme = Open-Child '导出为Windows 主题包' '导出为Windows主题包'
                    $null = Wait-For 'theme dialog controls' { Find-Control $theme Button '开始导出' -Visible }
                    if (!$modern) { Test-CustomThemeWindow $theme }
                    if ($pairs.Count -ge 2) { Test-ThemeExport $theme $modern $themeOutput }
                    Close-Child $theme '主题包'
                }
                Write-Host 'PASS: Theme window closes and reopens.'
                if ($pairs.Count -lt 2) { Write-Host 'SKIP: Theme export and same-name protection require at least two fixture pairs.' }
                Select-Page 4
                Test-ChildReopen '获取 iOS 版' '胖哥每日壁纸 iOS版' Button '点此查看'
                Select-Page 0
                Write-Host 'PASS: Five-page switching.'
            }

            Select-Page 3
            $opened = Open-LoadedGallery
            $gallery = $opened.Window
            $list = $opened.List
            $rows = $opened.Rows

            if ($run -eq 1) {
                Test-GalleryViewer $gallery
                $selected = $session.SelectedRow($list)
                Assert-That ($selected -ge 0 -and $selected -lt $rows.Count) 'No initial gallery date selected.'
                $testedDate = @($rows.Keys)[$selected]
                $favorite = !$rows[$testedDate]
                $expected = $rows
                $expected[$testedDate] = $favorite
                $before = if ($favorite) { '收藏' } else { '取消收藏' }
                $after = if ($favorite) { '取消收藏' } else { '收藏' }
                Click-Allowed $gallery $before
                $rows = Wait-For "favorite state change for $testedDate" {
                    $updated = Read-Gallery $list
                    $action = Find-Control $gallery Button $after -Visible
                    if ($updated -and $updated.Contains($testedDate) -and $updated[$testedDate] -eq $favorite -and
                        $action -and $session.Enabled($action)) { $updated }
                }
            }
            Assert-That ($rows.Count -eq $expected.Count) 'Gallery row count changed after favorite toggle or restart.'
            foreach ($date in $expected.Keys) {
                Assert-That ($rows.Contains($date) -and $rows[$date] -eq $expected[$date]) "Unexpected favorite state for $date."
            }
            Write-Host "PASS: Gallery favorite state for $testedDate (run $run)."
            Close-Child $gallery '查看/管理壁纸库'
            if ($run -eq 1) {
                $reopened = Open-LoadedGallery
                foreach ($date in $expected.Keys) {
                    Assert-That ($reopened.Rows.Contains($date) -and $reopened.Rows[$date] -eq $expected[$date]) "Unexpected favorite state on reopening the gallery for $date."
                }
                Close-Child $reopened.Window '查看/管理壁纸库'
                Write-Host 'PASS: Gallery closes and reopens with its rows and favorite state.'
                $session.HideMainToTray($main)
                $null = Wait-For 'main window hidden without exiting' { $session.Alive($main) -and !$session.Visible($main) }
                $session.SimulateTrayRestore($main)
                $null = Wait-For 'main window restored by tray notification' { $session.Visible($main) -and $session.Enabled($main) }
                Write-Host 'PASS: Main X hides to tray and the tray notification restores the same window.'
            }
            $exitAttempted = $true
            Quit-Product
        } finally {
            if ($session) {
                try {
                    if (!$session.Wait(0) -and $main -and $markers.Count -eq 5 -and !$exitAttempted -and $session.Visible($main)) {
                        try { Quit-Product } catch { Write-Warning "Normal cleanup exit failed: $_" }
                    }
                    if (!$session.Wait(0)) { Write-Warning "Cleaning up only test PID $($session.Id)." }
                } finally {
                    $session.Dispose()
                }
            }
        }
    }
    Write-Host 'PASS: Gallery favorite persisted after confirmed exit and relaunch.'
    $status = 0
} catch {
    [Console]::Error.WriteLine("FAIL: $_")
} finally {
    if ($dataRoot) { Write-Host "Retained test data: $dataRoot" }
}
exit $status

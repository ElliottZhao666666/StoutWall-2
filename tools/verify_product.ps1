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

Manual follow-up with disposable data and --test-mode: check main X hides to
tray, restore that instance, then exit from Settings. Check rendering and the
full-screen viewer, source save/refresh, gallery relocation, and theme options
separately. The automated check only opens/closes the source, relocation, theme
and iOS windows. Do not operate startup controls or apply a desktop wallpaper.
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
        [DllImport("user32.dll", ExactSpelling=true)] static extern IntPtr GetParent(IntPtr h);

        static Exception Failed(string step) { return new Win32Exception(Marshal.GetLastWin32Error(), step); }

        public Session(string exe, string directory, string dataRoot, string config) {
            job = CreateJobObjectW(IntPtr.Zero, IntPtr.Zero);
            if (job == IntPtr.Zero) throw Failed("CreateJobObjectW");
            ProcessInfo info = new ProcessInfo();
            try {
                JobLimits limits = new JobLimits();
                limits.Basic.Flags = 0x2008; // KILL_ON_JOB_CLOSE | ACTIVE_PROCESS
                limits.Basic.ActiveProcesses = 1;
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
        public void CloseChild(IntPtr child, IntPtr main) {
            if (child == main || Class(child) != "StoutWallOriginalWindow") throw new InvalidOperationException("Not a product child window");
            Post(child, 0x0010, 0, IntPtr.Zero); // WM_CLOSE
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
            if (Find-Control $window ListBox -Visible) { $window }
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
        '获取 iOS 版', '退出软件', '收藏', '取消收藏')
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
    Click-Allowed $main $Button
    return Wait-For "child window '$Button'" {
        $found = @(Get-TestWindow $Title -Gallery:$Gallery | Where-Object { $_ -ne $main })
        Assert-That ($found.Count -le 1) "Multiple windows for '$Button'."
        if ($found.Count -eq 1) { $found[0] }
    }
}

function Close-Child([IntPtr] $Child, [string] $Name) {
    $session.CloseChild($Child, $main)
    $null = Wait-For "closing '$Name'" { !$session.Visible($Child) }
    Assert-That ($session.Visible($main)) "Main window did not survive closing '$Name'."
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
                Select-Page 1
                Select-Page 2
                Close-Child (Open-Child '选择壁纸源...' '选择新壁纸获取来源...') '选择壁纸源'
                Close-Child (Open-Child '壁纸库位置...' '请指定新的壁纸库位置:') '壁纸库位置'
                Select-Page 3
                Close-Child (Open-Child '导出为Windows 主题包' '导出为Windows主题包') '主题包'
                Select-Page 4
                Close-Child (Open-Child '获取 iOS 版' '胖哥每日壁纸 iOS版') '获取 iOS 版'
                Select-Page 0
                Write-Host 'PASS: Five-page switching and source, relocation, theme and iOS window lifecycle.'
            }

            Select-Page 3
            $gallery = Open-Child '查看/管理壁纸库' -Gallery
            $list = Find-Control $gallery ListBox -Visible
            Assert-That ([bool] $list) 'Gallery list not found.'
            $rows = Wait-For 'dated gallery rows with an initialized selection' {
                if ($session.Enabled($list) -and $session.SelectedRow($list) -ge 0) { Read-Gallery $list }
            }
            Assert-That ($rows.Count -eq $pairs.Count) 'Gallery row count differs from the fixture.'
            foreach ($pair in $pairs) {
                Assert-That ($rows.Contains($pair.Name.Substring(6))) "Fixture date not shown: $($pair.Name)."
            }

            if ($run -eq 1) {
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
    Write-Host 'Manual: main X hides to tray, restore the same test instance, then exit from Settings.'
}
exit $status

// Spawn latency probe for the AV-per-spawn study (issue #1396).
//
// Why C# and not Measure-Command: PowerShell's own invocation overhead is
// 5-15 ms per call, which is the same order as the effect we are chasing on a
// clean machine. This helper drives CreateProcessW / ShellExecuteExW directly
// and times them with QueryPerformanceCounter, so the only cost in the sample
// is what the OS + filter drivers actually add.
//
// Two spawn paths are provided because they exercise different OS machinery:
//   * CreateProcessW  - what terminals, shells, and winuxcmd's own dispatcher use.
//   * ShellExecuteExW - what Explorer uses; this is the path where SmartScreen
//                       (AppInfo service) and MOTW/attachment-manager decisions
//                       are made.
//
// Cold preparation: Windows keeps recently used file data in the system cache
// and in standby pages. To measure an image that is not resident we ask the
// kernel to trim the system file cache (SetSystemFileCacheSize(-1,-1)) and, in
// stronger mode, to purge the standby list (NtSetSystemInformation with
// SystemMemoryListInformation/MemoryPurgeStandbyList) - the same operation
// RAMMap performs. Both need privileges the default token does not carry, so
// callers must run under an elevated token; failures are reported, not faked.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Perf1396
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct SHELLEXECUTEINFO
    {
        public int cbSize;
        public uint fMask;
        public IntPtr hwnd;
        public string lpVerb;
        public string lpFile;
        public string lpParameters;
        public string lpDirectory;
        public int nShow;
        public IntPtr hInstApp;
        public IntPtr lpIDList;
        public string lpClass;
        public IntPtr hkeyClass;
        public uint dwHotKey;
        public IntPtr hIcon;
        public IntPtr hProcess;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct LUID
    {
        public uint LowPart;
        public int HighPart;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct LUID_AND_ATTRIBUTES
    {
        public LUID Luid;
        public uint Attributes;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TOKEN_PRIVILEGES
    {
        public uint PrivilegeCount;
        public LUID_AND_ATTRIBUTES Privilege;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SECURITY_ATTRIBUTES
    {
        public int nLength;
        public IntPtr lpSecurityDescriptor;
        public bool bInheritHandle;
    }

    public sealed class Sample
    {
        public double Ms;
        public int ExitCode = -1;
        public bool Started;
        public bool TimedOut;
        public string Error = "";
        public double PrepMs;
    }

    public static class SpawnBench
    {
        const uint CREATE_NO_WINDOW = 0x08000000;
        const uint STARTF_USESHOWWINDOW = 0x00000001;
        const uint STARTF_USESTDHANDLES = 0x00000100;
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x40000000;
        const uint FILE_SHARE_READ = 0x00000001;
        const uint FILE_SHARE_WRITE = 0x00000002;
        const uint OPEN_EXISTING = 3;
        const uint INFINITE = 0xFFFFFFFF;
        const uint WAIT_TIMEOUT = 0x00000102;
        const uint TOKEN_ADJUST_PRIVILEGES = 0x0020;
        const uint TOKEN_QUERY = 0x0008;
        const uint SE_PRIVILEGE_ENABLED = 0x0002;
        const uint SEE_MASK_NOCLOSEPROCESS = 0x00000040;
        const uint SEE_MASK_NOASYNC = 0x00000100;
        const int SW_HIDE = 0;
        const int SW_SHOWNORMAL = 1;
        const int SystemMemoryListInformation = 80;
        const int MemoryPurgeStandbyList = 4;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string lpApplicationName, string lpCommandLine,
            IntPtr lpProcessAttributes, IntPtr lpThreadAttributes, bool bInheritHandles,
            uint dwCreationFlags, IntPtr lpEnvironment, string lpCurrentDirectory,
            ref STARTUPINFO lpStartupInfo, out PROCESS_INFORMATION lpProcessInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr hProcess, out uint lpExitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        static extern bool QueryPerformanceCounter(out long lpPerformanceCount);

        [DllImport("kernel32.dll")]
        static extern bool QueryPerformanceFrequency(out long lpFrequency);

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetSystemFileCacheSize(IntPtr MinimumFileCacheSize,
            IntPtr MaximumFileCacheSize, uint Flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateFileW(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
            ref SECURITY_ATTRIBUTES lpSecurityAttributes, uint dwCreationDisposition,
            uint dwFlagsAndAttributes, IntPtr hTemplateFile);

        [DllImport("ntdll.dll")]
        static extern int NtSetSystemInformation(int SystemInformationClass,
            IntPtr SystemInformation, int SystemInformationLength);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool ShellExecuteExW(ref SHELLEXECUTEINFO lpExecInfo);

        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool OpenProcessToken(IntPtr ProcessHandle, uint DesiredAccess, out IntPtr TokenHandle);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool LookupPrivilegeValue(string lpSystemName, string lpName, out LUID lpLuid);

        [DllImport("advapi32.dll", SetLastError = true)]
        static extern bool AdjustTokenPrivileges(IntPtr TokenHandle, bool DisableAllPrivileges,
            ref TOKEN_PRIVILEGES NewState, int BufferLength, IntPtr PreviousState, IntPtr ReturnLength);

        [DllImport("kernel32.dll")]
        static extern IntPtr GetCurrentProcess();

        static long freq;
        public static string PrivLog = "";

        static SpawnBench()
        {
            long f;
            QueryPerformanceFrequency(out f);
            freq = f;
        }

        public static double MsSince(long start)
        {
            long now;
            QueryPerformanceCounter(out now);
            return (now - start) * 1000.0 / freq;
        }

        public static long Qpc()
        {
            long v;
            QueryPerformanceCounter(out v);
            return v;
        }

        static bool EnablePrivilege(string name)
        {
            IntPtr token;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out token))
                return false;
            LUID luid;
            if (!LookupPrivilegeValue(null, name, out luid))
            {
                CloseHandle(token);
                return false;
            }
            TOKEN_PRIVILEGES tp = new TOKEN_PRIVILEGES();
            tp.PrivilegeCount = 1;
            tp.Privilege.Luid = luid;
            tp.Privilege.Attributes = SE_PRIVILEGE_ENABLED;
            bool ok = AdjustTokenPrivileges(token, false, ref tp,
                Marshal.SizeOf(typeof(TOKEN_PRIVILEGES)), IntPtr.Zero, IntPtr.Zero);
            int err = Marshal.GetLastWin32Error();
            CloseHandle(token);
            return ok && err == 0;
        }

        /// Enables the two privileges cold preparation needs. Returns a log line
        /// so the caller can prove whether cold legs were really cold.
        public static string PrepareColdSupport()
        {
            bool quota = EnablePrivilege("SeIncreaseQuotaPrivilege");
            bool profile = EnablePrivilege("SeProfileSingleProcessPrivilege");
            PrivLog = "SeIncreaseQuotaPrivilege=" + quota + ";SeProfileSingleProcessPrivilege=" + profile;
            return PrivLog;
        }

        /// Trims the system file cache. Cheap and targeted: it evicts cached file
        /// data (including mapped image pages not currently in a working set).
        public static double TrimFileCache()
        {
            long t0 = Qpc();
            SetSystemFileCacheSize(new IntPtr(-1), new IntPtr(-1), 0);
            return MsSince(t0);
        }

        /// Purges the standby page list. Stronger and slower; leaves the next
        /// image map to fault everything in from disk.
        public static double PurgeStandby()
        {
            long t0 = Qpc();
            IntPtr cmd = Marshal.AllocHGlobal(sizeof(int));
            try
            {
                Marshal.WriteInt32(cmd, MemoryPurgeStandbyList);
                NtSetSystemInformation(SystemMemoryListInformation, cmd, sizeof(int));
            }
            finally { Marshal.FreeHGlobal(cmd); }
            return MsSince(t0);
        }

        /// One CreateProcessW + wait-for-exit, measured end to end.
        ///
        /// The child keeps the parent's console and is created with default
        /// flags, because that is what a terminal or a build tool does. Forcing
        /// CREATE_NO_WINDOW would measure conhost creation instead (measured
        /// +18 ms per spawn on this host), which is an artefact, not the applet.
        /// stdout/stderr point at NUL so 2400 samples don't spray output, and
        /// that redirection is set up once, outside the timed region.
        public static Sample SpawnCreateProcess(string exe, string args, int prepMode)
        {
            return SpawnCreateProcess(exe, args, prepMode, 60000);
        }

        public static Sample SpawnCreateProcess(string exe, string args, int prepMode, int timeoutMs)
        {
            Sample s = new Sample();
            if (prepMode == 1) s.PrepMs = TrimFileCache();
            else if (prepMode == 2) { TrimFileCache(); s.PrepMs = PurgeStandby(); }

            STARTUPINFO si = new STARTUPINFO();
            si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
            si.dwFlags = (int)(STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
            si.wShowWindow = SW_SHOWNORMAL;
            IntPtr nul = NulHandle;
            si.hStdInput = nul;
            si.hStdOutput = nul;
            si.hStdError = nul;
            PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
            string cmdLine = "\"" + exe + "\"";
            if (!string.IsNullOrEmpty(args)) cmdLine += " " + args;

            long t0 = Qpc();
            bool ok = CreateProcessW(exe, cmdLine, IntPtr.Zero, IntPtr.Zero, true,
                0, IntPtr.Zero, null, ref si, out pi);
            if (!ok)
            {
                s.Ms = MsSince(t0);
                s.Error = "CreateProcessW failed: " + Marshal.GetLastWin32Error();
                return s;
            }
            uint wr = WaitForSingleObject(pi.hProcess, (uint)timeoutMs);
            if (wr == WAIT_TIMEOUT)
            {
                // A bound, not a measurement: something is holding the process
                // (security prompt, hung child). Kill it so the run continues.
                s.TimedOut = true;
                s.Ms = MsSince(t0);
                s.Error = "timeout after " + timeoutMs + " ms";
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return s;
            }
            uint code;
            GetExitCodeProcess(pi.hProcess, out code);
            s.Ms = MsSince(t0);
            s.ExitCode = unchecked((int)code);
            s.Started = true;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return s;
        }

        static IntPtr nulHandle = IntPtr.Zero;
        static IntPtr NulHandle
        {
            get
            {
                if (nulHandle == IntPtr.Zero)
                {
                    SECURITY_ATTRIBUTES sa = new SECURITY_ATTRIBUTES();
                    sa.nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES));
                    sa.bInheritHandle = true;
                    nulHandle = CreateFileW("NUL", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, ref sa, OPEN_EXISTING, 0, IntPtr.Zero);
                }
                return nulHandle;
            }
        }

        /// One ShellExecuteExW + wait-for-exit. This is the Explorer path, so it
        /// is the only one where SmartScreen/MOTW policy can intervene.
        public static Sample SpawnShellExecute(string exe, string args, int prepMode, bool waitForExit)
        {
            return SpawnShellExecute(exe, args, prepMode, waitForExit, 15000);
        }

        public static Sample SpawnShellExecute(string exe, string args, int prepMode, bool waitForExit, int timeoutMs)
        {
            Sample s = new Sample();
            if (prepMode == 1) s.PrepMs = TrimFileCache();
            else if (prepMode == 2) { TrimFileCache(); s.PrepMs = PurgeStandby(); }

            SHELLEXECUTEINFO sei = new SHELLEXECUTEINFO();
            sei.cbSize = Marshal.SizeOf(typeof(SHELLEXECUTEINFO));
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.lpVerb = "open";
            sei.lpFile = exe;
            sei.lpParameters = args;
            sei.nShow = SW_HIDE;

            long t0 = Qpc();
            bool ok = ShellExecuteExW(ref sei);
            if (!ok)
            {
                s.Ms = MsSince(t0);
                s.Error = "ShellExecuteExW failed: " + Marshal.GetLastWin32Error();
                return s;
            }
            if (waitForExit && sei.hProcess != IntPtr.Zero)
            {
                uint wr = WaitForSingleObject(sei.hProcess, (uint)timeoutMs);
                if (wr == WAIT_TIMEOUT)
                {
                    s.TimedOut = true;
                    s.Ms = MsSince(t0);
                    s.Error = "timeout after " + timeoutMs + " ms (blocking security prompt?)";
                    TerminateProcess(sei.hProcess, 1);
                    CloseHandle(sei.hProcess);
                    return s;
                }
                uint code;
                GetExitCodeProcess(sei.hProcess, out code);
                s.ExitCode = unchecked((int)code);
                CloseHandle(sei.hProcess);
            }
            s.Ms = MsSince(t0);
            s.Started = true;
            return s;
        }
    }
}

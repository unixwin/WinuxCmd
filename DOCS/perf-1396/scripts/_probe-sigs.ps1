param([string]$Root = 'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd')
$targets = @(
    'usr\bin\ls.exe', 'usr\bin\which.exe', 'usr\bin\git.exe', 'usr\bin\sort.exe',
    'usr\bin\winuxcmd.exe', 'opt\niugit\bin\git.exe', 'opt\niugit\bin\sort.exe'
)
foreach ($t in $targets) {
    $p = Join-Path $Root $t
    if (-not (Test-Path $p)) { "MISSING  $t"; continue }
    $f = Get-Item $p -Force
    $s = Get-AuthenticodeSignature $p
    $ver = (Get-Item $p).VersionInfo
    "{0}`n  size={1} mtime={2} sig={3}`n  subject={4}`n  product={5} filever={6}" -f `
        $t, $f.Length, $f.LastWriteTimeUtc.ToString('yyyy-MM-dd'), $s.Status, `
        $s.SignerCertificate.Subject, $ver.ProductName, $ver.FileVersion
}

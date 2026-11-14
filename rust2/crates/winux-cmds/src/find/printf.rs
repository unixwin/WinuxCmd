//! `-printf` / `-fprintf` format compilation and rendering, `-ls`
//! (findutils 4.9 print.c `insert_fprintf` / `do_fprintf` / `format_date`
//! port; gnulib listfile.c `list_file` for `-ls`).
//!
//! Byte-exact quirks kept from print.c:
//! * every directive is a C printf conversion: `%d` honors all flags,
//!   `%m` is `%o` (so `#` gives a leading 0), `%S` is `%g`, everything
//!   else is `%s` (width, `-` and precision apply; `0`, `+`, ` ` do not);
//! * an unknown directive (`%q`, `%5x`) prints its whole text after a
//!   warning; `%5%` prints `%5` (the segment text before the second `%`);
//! * `\c` flushes and drops the rest of the format; `\NNN` takes up to
//!   three octal digits (`\0` is a NUL byte); an unknown escape `\q`
//!   warns and stays literal;
//! * `%h` of a one-character name with a trailing slash (`a/`) is `a`
//!   (the dirname loop never trims at index 0);
//! * time directives `%Ak %Bk %Ck %Tk` are strftime(`%k`) in local time,
//!   with `.NNNNNNNNN0` spliced after the seconds digits for `S T X @ +`.

use std::io::Write;

use super::entry::{Entry, Kind, Ts};
use super::Run;

/// printf conversion flags of one directive.
#[derive(Clone, Debug, Default)]
pub struct Spec {
    minus: bool,
    plus: bool,
    space: bool,
    hash: bool,
    zero: bool,
    width: Option<usize>,
    prec: Option<usize>,
}

#[derive(Clone, Debug)]
pub enum Seg {
    Lit(Vec<u8>),
    /// `\c`: flush the destination and stop.
    Stop,
    Dir(Spec, u8),
    Time(Spec, u8, u8),
}

/// Compile a `-printf` format. Warnings go to `warn`; a fatal format
/// error returns `Err(message)`.
pub fn compile(fmt: &[u8], warn: &mut dyn Write) -> Result<Vec<Seg>, String> {
    let mut segs: Vec<Seg> = Vec::new();
    let mut lit: Vec<u8> = Vec::new();
    let mut i = 0;
    let flush_lit = |segs: &mut Vec<Seg>, lit: &mut Vec<u8>| {
        if !lit.is_empty() {
            segs.push(Seg::Lit(std::mem::take(lit)));
        }
    };
    while i < fmt.len() {
        let c = fmt[i];
        if c == b'\\' {
            if i + 1 >= fmt.len() {
                let _ = writeln!(
                    warn,
                    "find: warning: escape `\\' followed by nothing at all"
                );
                lit.push(b'\\');
                i += 1;
                continue;
            }
            let k = fmt[i + 1];
            if k == b'c' {
                flush_lit(&mut segs, &mut lit);
                segs.push(Seg::Stop);
                return Ok(segs);
            }
            if (b'0'..=b'7').contains(&k) {
                let mut n: u32 = 0;
                let mut j = i + 1;
                while j < fmt.len() && j < i + 4 && (b'0'..=b'7').contains(&fmt[j]) {
                    n = n * 8 + (fmt[j] - b'0') as u32;
                    j += 1;
                }
                lit.push(n as u8);
                i = j;
                continue;
            }
            let v = match k {
                b'a' => 7,
                b'b' => 8,
                b'f' => 12,
                b'n' => b'\n',
                b'r' => b'\r',
                b't' => b'\t',
                b'v' => 11,
                b'\\' => b'\\',
                _ => 0,
            };
            if v == 0 {
                let _ = writeln!(warn, "find: warning: unrecognized escape `\\{}'", k as char);
                lit.push(b'\\');
                i += 1;
                continue;
            }
            lit.push(v);
            i += 2;
            continue;
        }
        if c != b'%' {
            lit.push(c);
            i += 1;
            continue;
        }
        // directive
        if i + 1 >= fmt.len() {
            return Err("error: % at end of format string".to_string());
        }
        let start = i;
        let mut j = i + 1;
        let mut spec = Spec::default();
        if fmt[j] != b'%' {
            while j < fmt.len() && b"-+ #".contains(&fmt[j]) {
                match fmt[j] {
                    b'-' => spec.minus = true,
                    b'+' => spec.plus = true,
                    b' ' => spec.space = true,
                    _ => spec.hash = true,
                }
                j += 1;
            }
            if j < fmt.len() && fmt[j] == b'0' {
                spec.zero = true;
            }
            let ws = j;
            while j < fmt.len() && fmt[j].is_ascii_digit() {
                j += 1;
            }
            if j > ws {
                spec.width = std::str::from_utf8(&fmt[ws..j])
                    .ok()
                    .and_then(|s| s.parse().ok());
            }
            if j < fmt.len() && fmt[j] == b'.' {
                j += 1;
                let ps = j;
                while j < fmt.len() && fmt[j].is_ascii_digit() {
                    j += 1;
                }
                spec.prec = Some(
                    std::str::from_utf8(&fmt[ps..j])
                        .ok()
                        .and_then(|s| s.parse().ok())
                        .unwrap_or(0),
                );
            }
        }
        let d = fmt.get(j).copied().unwrap_or(0);
        if d == b'%' {
            // `%%` -> "%", `%5%` -> "%5" (print.c segment text quirk)
            lit.extend_from_slice(&fmt[start..j]);
            i = j + 1;
            continue;
        }
        if d != 0 && b"abcdDfFgGhHiklmMnpPsStuUyYZ".contains(&d) {
            flush_lit(&mut segs, &mut lit);
            segs.push(Seg::Dir(spec, d));
            i = j + 1;
            continue;
        }
        if d != 0 && b"ABCT".contains(&d) {
            if j + 1 < fmt.len() {
                flush_lit(&mut segs, &mut lit);
                segs.push(Seg::Time(spec, d, fmt[j + 1]));
                i = j + 2;
                continue;
            }
            let _ = writeln!(
                warn,
                "find: warning: format directive `%{}' should be followed by another character",
                d as char
            );
            lit.extend_from_slice(&fmt[start..=j]);
            i = j + 1;
            continue;
        }
        if d == 0 || b"{[(".contains(&d) {
            return Err(format!(
                "error: the format directive `%{}' is reserved for future use",
                if d == 0 {
                    String::new()
                } else {
                    (d as char).to_string()
                }
            ));
        }
        let _ = writeln!(
            warn,
            "find: warning: unrecognized format directive `%{}'",
            d as char
        );
        lit.extend_from_slice(&fmt[start..=j]);
        i = j + 1;
    }
    flush_lit(&mut segs, &mut lit);
    Ok(segs)
}

fn pad(out: &mut Vec<u8>, body: &[u8], spec: &Spec, zero_ok: bool) {
    let w = spec.width.unwrap_or(0);
    if body.len() >= w {
        out.extend_from_slice(body);
        return;
    }
    let fill = w - body.len();
    if spec.minus {
        out.extend_from_slice(body);
        out.extend(std::iter::repeat_n(b' ', fill));
    } else if zero_ok && spec.zero {
        // zero padding goes after any sign / 0x prefix
        let sign_len = if !body.is_empty() && matches!(body[0], b'+' | b'-' | b' ') {
            1
        } else {
            0
        };
        out.extend_from_slice(&body[..sign_len]);
        out.extend(std::iter::repeat_n(b'0', fill));
        out.extend_from_slice(&body[sign_len..]);
    } else {
        out.extend(std::iter::repeat_n(b' ', fill));
        out.extend_from_slice(body);
    }
}

/// C `%s` with this spec.
pub fn fmt_s(out: &mut Vec<u8>, spec: &Spec, s: &[u8]) {
    let body = match spec.prec {
        Some(p) if p < s.len() => &s[..p],
        _ => s,
    };
    pad(out, body, spec, false);
}

/// C `%d`.
fn fmt_d(out: &mut Vec<u8>, spec: &Spec, v: i64) {
    let mut digits = v.unsigned_abs().to_string();
    if let Some(p) = spec.prec {
        if p == 0 && v == 0 {
            digits.clear();
        }
        while digits.len() < p {
            digits.insert(0, '0');
        }
    }
    let mut body = String::new();
    if v < 0 {
        body.push('-');
    } else if spec.plus {
        body.push('+');
    } else if spec.space {
        body.push(' ');
    }
    body.push_str(&digits);
    pad(out, body.as_bytes(), spec, spec.prec.is_none());
}

/// C `%o`.
fn fmt_o(out: &mut Vec<u8>, spec: &Spec, v: u64) {
    let mut digits = format!("{v:o}");
    if let Some(p) = spec.prec {
        if p == 0 && v == 0 {
            digits.clear();
        }
        while digits.len() < p {
            digits.insert(0, '0');
        }
    }
    if spec.hash && !digits.starts_with('0') {
        digits.insert(0, '0');
    }
    pad(out, digits.as_bytes(), spec, spec.prec.is_none());
}

/// C `%g`.
pub fn fmt_g(out: &mut Vec<u8>, spec: &Spec, v: f64) {
    let mut body = String::new();
    if v.is_sign_negative() && v != 0.0 {
        body.push('-');
    } else if spec.plus {
        body.push('+');
    } else if spec.space {
        body.push(' ');
    }
    let a = v.abs();
    if a.is_infinite() {
        body.push_str("inf");
    } else if a.is_nan() {
        body.push_str("nan");
    } else {
        let p = match spec.prec {
            Some(0) => 1,
            Some(p) => p,
            None => 6,
        };
        let s = if a == 0.0 {
            "0".to_string()
        } else {
            let e = format!("{:.*e}", p - 1, a);
            let (mant, exp) = e.split_once('e').unwrap();
            let x: i32 = exp.parse().unwrap();
            let mut r = if x < -4 || x >= p as i32 {
                let mut m = mant.to_string();
                if !spec.hash && m.contains('.') {
                    m = m.trim_end_matches('0').trim_end_matches('.').to_string();
                }
                format!("{m}e{}{:02}", if x < 0 { '-' } else { '+' }, x.abs())
            } else {
                format!("{:.*}", (p as i32 - 1 - x).max(0) as usize, a)
            };
            if !spec.hash && !r.contains('e') && r.contains('.') {
                r = r.trim_end_matches('0').trim_end_matches('.').to_string();
            }
            r
        };
        body.push_str(&s);
    }
    pad(out, body.as_bytes(), spec, true);
}

const WEEKDAYS: [&str; 7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS: [&str; 12] = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
];

fn weekday(dt: &crate::date::DateTime) -> usize {
    ((dt.epoch + dt.utc_offset).div_euclid(86400) + 4).rem_euclid(7) as usize
}

/// print.c ctime_format: "Wed Jan  1 00:00:00.1234567890 2020".
pub fn ctime_format(run: &Run, ts: Ts) -> String {
    let dt = run.local(ts.sec);
    format!(
        "{} {} {:2} {:02}:{:02}:{:02}.{:09}0 {:04}",
        WEEKDAYS[weekday(&dt)],
        MONTHS[(dt.month - 1) as usize],
        dt.day,
        dt.hour,
        dt.minute,
        dt.second,
        ts.nsec,
        dt.year
    )
}

/// print.c format_date for `%Xk`.
pub fn format_date(run: &Run, ts: Ts, k: u8) -> String {
    let ns = format!(".{:09}0", ts.nsec);
    if k == b'@' {
        return format!("{}{}", ts.sec, ns);
    }
    let dt = run.local(ts.sec);
    let f: &str = match k {
        b'+' => "%Y-%m-%d+%H:%M:%S",
        b'c' => "%a %b %e %H:%M:%S %Y",
        b'x' => "%m/%d/%y",
        b'X' | b'T' => "%H:%M:%S",
        b'r' => "%I:%M:%S %p",
        _ => "",
    };
    let mut s = if f.is_empty() {
        let one = format!("%{}", k as char);
        crate::date::strftime(&dt, &one)
    } else {
        crate::date::strftime(&dt, f)
    };
    if matches!(k, b'S' | b'T' | b'X' | b'+') {
        s.push_str(&ns);
    }
    s
}

/// GNU base_name: last component, keeping one trailing slash.
pub fn base_name(p: &str) -> &str {
    let b = p.as_bytes();
    if b.is_empty() {
        return p;
    }
    if b.iter().all(|&c| c == b'/') {
        return if b.len() >= 3 { &p[..2] } else { &p[..1] };
    }
    let mut end = b.len();
    while end > 0 && b[end - 1] == b'/' {
        end -= 1;
    }
    let start = b[..end]
        .iter()
        .rposition(|&c| c == b'/')
        .map(|i| i + 1)
        .unwrap_or(0);
    let stop = if end < b.len() { end + 1 } else { end };
    &p[start..stop]
}

/// print.c `%h`.
fn dir_name(p: &str) -> Vec<u8> {
    let mut b = p.as_bytes().to_vec();
    if b.is_empty() {
        return b".".to_vec();
    }
    let mut s = b.len() - 1;
    while s > 0 && b[s] == b'/' {
        s -= 1;
    }
    if s > 0 && s + 1 < b.len() && b[s + 1] == b'/' {
        b.truncate(s + 1);
    }
    match b.iter().rposition(|&c| c == b'/') {
        None => b".".to_vec(),
        Some(i) => b[..i].to_vec(),
    }
}

/// `ls -l` mode string.
pub fn mode_string(e: &Entry) -> String {
    let mut s = String::with_capacity(10);
    s.push(match e.kind() {
        Kind::Reg => '-',
        Kind::Dir => 'd',
        Kind::Lnk => 'l',
        Kind::Chr => 'c',
        Kind::Fifo => 'p',
    });
    s.push_str(&crate::modemap::rwx_string(e.mode()));
    s
}

/// 512-byte blocks allocated (GNU ST_NBLOCKS).
pub fn blocks512(e: &Entry) -> u64 {
    e.full().map(|h| h.alloc.div_ceil(512)).unwrap_or(0)
}

/// `%Y` letter of an entry.
fn target_letter(e: &Entry) -> String {
    if e.st().kind != Kind::Lnk {
        return (e.st().kind.letter() as char).to_string();
    }
    match e.followed() {
        Ok((b, _)) => (b.kind.letter() as char).to_string(),
        Err(1921) => "L".to_string(),
        Err(2) | Err(3) => "N".to_string(),
        Err(_) => "N".to_string(),
    }
}

/// Render the compiled format for one entry into `out`. Returns false
/// when a `\c` stop was hit (the caller flushes).
pub fn render(run: &mut Run, segs: &[Seg], e: &Entry, out: &mut Vec<u8>) -> bool {
    for seg in segs {
        match seg {
            Seg::Lit(b) => out.extend_from_slice(b),
            Seg::Stop => return false,
            Seg::Time(spec, which, k) => {
                let ts = match which {
                    b'A' => e.st().atime,
                    b'B' => e.st().btime,
                    b'C' => e.ctime(),
                    _ => e.st().mtime,
                };
                let s = format_date(run, ts, *k);
                fmt_s(out, spec, s.as_bytes());
            }
            Seg::Dir(spec, d) => match d {
                b'a' => fmt_s(out, spec, ctime_format(run, e.st().atime).as_bytes()),
                b'c' => fmt_s(out, spec, ctime_format(run, e.ctime()).as_bytes()),
                b't' => fmt_s(out, spec, ctime_format(run, e.st().mtime).as_bytes()),
                b'b' => fmt_s(out, spec, blocks512(e).to_string().as_bytes()),
                b'k' => fmt_s(out, spec, blocks512(e).div_ceil(2).to_string().as_bytes()),
                b'd' => fmt_d(out, spec, e.depth as i64),
                b'D' => fmt_s(
                    out,
                    spec,
                    e.full()
                        .map(|h| h.dev as u64)
                        .unwrap_or(0)
                        .to_string()
                        .as_bytes(),
                ),
                b'f' => fmt_s(out, spec, base_name(&e.path).as_bytes()),
                b'F' => {
                    let t = run.fstype(e);
                    fmt_s(out, spec, t.as_bytes())
                }
                b'g' | b'G' => {
                    let (id, name) = run.account(e, false);
                    match name {
                        Some(n) if *d == b'g' => fmt_s(out, spec, n.as_bytes()),
                        _ => fmt_s(out, spec, id.to_string().as_bytes()),
                    }
                }
                b'u' | b'U' => {
                    let (id, name) = run.account(e, true);
                    match name {
                        Some(n) if *d == b'u' => fmt_s(out, spec, n.as_bytes()),
                        _ => fmt_s(out, spec, id.to_string().as_bytes()),
                    }
                }
                b'h' => fmt_s(out, spec, &dir_name(&e.path)),
                b'H' => fmt_s(out, spec, &e.path.as_bytes()[..e.root_len]),
                b'i' => fmt_s(
                    out,
                    spec,
                    e.full().map(|h| h.ino).unwrap_or(0).to_string().as_bytes(),
                ),
                b'l' => fmt_s(out, spec, e.link_target().unwrap_or("").as_bytes()),
                b'm' => fmt_o(out, spec, e.mode() as u64),
                b'M' => fmt_s(out, spec, mode_string(e).as_bytes()),
                b'n' => fmt_s(
                    out,
                    spec,
                    e.full()
                        .map(|h| h.nlink)
                        .unwrap_or(1)
                        .to_string()
                        .as_bytes(),
                ),
                b'p' => fmt_s(out, spec, e.path.as_bytes()),
                b'P' => {
                    let rest: &[u8] = if e.depth > 0 {
                        let r = &e.path.as_bytes()[e.root_len..];
                        r.strip_prefix(b"/").unwrap_or(r)
                    } else {
                        b""
                    };
                    fmt_s(out, spec, rest)
                }
                b's' => fmt_s(out, spec, e.size().to_string().as_bytes()),
                b'S' => {
                    let size = e.size();
                    let blocks = blocks512(e);
                    let v = if size == 0 {
                        if blocks == 0 {
                            1.0
                        } else {
                            f64::INFINITY
                        }
                    } else {
                        (blocks as f64 * 512.0) / size as f64
                    };
                    fmt_g(out, spec, v)
                }
                b'y' => fmt_s(out, spec, &[e.kind().letter()]),
                b'Y' => fmt_s(out, spec, target_letter(e).as_bytes()),
                b'Z' => {
                    // GNU with SELinux disabled: getfilecon fails, %Z is
                    // empty, the run exits 1 (probed: "getfilecon failed")
                    fmt_s(out, spec, b"");
                    let msg = format!(
                        "find: getfilecon failed: '{}': Operation not supported\n",
                        e.path
                    );
                    run.error(&msg);
                }
                _ => {}
            },
        }
    }
    true
}

/// gnulib list_file (`-ls` / `-fls`): widths grow when a value is wider.
pub fn ls_line(run: &mut Run, e: &Entry, out: &mut Vec<u8>) {
    let ino = e.full().map(|h| h.ino).unwrap_or(0).to_string();
    let blocks = blocks512(e).div_ceil(2).to_string();
    let nlink = e.full().map(|h| h.nlink).unwrap_or(1).to_string();
    let (uid, uname) = run.account(e, true);
    let (gid, gname) = run.account(e, false);
    let user = uname.unwrap_or_else(|| uid.to_string());
    let group = gname.unwrap_or_else(|| gid.to_string());
    let size = e.size().to_string();
    let w = &mut run.ls_widths;
    w[0] = w[0].max(ino.len());
    w[1] = w[1].max(blocks.len());
    w[2] = w[2].max(nlink.len());
    w[3] = w[3].max(user.len());
    w[4] = w[4].max(group.len());
    w[5] = w[5].max(size.len());
    let w = *w;
    let mtime = e.st().mtime;
    let dt = run.local(mtime.sec);
    let now = run.start.sec;
    let recent = mtime.sec > now - 31_556_952 / 2 && mtime.sec <= now;
    let date = if recent {
        format!(
            "{} {:2} {:02}:{:02}",
            MONTHS[(dt.month - 1) as usize],
            dt.day,
            dt.hour,
            dt.minute
        )
    } else {
        format!(
            "{} {:2}  {}",
            MONTHS[(dt.month - 1) as usize],
            dt.day,
            dt.year
        )
    };
    let line = format!(
        "{:>w0$} {:>w1$} {} {:>w2$} {:<w3$} {:<w4$} {:>w5$} {} ",
        ino,
        blocks,
        mode_string(e),
        nlink,
        user,
        group,
        size,
        date,
        w0 = w[0],
        w1 = w[1],
        w2 = w[2],
        w3 = w[3],
        w4 = w[4],
        w5 = w[5],
    );
    out.extend_from_slice(line.as_bytes());
    out.extend_from_slice(e.path.as_bytes());
    if let Some(t) = e.link_target() {
        out.extend_from_slice(b" -> ");
        out.extend_from_slice(t.as_bytes());
    }
    out.push(b'\n');
}

#[cfg(test)]
mod tests {
    use super::*;

    fn comp(f: &str) -> (Vec<Seg>, String) {
        let mut w = Vec::new();
        let s = compile(f.as_bytes(), &mut w).unwrap();
        (s, String::from_utf8(w).unwrap())
    }

    #[test]
    fn escapes_and_unknowns() {
        let (s, w) = comp("%q|\\q|%5%|ab%%|\\c|after");
        assert!(w.contains("unrecognized format directive `%q'"));
        assert!(w.contains("unrecognized escape `\\q'"));
        let mut lit = Vec::new();
        for seg in &s {
            match seg {
                Seg::Lit(b) => lit.extend_from_slice(b),
                Seg::Stop => break,
                _ => panic!(),
            }
        }
        assert_eq!(lit, b"%q|\\q|%5|ab%|");
        let (s, _) = comp("a\\0b\\101\\1012\\n");
        match &s[0] {
            Seg::Lit(b) => assert_eq!(b, b"a\0bAA2\n"),
            _ => panic!(),
        }
        let mut w = Vec::new();
        assert!(compile(b"%", &mut w).is_err());
        assert!(compile(b"%{", &mut w).is_err());
    }

    #[test]
    fn conversions() {
        let spec = |f: &str| -> Spec {
            let (s, _) = comp(f);
            match &s[0] {
                Seg::Dir(sp, _) => sp.clone(),
                _ => panic!(),
            }
        };
        let mut o = Vec::new();
        fmt_s(&mut o, &spec("%10p"), b"a/f5");
        fmt_s(&mut o, &spec("%-10p"), b"a/f5");
        fmt_s(&mut o, &spec("%.3p"), b"a/f5");
        fmt_s(&mut o, &spec("%05s"), b"5");
        assert_eq!(o, b"      a/f5a/f5      a/f    5");
        let mut o = Vec::new();
        fmt_d(&mut o, &spec("%+5d"), 0);
        fmt_d(&mut o, &spec("%05d"), 0);
        fmt_d(&mut o, &spec("% d"), 0);
        fmt_o(&mut o, &spec("%#m"), 0o644);
        fmt_o(&mut o, &spec("%05m"), 0o644);
        fmt_o(&mut o, &spec("%+m"), 0o644);
        assert_eq!(String::from_utf8(o).unwrap(), "   +000000 064400644644");
        let mut o = Vec::new();
        fmt_g(&mut o, &Spec::default(), 819.2);
        o.push(b'|');
        fmt_g(&mut o, &Spec::default(), 1.0);
        o.push(b'|');
        fmt_g(&mut o, &Spec::default(), 0.0001234);
        o.push(b'|');
        fmt_g(&mut o, &Spec::default(), 12345678.0);
        assert_eq!(
            String::from_utf8(o).unwrap(),
            "819.2|1|0.0001234|1.23457e+07"
        );
    }

    #[test]
    fn names() {
        assert_eq!(base_name("a/"), "a/");
        assert_eq!(base_name("./a/"), "a/");
        assert_eq!(base_name("a//"), "a/");
        assert_eq!(base_name("/"), "/");
        assert_eq!(base_name("a/b"), "b");
        assert_eq!(dir_name("a/"), b"a");
        assert_eq!(dir_name("ab/"), b".");
        assert_eq!(dir_name("./a/"), b".");
        assert_eq!(dir_name("a/b/big"), b"a/b");
        assert_eq!(dir_name("/"), b"");
        assert_eq!(dir_name("."), b".");
    }
}

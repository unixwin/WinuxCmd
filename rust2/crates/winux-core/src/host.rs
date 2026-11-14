//! Per-invocation host services injected through [`Ctx`](crate::Ctx).
//!
//! This is the seam a caller uses to supply runtime message lookup (i18n
//! catalogs) without any process-global state: the host travels with the
//! invocation's context, lookups run synchronously on the caller's thread,
//! and the trait is deliberately not `Send`/`Sync`-bound. Nothing here
//! touches the process environment, the filesystem, or catalogs on disk.

use std::collections::BTreeMap;

/// Host-provided message lookup. `message(key)` returns the catalog text
/// for `key`, or `None` when the host has no entry - callers then use
/// their compiled-in English fallback.
pub trait HostServices {
    #[allow(unused_variables)]
    fn message(&self, key: &str) -> Option<&str> {
        None
    }
}

/// The default host: no catalog at all, every lookup misses.
pub struct EmptyHost;

pub static DEFAULT_HOST: EmptyHost = EmptyHost;

impl HostServices for EmptyHost {}

/// In-memory host for tests and injected catalogs: key -> text.
#[derive(Default)]
pub struct MapHost {
    pub messages: BTreeMap<String, String>,
}

impl HostServices for MapHost {
    fn message(&self, key: &str) -> Option<&str> {
        self.messages.get(key).map(String::as_str)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_host_always_misses() {
        assert_eq!(DEFAULT_HOST.message("any.key"), None);
    }

    #[test]
    fn map_host_lookup_and_default() {
        let mut host = MapHost::default();
        assert_eq!(host.message("k"), None);
        host.messages
            .insert("k".to_string(), "catalog text".to_string());
        assert_eq!(host.message("k"), Some("catalog text"));
        assert_eq!(host.message("other"), None);
    }
}

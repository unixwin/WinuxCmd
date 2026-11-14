//! Shared user-facing message builders.
//!
//! These are the operands diagnostics many commands spell identically
//! (`missing operand`, `extra operand 'x'`, ...). The keys are the ones
//! the C++ side already publishes (`common.error.*`), so one zh-CN entry
//! covers both surfaces. The C++ catalog text is positioned (`{}`); the
//! extractor's migration annotation records the field names used here,
//! and the renderer accepts the positioned spelling at runtime.
//!
//! Program prefixes stay in the caller: these return the message only.

use winux_core::{tr, Ctx};

pub fn missing_operand(ctx: &Ctx) -> String {
    tr!(ctx, "common.error.missing_operand", "missing operand")
}

pub fn missing_file_operand(ctx: &Ctx) -> String {
    tr!(ctx, "common.error.missing_file", "missing file operand")
}

pub fn extra_operand(ctx: &Ctx, operand: &str) -> String {
    tr!(
        ctx,
        "common.error.extra_operand",
        "extra operand '{operand}'",
        operand = operand
    )
}

pub fn missing_after(ctx: &Ctx, operand: &str) -> String {
    tr!(
        ctx,
        "common.error.missing_after",
        "missing operand after '{operand}'",
        operand = operand
    )
}

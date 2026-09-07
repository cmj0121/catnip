/*
 * fs_path.h - the one rule that keeps a Lua app inside fs_base (#45).
 *
 * catnip_hal.h hands the runtime a single string, fs_base, and calls it a root.
 * Nothing in the word "root" enforces itself. Every fs.* entry point in
 * catnip_api.c joins fs_base with a name the app chose, and an app is free to
 * choose "..", or "/etc/passwd", or "a/./b/" - all of which a plain join turns
 * into a path that is no longer on the card. This file is where that name is
 * checked, and catnip_api.c's fs_path() is the only caller, so that the six
 * fs.* calls cannot end up disagreeing about the rule.
 *
 * It is deliberately arithmetic on strings with no filesystem in it, so the
 * host build can exercise it without a card in the slot - the same split the
 * rest of this directory uses (see README.md, "Testing what has no hardware").
 */
#ifndef CATNIP_FS_PATH_H
#define CATNIP_FS_PATH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Join `name` onto `base` and write the result into `out`, whose capacity is
 * `cap` bytes including the terminator and must be at least one. Returns true
 * when `name` names something inside `base`; returns false and leaves `out`
 * empty when it does not, in which case the caller must not touch the
 * filesystem at all.
 *
 * `name` is a relative path: ordinary segments separated by '/', with no
 * leading and no trailing separator. The empty name is `base` itself, which is
 * what fs.list() with no argument asks for. Three kinds of segment are refused:
 *
 *   ".."  climbs out of the root, wherever in the path it appears. "../etc"
 *         and "a/../../etc" are the same request written two ways, so the
 *         position of the segment is not part of the test.
 *   "."   is a segment that means nothing, and its only real use here is to pad
 *         a path until a check written against the obvious cases stops
 *         recognising it.
 *   ""    is what a leading '/' produces (an absolute path, which a join would
 *         otherwise paste after the root and hand back as though it were
 *         under it), what a trailing '/' produces (which changes what the app's
 *         own next join builds), and what "//" produces.
 *
 * Only a segment that is exactly ".." or "." is refused, not any segment
 * containing dots: "a..b.txt" and "..hidden" are ordinary filenames on a card
 * and go through. A substring search for ".." cannot tell those apart from a
 * climb, which is why this walks segments instead.
 *
 * A name that does not fit in `cap` is refused rather than truncated. A
 * truncated path is a different path, and the app that asked to delete the
 * first one would be deleting the second.
 *
 * Refusing rather than sanitising is the deliberate half of this. Quietly
 * rewriting "../../etc" into something harmless hands the app a success for an
 * operation it never asked for, and the mistake stays in the app forever
 * because nothing ever complained about it. A refusal is an error its author
 * can see and fix. That is worth more than the hobby-device framing suggests:
 * the first app through this door is a file browser with a delete button. */
bool catnip_fs_resolve(const char *base, const char *name, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* CATNIP_FS_PATH_H */

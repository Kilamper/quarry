# qtkeychain-qt6 overlay

Same as the upstream vcpkg port (baseline in `vcpkg.json` of the repo root), except that on Linux
it does **not** depend on vcpkg's `libsecret`/`glib`.

Why: QtKeychain loads the system `libsecret-1.so.0` at run time with `QLibrary("secret-1")`, while
the upstream port compiled vcpkg's static glib/libsecret into `libqt6keychain.so`. Two copies of
glib in one process crashed (`SIGSEGV`, `SEGV_ACCERR`) on the first keychain job. Linking the
system libsecret/glib (`libsecret-1-dev`, installed by `scripts/setup.sh`) keeps a single glib,
which is also how Linux desktop apps normally ship (glib is a system library).

When updating the vcpkg baseline, re-copy `portfile.cmake`/patches from `$VCPKG_ROOT/ports/qtkeychain-qt6`
and re-apply the two changes (dependency removal, `LIBSECRET_SUPPORT=ON` on Linux).

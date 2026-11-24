#!/bin/bash
# Wrap interactive Mac launches. A command-line override bypasses the chooser.
set -eu
if [[ -z "${DC3_PG_SONGS:-}" ]]; then
    pref_domain=com.milohax.dc3-native
    previous="$(defaults read "$pref_domain" PGSongs 2>/dev/null || true)"
    mode="$(osascript - "$previous" <<'APPLESCRIPT'
on run argv
    set defaultMode to "PG songs"
    if item 1 of argv is "0" then set defaultMode to "Full catalog"
    try
        set picked to button returned of (display dialog "Choose the song list for free play." with title "Dance Central 3" buttons {"Cancel", "Full catalog", "PG songs"} default button defaultMode cancel button "Cancel")
    on error number -128
        return ""
    end try
    if picked is "Full catalog" then return "0"
    return "1"
end run
APPLESCRIPT
)"
    case "$mode" in
        "") exit 0 ;;
        0|1) defaults write "$pref_domain" PGSongs -int "$mode" ;;
        *) exit 1 ;;
    esac
    export DC3_PG_SONGS="$mode"
fi
exec "$@"

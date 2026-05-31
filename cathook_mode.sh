#!/usr/bin/env bash

cathook_is_enabled() {
    case "${1:-}" in
        1 | true | TRUE | True | yes | YES | Yes | y | Y | on | ON | On)
            return 0
            ;;
    esac

    return 1
}

cathook_is_disabled() {
    case "${1:-}" in
        "" | 0 | false | FALSE | False | no | NO | No | n | N | off | OFF | Off)
            return 0
            ;;
    esac

    return 1
}

cathook_normalize_mode() {
    local value="${1:-}"
    local allow_both="${2:-0}"

    case "$value" in
        default | non-textmode | non_textmode | normal | gui | 0 | 1)
            printf '%s\n' "default"
            ;;
        textmode | text | 2)
            printf '%s\n' "textmode"
            ;;
        both | all | 3)
            if [ "$allow_both" = "1" ]; then
                printf '%s\n' "both"
            else
                return 1
            fi
            ;;
        *)
            return 1
            ;;
    esac
}

cathook_normalize_textmode_mode() {
    local value="${1:-}"

    if cathook_is_enabled "$value"; then
        printf '%s\n' "textmode"
        return
    fi

    if cathook_is_disabled "$value"; then
        printf '%s\n' "default"
        return
    fi

    return 1
}

cathook_mode_from_env() {
    local allow_both="${1:-0}"
    local mode=""

    if [ -n "${CATHOOK_MODE:-}" ]; then
        cathook_normalize_mode "$CATHOOK_MODE" "$allow_both"
        return
    fi

    if [ -n "${CAT_BUILD_MODE:-}" ]; then
        cathook_normalize_mode "$CAT_BUILD_MODE" "$allow_both"
        return
    fi

    if [ "${CATHOOK_TEXTMODE+x}" = "x" ]; then
        cathook_normalize_textmode_mode "$CATHOOK_TEXTMODE"
        return
    fi

    if [ "${TEXTMODE+x}" = "x" ]; then
        cathook_normalize_textmode_mode "$TEXTMODE"
        return
    fi

    if [ -n "${CATHOOK_BINARY:-}" ]; then
        case "$(basename -- "$CATHOOK_BINARY")" in
            libcathook.so)
                mode="default"
                ;;
            libcathooktextmode.so | libcathook-textmode.so)
                mode="textmode"
                ;;
            *)
                return 1
                ;;
        esac

        cathook_normalize_mode "$mode" "$allow_both"
        return
    fi

    return 1
}

cathook_user_home() {
    local sudo_home=""

    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        sudo_home="$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6 || true)"
        if [ -n "$sudo_home" ]; then
            printf '%s\n' "$sudo_home"
            return
        fi
    fi

    if [ -n "${HOME:-}" ]; then
        printf '%s\n' "$HOME"
        return
    fi

    return 1
}

cathook_mode_file() {
    local home_dir=""

    if [ -n "${CATHOOK_MODE_FILE:-}" ]; then
        printf '%s\n' "$CATHOOK_MODE_FILE"
        return
    fi

    if [ -n "${CATHOOK_CONFIG_DIR:-}" ]; then
        printf '%s\n' "$CATHOOK_CONFIG_DIR/mode"
        return
    fi

    if [ -z "${SUDO_USER:-}" ] && [ -n "${XDG_CONFIG_HOME:-}" ]; then
        printf '%s\n' "$XDG_CONFIG_HOME/cathook/mode"
        return
    fi

    if home_dir="$(cathook_user_home)"; then
        printf '%s\n' "$home_dir/.config/cathook/mode"
        return
    fi

    printf '%s\n' "${CATHOOK_ROOT:-/opt/cathook}/config/mode"
}

cathook_save_mode_preference() {
    local mode="$1"
    local mode_file=""
    local mode_dir=""
    local owner_user="${SUDO_USER:-}"
    local owner_group=""

    mode_file="$(cathook_mode_file)"
    mode_dir="$(dirname -- "$mode_file")"

    if ! mkdir -p "$mode_dir" 2>/dev/null; then
        echo "Could not create $mode_dir; mode choice will only be used for this run." >&2
        return 0
    fi

    if ! printf '%s\n' "$mode" > "$mode_file" 2>/dev/null; then
        echo "Could not write $mode_file; mode choice will only be used for this run." >&2
        return 0
    fi

    if [ -n "$owner_user" ] && command -v id >/dev/null 2>&1; then
        owner_group="$(id -gn "$owner_user" 2>/dev/null || true)"
        if [ -n "$owner_group" ]; then
            chown "$owner_user:$owner_group" "$mode_dir" "$mode_file" 2>/dev/null || true
        fi
    fi
}

cathook_load_mode_preference() {
    local allow_both="${1:-0}"
    local mode_file=""
    local mode=""

    mode_file="$(cathook_mode_file)"
    if [ ! -f "$mode_file" ]; then
        return 1
    fi

    IFS= read -r mode < "$mode_file" || true
    cathook_normalize_mode "$mode" "$allow_both"
}

cathook_prompt_mode() {
    local allow_both="${1:-0}"
    local answer=""
    local mode=""

    if [ ! -t 0 ]; then
        echo "Cathook mode is not set and stdin is not interactive; using default mode." >&2
        printf '%s\n' "default"
        return
    fi

    while true; do
        echo >&2
        echo "Cathook mode is not set yet." >&2
        echo "1) default  - normal SDL/GUI mode" >&2
        echo "2) textmode - textmode binary" >&2
        if [ "$allow_both" = "1" ]; then
            echo "3) both     - build both binaries" >&2
            read -r -p "Choose mode [1/2/3]: " answer
        else
            read -r -p "Choose mode [1/2]: " answer
        fi

        if mode="$(cathook_normalize_mode "$answer" "$allow_both")"; then
            cathook_save_mode_preference "$mode"
            echo "Saved Cathook mode '$mode' to $(cathook_mode_file)." >&2
            printf '%s\n' "$mode"
            return
        fi

        echo "Please choose a valid mode." >&2
    done
}

cathook_select_mode() {
    local allow_both="${1:-0}"
    local mode=""

    if mode="$(cathook_mode_from_env "$allow_both")"; then
        printf '%s\n' "$mode"
        return
    fi

    if mode="$(cathook_load_mode_preference "$allow_both")"; then
        printf '%s\n' "$mode"
        return
    fi

    cathook_prompt_mode "$allow_both"
}

cathook_binary_for_mode() {
    case "$1" in
        default)
            printf '%s\n' "libcathook.so"
            ;;
        textmode)
            printf '%s\n' "libcathooktextmode.so"
            ;;
        *)
            return 1
            ;;
    esac
}


# Fix permissions at the end of the script, but only if we are the main script
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    _fix_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    _fix_repo_root="$_fix_script_dir"
    while [ ! -d "$_fix_repo_root/.git" ] && [ "$_fix_repo_root" != "/" ]; do
        _fix_repo_root="$(dirname -- "$_fix_repo_root")"
    done

    if [ -f "$_fix_repo_root/botpanel/fix_permissions" ]; then
        bash "$_fix_repo_root/botpanel/fix_permissions" --once
    fi
fi

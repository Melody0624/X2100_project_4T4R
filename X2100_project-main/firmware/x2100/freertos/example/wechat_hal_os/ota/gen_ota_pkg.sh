#!/bin/sh

set -e

usage() {
    cat <<'EOF'
Usage:
  gen_ota_pkg.sh rtos=<rtos_file> app=<app_file> userdata=<user_file> \
                 rtos_ver=<rtos_ver> app_ver=<app_ver> pkg=<ota_pkg>

Notes:
  - rtos/app/userdata are optional; at least one payload is recommended.
  - rtos_ver/app_ver default to 2 if not specified.
  - pkg default to ota.bin if not specified.
  - no python dependency, pure shell.

Example:
  ./gen_ota_pkg.sh rtos=zero.bin app=app.bin userdata=user.bin \
      rtos_ver=2 app_ver=3 pkg=ota.bin
EOF
}

rtos=""
app=""
userdata=""
rtos_ver="2"
app_ver="2"
pkg="ota.bin"

for arg in "$@"; do
    case "$arg" in
        rtos=*) rtos="${arg#*=}" ;;
        app=*) app="${arg#*=}" ;;
        userdata=*) userdata="${arg#*=}" ;;
        rtos_ver=*) rtos_ver="${arg#*=}" ;;
        app_ver=*) app_ver="${arg#*=}" ;;
        pkg=*) pkg="${arg#*=}" ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $arg"; usage; exit 1 ;;
    esac
done

if [ -n "$rtos" ] && [ ! -f "$rtos" ]; then
    echo "rtos file not found: $rtos"
    exit 1
fi

if [ -n "$app" ] && [ ! -f "$app" ]; then
    echo "app file not found: $app"
    exit 1
fi

if [ -n "$userdata" ] && [ ! -f "$userdata" ]; then
    echo "userdata file not found: $userdata"
    exit 1
fi

size_of() {
    if [ -z "$1" ]; then
        echo 0
        return
    fi
    wc -c < "$1" | tr -d ' '
}

to_int() {
    if [ -z "$1" ]; then
        echo 0
        return
    fi
    echo $(( $1 ))
}

u32_le() {
    v=$1
    b1=$((v & 0xff))
    b2=$(((v >> 8) & 0xff))
    b3=$(((v >> 16) & 0xff))
    b4=$(((v >> 24) & 0xff))
    esc=$(printf '\\%03o\\%03o\\%03o\\%03o' "$b1" "$b2" "$b3" "$b4")
    printf '%b' "$esc"
}

userdata_size=$(size_of "$userdata")
rtos_size=$(size_of "$rtos")
app_size=$(size_of "$app")

rtos_flags=0
app_flags=0
[ "$rtos_size" -gt 0 ] && rtos_flags=1
[ "$app_size" -gt 0 ] && app_flags=1

hdr_size=$((8 * 4))
rtos_ver_i=$(to_int "$rtos_ver")
app_ver_i=$(to_int "$app_ver")

{
    u32_le "$hdr_size"
    u32_le "$rtos_ver_i"
    u32_le "$app_ver_i"
    u32_le "$rtos_flags"
    u32_le "$app_flags"
    u32_le "$userdata_size"
    u32_le "$rtos_size"
    u32_le "$app_size"
} > "$pkg"

if [ "$userdata_size" -gt 0 ]; then
    cat "$userdata" >> "$pkg"
fi
if [ "$rtos_size" -gt 0 ]; then
    cat "$rtos" >> "$pkg"
fi
if [ "$app_size" -gt 0 ]; then
    cat "$app" >> "$pkg"
fi

total=$((hdr_size + userdata_size + rtos_size + app_size))
echo "ota pkg generated: $pkg (${total} bytes)"

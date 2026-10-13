#!/bin/bash
set -euo pipefail
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "Usage: $0 BUILD_DIR VERSION DISTRO [OUTPUT_DIR]" >&2
    exit 1
fi
DEB_BUILD=$(realpath "$1")
DEB_VERSION=${2#v}
DEB_DISTRO=$3
DEB_OUTPUT=$(realpath -m "${4:-.}")
dpkg --validate-version "$DEB_VERSION"
[[ "$DEB_DISTRO" =~ ^[a-zA-Z0-9._-]+$ ]]
DEB_ARCH=$(dpkg-architecture -qDEB_HOST_ARCH)
DEB_MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
DEB_LIBRARY="$DEB_BUILD/libfprint/libfprint-2.so.2.0.0"
test -f "$DEB_LIBRARY"
DEB_STAGE=$(mktemp -d)
trap 'rm -rf "$DEB_STAGE"' EXIT
DEB_ROOT="$DEB_STAGE/debian/goodix-gf32xx-driver"
mkdir -p "$DEB_ROOT/usr/lib/$DEB_MULTIARCH" "$DEB_ROOT/DEBIAN" "$DEB_STAGE/debian" "$DEB_OUTPUT"
# Runtime overlay only: do not take ownership of distribution headers or GIRs.
install -m 755 "$DEB_LIBRARY" "$DEB_ROOT/usr/lib/$DEB_MULTIARCH/libfprint-2.so.2.0.0"
ln -s libfprint-2.so.2.0.0 "$DEB_ROOT/usr/lib/$DEB_MULTIARCH/libfprint-2.so.2"
cat > "$DEB_STAGE/debian/control" <<EOF
Source: goodix-gf32xx-driver
Maintainer: Goodix Fingerprint Linux Community

Package: goodix-gf32xx-driver
Architecture: any
Description: Goodix GF32xx fingerprint driver runtime
EOF
DEB_DEPENDS=$(cd "$DEB_STAGE" && dpkg-shlibdeps -O -e"debian/goodix-gf32xx-driver/usr/lib/$DEB_MULTIARCH/libfprint-2.so.2.0.0")
[[ "$DEB_DEPENDS" == shlibs:Depends=* ]]
DEB_DEPENDS=${DEB_DEPENDS#shlibs:Depends=}
test -n "$DEB_DEPENDS"
cat > "$DEB_ROOT/DEBIAN/control" <<EOF
Package: goodix-gf32xx-driver
Version: $DEB_VERSION
Architecture: $DEB_ARCH
Maintainer: Goodix Fingerprint Linux Community
Depends: $DEB_DEPENDS
Replaces: libfprint-2-2
Provides: libfprint-2-2
Description: Goodix GF32xx fingerprint driver runtime ($DEB_DISTRO)
 Modified libfprint for USB 27c6:55a4/55b4. Built natively for $DEB_DISTRO.
EOF
cat > "$DEB_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    ldconfig
fi
EOF
cat > "$DEB_ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
case "$1" in
    remove|purge) ldconfig ;;
esac
EOF
chmod 755 "$DEB_ROOT/DEBIAN/postinst" "$DEB_ROOT/DEBIAN/postrm"
DEB_FILE="goodix-gf32xx-driver_${DEB_VERSION}_${DEB_DISTRO}_${DEB_ARCH}.deb"
dpkg-deb --build --root-owner-group "$DEB_ROOT" "$DEB_OUTPUT/$DEB_FILE"
(cd "$DEB_OUTPUT" && sha256sum "$DEB_FILE" > SHA256SUMS)

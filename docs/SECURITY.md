# Security Notes

## PSK (Pre-Shared Key)

The driver uses a **hardcoded community PSK** (`PMK_HASH` / `PSK_WHITE_BOX`) to communicate with the fingerprint sensor. This is a design constraint of the open-source driver — the sensor requires a pre-shared key to establish the TLS session used for image transfer.

### Implications

1. **This key is public.** Anyone who reads the driver source has access to it. The sensor's TLS channel is a **transport encryption** layer, not an authentication or access-control layer. It does not establish a unique host identity, and a public PSK must not be treated as a strong protection boundary for biometric traffic.

2. **Physical access.** An attacker with physical access to the USB bus could:
   - Read the community PSK from the open-source driver
   - Man-in-the-middle the TLS connection
   - Capture or replay fingerprint images

   This threat exists regardless of the PSK mechanism and is inherent to USB bus access.

3. **Dual-boot PSK changes.** When switching between Windows and Linux, each OS writes its own PSK. The community driver detects mismatches and reprovisions the community key. This write is a one-shot USB command (`0xe0`) that takes ~500ms and does not touch enrolled fingerprint data.

### Mitigations

- Keep your system's USB ports physically secure
- Use full-disk encryption to protect fingerprint templates at rest (`/var/lib/fprint/`)
- Stored templates are protected by filesystem permissions and disk encryption, not by the transport PSK

## Fingerprint Templates

This is an image-based, host-matching driver. SIGFM/OpenCV extracts and matches
features on the computer. fprintd stores the enrolled templates under
`/var/lib/fprint/`, normally with owner read/write permissions (`0600`). This
driver does not manage fingerprint templates stored on the sensor MCU.

Normal operation does not save raw image files. Separate debugging and firmware
tools can have different storage behavior.

## OpenSSL TLS-PSK Context

The TLS-PSK connection is established over a **local socketpair** within the fprintd process:

```
fingerprint sensor ←──USB──→ goodixtls ←──socketpair──→ OpenSSL (in-process)
```

- `@SECLEVEL=0` is set on the sensor's TLS context only. This does not affect system-wide OpenSSL security levels.
- The TLS context is destroyed when fprintd closes the device.
- The PSK cipher configuration (`PSK:@SECLEVEL=0`) is not propagated to any other OpenSSL context.

## udev Permissions

The rule grants `0660` access to `27c6:55a4` and `27c6:55b4`, with group
`plugdev`. Group membership grants raw USB access; it does not imply an active
local desktop session. fprintd normally runs as a system daemon, and its D-Bus
operations have separate authorization rules.

## Reporting

Security concerns should be reported via GitHub Issues on [GuNanOvO/goodix-55x4-linux](https://github.com/GuNanOvO/goodix-55x4-linux/issues).

## Firmware Write Exposure

The C driver implements PSK provisioning (`0xe0`). Firmware erase and flashing
are performed by the separate `goodix-fp-dump` project; this driver does not
implement the firmware update commands listed in the historical RE notes.

Raw USB access can still permit a separate tool to modify the device. The udev
rule restricts that access to root and `plugdev` members.

## PSK Race Condition

The driver checks PSK during cold activation and writes on mismatch. It checks
the write response but does not read back the hash afterward. The optional
Python boot helper additionally verifies the hash after writing. Neither path
implements an atomic device-side check-and-write transaction.

"""Regenerate include/ota_ca_bundle.h: the CA list the cloud pull verifies against.

Why this file exists (issue #71). The cloud pull downloads firmware over HTTPS
from GitHub, and GitHub's two hosts present two different chains. Captured from
the live servers on 2026-10-31 with `openssl s_client -showcerts`, which shows
what the server actually transmits (a local trust store's own view hides that):

  api.github.com              leaf *.github.com
                              <- Sectigo Public Server Authentication CA DV E36
                              <- E46 cross-signed by USERTrust ECC Certification
                                 Authority (not the self-signed E46)
  github.com /releases/download/...  (the asset URL in a release manifest)
                              leaf github.com  <- same Sectigo chain
  release-assets.githubusercontent.com and objects.githubusercontent.com (where
  that redirect lands)
                              leaf *.github.io  (SAN covers *.githubusercontent.com)
                              <- YR1 (Let's Encrypt)
                              <- Root YR cross-signed by ISRG Root X1 (not the
                                 self-signed Root YR)

What is compiled in is the two intermediates - E36 and YR1 - because they are what
actually signs GitHub's leaves, and pinning them makes every verification one hop
long (leaf -> anchor, checked with the leaf's own signature algorithm). Root-only
lists were measured on hardware first: with X1 + E46 + USERTrust the manifest leg
verified and the download leg did not, while the same list verifies both chains on
a PC - so the long walk to a root is where this chip gives up, and the RAM cost of
extra pins is not affordable either (issue #70 leaves ~34.8 KB contiguous for one
handshake, and five pins took the biggest free block below that). The roots are
parked, commented, in the list below, with the measurements and the dates.

Because firmware images carry no signature since V1.5.1, this TLS check is the
only thing between the pull path and a stranger on the network supplying the
image - so the fix is to *narrow* trust to a hand-built list, never to switch the
check off.

Usage:
    python scripts/make_ca_bundle.py            # fetch, verify, write the header
    python scripts/make_ca_bundle.py --check    # fetch and verify, write nothing

Every root is fetched from its publisher and, where a second independent copy
exists, from that one too; the script refuses to write the header unless all the
copies agree byte-for-byte AND the SHA-256 of the DER matches the fingerprint
pinned in ROOTS below (fingerprints are the thing you can confirm from a
publication, a second mirror, or a certificate-transparency log - the bytes alone
prove nothing about who they came from). The header it writes is a generated file:
never hand-edit it (AGENTS.md rule 15).

Adding a root: add a row to ROOTS with the fingerprint you verified, run the
script, rebuild. Expiry: when the pull starts reporting a chain this list does
not validate, this is the file to regenerate - and the unit says so in its log
line and in "ca_bundle" on GET /api/ota/check.
"""
import base64
import hashlib
import re
import sys
import urllib.request

HEADER = "include/ota_ca_bundle.h"

# name, publisher URLs (all must agree), pinned SHA-256 of the DER, comment on
# how that fingerprint was cross-checked.
ROOTS = [
    # Parked roots - kept here (commented) so a rotation is a copy-paste away.
    # They are NOT compiled in, for two measured reasons; see the note under the
    # active entries below the list.
    #
    # {
    #     "name": "ISRG Root X1",
    #     "urls": ["https://letsencrypt.org/certs/isrgrootx1.pem"],
    #     "sha256": "96BCEC06264976F37460779ACF28C5A7CFE8A3C0AAE11A8FFCEE05C0BDDF08C6",
    #     "cross_check": "fingerprint and serial 8210CFB0D240E3594463E0BB63828B00 "
    #                    "confirmed against letsencrypt.org/certs/ (fetched 2026-10-31); "
    #                    "this is the root every ISRG chain has led to since 2015, valid "
    #                    "to 2035-06-04",
    # },
    # {
    #     "name": "Sectigo Public Server Authentication Root E46",
    #     "urls": [
    #         "http://crt.sectigo.com/SectigoPublicServerAuthenticationRootE46.crt",  # DER
    #         "https://sectigo.tbs-certificats.com/SectigoPublicServerAuthenticationRootE46.crt",  # PEM copy
    #     ],
    #     "sha256": "C90F26F0FB1B4018B22227" "519B5CA2B53E2CA5B3BE5CF18EFE1BEF47" "380C5383",
    #     "cross_check": "serial 42F2CCDA1B6937445F15FE752810B8F4, ECDSA secp384r1, "
    #                    "valid 2021-03-22..2046-03-21, subject C=GB O=Sectigo Limited - "
    #                    "agrees byte-for-byte between Sectigo's own DER and an "
    #                    "independently maintained PEM mirror (both fetched 2026-10-31)",
    # },
    # {
    #     "name": "USERTrust ECC Certification Authority",
    #     "urls": [
    #         "http://crt.comodoca.com/USERTrustECCCertificationAuthority.crt",
    #         "http://crt.usertrust.com/USERTrustECCCertificationAuthority.crt",
    #     ],
    #     "sha256": "4FF460D54B9C86DABFBCFC5712E0400D" "2BED3FBC4D4FBDAA86E06ADCD2A9AD7A",
    #     "cross_check": "serial 5C8B99C55A94C5D27156DECD8980CC26, valid "
    #                    "2010-02-01..2038-01-18, subject C=US ST=New Jersey L=Jersey "
    #                    "City O=The USERTRUST Network - fetched byte-identical from two "
    #                    "publisher-controlled AIA endpoints (both fetched 2026-10-31)",
    # },
    # The two intermediates GitHub's servers actually sign leaves with. This is
    # where the issue #71 diagnosis landed, so the measurements are recorded here:
    #   - What the servers transmit (captured 2026-10-31 with `openssl s_client
    #     -showcerts`, which shows the chain as sent, not the one a PC's store
    #     builds for itself):
    #       api.github.com and github.com: leaf <- E36 <- E46 cross-signed by
    #         USERTrust ECC CA (github.com's is the asset-URL redirect hop)
    #       release-assets.githubusercontent.com and objects.githubusercontent.com
    #         (where that redirect lands): leaf *.github.io <- YR1 <- Root YR
    #         cross-signed by ISRG Root X1
    #   - Roots-only list (X1 + E46 + USERTrust): the manifest leg verifies on the
    #     device, the asset-download leg does not - even though the same list
    #     verifies both chains on a PC (`openssl verify -CAfile <this list>
    #     -untrusted <chain-as-sent>` says OK for both). The one thing the failing
    #     leg needs that the working leg does not is the RSA-4096/SHA-384 signature
    #     ISRG Root X1 makes over Root YR, so that hop is the suspect, and pinning
    #     YR1 removes it: the walk becomes leaf -> anchor, signed with the leaf's
    #     own algorithm (RSA-2048/SHA-256 for the LE leaf, ECDSA P-256/SHA-256 for
    #     the Sectigo one).
    #   - List size costs RAM: with five pinned certs the biggest free block at the
    #     moment the download client is created was 34,804 B, under the ~34,816 B
    #     this core needs for one handshake (issue #70) - so that build fails for
    #     RAM, not for trust, and proves nothing. Three roots measured 45,044 B.
    #     Hence: two pinned intermediates, roots commented out, and regenerate
    #     rather than accumulate.
    #   Both pinned intermediates expire (YR1 2028-09-02, E36 2036-03-21). The unit
    #   says so in its log and in "ca_bundle" on /api/ota/check; regenerating this
    #   header is one command.
    {
        "name": "YR1 (Let's Encrypt intermediate)",
        "urls": ["http://yr1.i.lencr.org/"],
        "sha256": "13949634D99CD6FD6AA80BC034FEFACC" "EB1969FEEF986586713ECDBB05758D3F",
        "cross_check": "byte-identical to the certificate the live chain presents for "
                       "release-assets.githubusercontent.com and objects.githubusercontent.com "
                       "(captured 2026-10-31); serial A20253F15F2691C05DC1CE13B9BCCA4E, "
                       "valid 2025-09-03..2028-09-02, signed by ISRG Root YR",
    },
    {
        "name": "Sectigo Public Server Authentication CA DV E36",
        "urls": ["http://crt.sectigo.com/SectigoPublicServerAuthenticationCADVE36.crt"],
        "sha256": "873F0BA80E3AC222656DFD04158CC15C" "2927D42D5D05F01DEE4A47EB43A916DF",
        "cross_check": "byte-identical to the certificate the live chain presents for "
                       "api.github.com and github.com (captured 2026-10-31); "
                       "valid 2021-03-22..2036-03-21",
    },
    # Ready-to-use rows for the Let's Encrypt "Generation Y" roots, published at
    # letsencrypt.org/certs/gen-y/. They matter if GitHub ever stops sending the
    # cross-signature that links Root YR to ISRG Root X1: at that point the chain
    # under YR1 dead-ends for us, and the fix is to uncomment the self-signed
    # Root YR row below (and, if RAM allows, drop one pinned intermediate to pay
    # for it). Keeping the list this short is a hard constraint, not a preference:
    # every extra certificate costs parsing heap on a path that already needs
    # ~34.8 KB contiguous (issue #70), so it stays as long as what is observed.
    #
    # {"name": "ISRG Root YR (self-signed)",
    #  "urls": ["https://letsencrypt.org/certs/gen-y/root-yr.pem"],
    #  "sha256": "E57B7E6F150C419102E8D5C055729FF9" "67B9D1A829BF00CEC89CA604EBF4A86F",
    #  "cross_check": "serial EC46349360CF4B0FF8A982D93AA9CA3D matches "
    #                 "letsencrypt.org/certs/gen-y/root-yr.txt (fetched 2026-10-31)"},
    # {"name": "ISRG Root YR (cross-signed by ISRG Root X1)",
    #  "urls": ["https://letsencrypt.org/certs/gen-y/root-yr-by-x1.pem"],
    #  "sha256": "072639D0B140D5BFFAE16AD9C3F6CC60" "86040621F51EE61A6D46A8915C07CF76",
    #  "cross_check": "serial F24B6D17F9D9AD7CB1C9FEA78782699F matches "
    #                 "letsencrypt.org/certs/gen-y/root-yr-by-x1.txt (fetched 2026-10-31)"},
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "dpp-ca-bundle/1.0"})
    with urllib.request.urlopen(req, timeout=40) as r:
        return r.read()


def to_der(raw):
    """Accept DER or PEM (the same certificate travels both ways) and return DER."""
    if b"-----BEGIN CERTIFICATE-----" in raw[:200]:
        body = re.search(rb"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----",
                         raw, re.S)
        if not body:
            raise ValueError("PEM block not found")
        return base64.b64decode(body.group(1))
    return raw


def pem_wrap(der):
    b64 = base64.encodebytes(der).decode("ascii")
    return "-----BEGIN CERTIFICATE-----\n" + b64 + "-----END CERTIFICATE-----\n"


def main():
    check_only = "--check" in sys.argv
    chunks, info_lines, rows = [], [], []
    for root in ROOTS:
        ders = {}
        for url in root["urls"]:
            try:
                ders[url] = to_der(fetch(url))
            except Exception as exc:  # noqa: BLE001 - report and stop, don't guess
                print(f"FAIL  {root['name']}: {url} -> {exc}")
                return 1
        fps = {hashlib.sha256(d).hexdigest().upper(): (len(d), u)
               for u, d in ders.items()}
        if len(fps) != 1:
            print(f"FAIL  {root['name']}: the copies disagree: {fps}")
            return 1
        fp = next(iter(fps))
        if fp != root["sha256"]:
            print(f"FAIL  {root['name']}: fetched SHA-256 {fp} does not match the "
                  f"pinned {root['sha256']} - nothing was written")
            return 1
        der = ders[root["urls"][0]]
        chunks.append(pem_wrap(der))
        short = root["name"]
        info_lines.append(f"{short} ({fp[:12]}..., {len(der)} B)")
        rows.append((short, root, fp, len(der)))
        print(f"ok    {short}: {len(der)} B DER, SHA-256 {fp}")

    info = "; ".join(info_lines)
    if check_only:
        print(f"--check: {len(rows)} root(s) verified, header not written")
        print(f"        info line the header would carry: {info}")
        return 0

    pem_text = "".join(chunks)
    out = []
    out.append("/* GENERATED by scripts/make_ca_bundle.py - DO NOT EDIT, DO NOT COMMIT BY HAND. */")
    out.append("/* Regenerate with:  python scripts/make_ca_bundle.py            (then rebuild)")
    out.append("                    python scripts/make_ca_bundle.py --check    (verify only)")
    out.append(" *")
    out.append(" * The certificate authorities the cloud pull verifies GitHub against.")
    out.append(" * Firmware images carry no signature since V1.5.1, so this TLS check is the")
    out.append(" * one thing standing between the pull path and a stranger on the network")
    out.append(" * handing us an image. Narrowing this list is safe; switching the check off")
    out.append(" * (setInsecure) is not - see AGENTS.md rule 3 and issue #71.")
    out.append(" *")
    out.append(" * Verified against pinned fingerprints, not against whatever a live")
    out.append(" * connection happened to present. Sources and cross-checks:")
    for short, root, fp, size in rows:
        out.append(f" *   {short} - {size} B DER, SHA-256 {fp}")
        for url in root["urls"]:
            out.append(f" *       fetched from {url}")
        out.append(f" *       {root['cross_check']}")
    out.append(" */")
    out.append("#pragma once")
    out.append("")
    out.append("// One PEM per trusted root, concatenated: mbedTLS parses every certificate in")
    out.append("// the buffer, so this is a list and not a single CA. Kept in flash (PROGMEM is")
    out.append("// a no-op on the ESP32) - NetworkClientSecure::setCACert() stores the pointer,")
    out.append("// it does not copy the text, so the only runtime cost is the parse itself.")
    out.append(f"// {info}")
    out.append('static const char OTA_CA_BUNDLE[] = R"PEM(')
    out.append(pem_text.rstrip("\n"))
    out.append(')PEM";')
    out.append("")
    out.append("// The same list as one short line, for the serial log and GET /api/ota/check,")
    out.append("// so 'this build's CA list is out of date' is a sentence you can read instead")
    out.append("// of a mystery handshake failure.")
    out.append(f'static const char OTA_CA_BUNDLE_INFO[] = "{info}";')
    out.append("")

    with open(HEADER, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))
    print(f"wrote {HEADER}: {len(rows)} root(s), {len(pem_text)} bytes of PEM")
    return 0


if __name__ == "__main__":
    sys.exit(main())

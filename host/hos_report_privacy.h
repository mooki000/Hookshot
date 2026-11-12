/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>

/* Report labels only, not a general log scrubber. The menu's log redactor also
   replaces home/user paths (gpu/vk/vk_bug_report.cpp); pending records are stricter:
   a label containing an identity, path, address or control byte is omitted whole.
   Call init at normal startup. redact uses only caller-owned, bounded storage. */
typedef struct HosReportPrivacy {
    char home[4096], user[256], host[256];
    size_t sizes[3];
    int incomplete;
} HosReportPrivacy;

static inline void hos_report_privacy_init(HosReportPrivacy *p, const char *home,
                                           const char *user, const char *host) {
    const char *src[3] = {home, user, host};
    char *dst[3] = {p->home, p->user, p->host};
    const size_t caps[3] = {sizeof p->home, sizeof p->user, sizeof p->host};
    p->incomplete = 0;
    for (int k = 0; k < 3; ++k) {
        size_t n = 0;
        if (src[k]) {
            while (n + 1 < caps[k] && src[k][n]) { dst[k][n] = src[k][n]; ++n; }
            if (src[k][n]) p->incomplete = 1; /* fail closed, not a partial identity */
        }
        dst[k][n] = 0;
        p->sizes[k] = n;
    }
}

static inline size_t hos_report_privacy_redact(const HosReportPrivacy *p, char *out,
                                               size_t cap, const char *text, size_t size) {
    const char *ids[3] = {p->home, p->user, p->host};
    int private_label = p->incomplete;
    size_t n = 0;
    if (!text) size = 0;
    while (n < size && text[n]) ++n;
    for (size_t i = 0; i < n && !private_label; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '@') private_label = 1;
        /* Dotted numeric labels can be IP addresses. Module names have letters. */
        if (c >= '0' && c <= '9') {
            size_t j = i;
            unsigned dots = 0;
            while (j < n && ((text[j] >= '0' && text[j] <= '9') || text[j] == '.')) {
                dots += text[j] == '.';
                ++j;
            }
            if (dots >= 3) private_label = 1;
        }
        for (int k = 0; k < 3; ++k) {
            const size_t len = p->sizes[k];
            if (!len || len > n - i) continue;
            size_t j = 0;
            for (; j < len; ++j) {
                unsigned char a = (unsigned char)text[i + j], b = (unsigned char)ids[k][j];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) break;
            }
            if (j == len) private_label = 1;
        }
    }
    if (private_label) text = "redacted", n = 8;
    if (!cap) return 0;
    if (n >= cap) n = cap - 1;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)text[i];
        out[i] = c < 128 ? (char)c : '?'; /* records remain plain UTF-8 even after a byte bound */
    }
    out[n] = 0;
    return n;
}

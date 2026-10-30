# S5120V2-LI log data

`samples.log` contains synthetic values inserted into message formats documented
by H3C. It is suitable for parser and USB transport tests, but it is not a log
capture from a production switch.

Comware messages are identified by this stable tuple:

```text
MODULE / severity / MNEMONIC
```

For example, `IFNET/3/PHY_UPDOWN` means module `IFNET`, severity `3` (error),
and mnemonic `PHY_UPDOWN`. Parsers should use the tuple rather than matching
only the English message text.

Severity values:

| Value | Name |
| ---: | --- |
| 0 | Emergency |
| 1 | Alert |
| 2 | Critical |
| 3 | Error |
| 4 | Warning |
| 5 | Notification |
| 6 | Informational |
| 7 | Debug |

Before adding a captured log:

1. Remove management IPs, usernames, serial numbers, MAC addresses, and site
   names.
2. Record the exact output of `display version` separately.
3. Keep the original line unchanged and put normalized metadata in
   `catalog.json`.

# zenbook-hid-irq-fix

The touchpads installed in the ASUS ZenBook UM5302TA tend to flood the
OS with thousands of spurious interrupts, which causes unnecessary
battery drain. This Linux kernel module addresses the issue by
monitoring the interrupts and automatically soft-resetting the
touchpad when necessary.

## Installation

Clone the repo under `/usr/src/zenbook-hid-irq-fix`. Install
`dkms` (eg. `dnf install dkms`). Run:

```
dkms add -m zenbook-hid-irq-fix -v 1.0
dkms build -m zenbook-hid-irq-fix -v 1.0
dkms install -m zenbook-hid-irq-fix -v 1.0
```

Make the module load at boot time:

```
echo zenbook-hid-irq-fix > /etc/modules-load.d/zenbook-hid-irq-fix.conf
```

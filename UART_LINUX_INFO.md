# UART16550, OpenSBI, and Linux Interaction Guide

## 1. Brief intro to OpenSBI (UART / console context)

OpenSBI is the RISC‑V firmware layer that sits below the operating system and provides:

- Mandatory **SBI services** (time, IPIs, CPU‑management, reset).
- A **portable console layer** (`sbi_console_write`‑style interface) that can be backed by hardware such as UART16550.

In the **UART/console context**, OpenSBI typically:

- Uses a **UART16550‑compatible driver** in M‑mode to:
  - Initialize UART clock, baud, and basic control registers.
  - Implement `sbi_console_putchar` / `sbi_console_write` for early debug and console output.
- Exposes this as a **firmware‑controlled console** that Linux can reuse via `earlycon=sbi` and `console=hvc0`.

OpenSBI does **not** expose generic UART‑control ecalls (e.g., “set‑baud‑on‑this UART”); it just owns the UART for **console‑only** or **firmware‑debug** purposes, and leaves normal device‑driver operations to Linux.

---

## 2. UART16550, Linux, and OpenSBI roles

### UART16550 hardware

- A memory‑mapped **16550‑compatible UART** with registers at stride `0, 1, 2, ...` (or `<< reg-shift` offsets).
- Connected to the **PLIC** (or similar interrupt controller) for RX/TX interrupts.
- Wired to the same physical UART line as the host terminal (e.g., your serial port on the host).

Both **OpenSBI** and **Linux** can access the **same UART16550 registers**, but they do so for different purposes and at different times.

---

### OpenSBI UART / Console driver

OpenSBI’s UART driver (often enabled via `select SERIAL_UART8250` in the platform config):

- **Statically configures** the UART:
  - Base address from device tree or platform definition.
  - Fixed baud rate, word length, and basic control (e.g., enable DLAB, clocks, FIFOs if supported).
- **Implements a console API**:
  - `sbi_console_write` calls into the UART driver and **polls** the transmitter FIFO / `THRE` bit to send bytes.
  - Optional SBI ecall for reading characters (less common; most SBI consoles are write‑only).
- **Does not normally use UART interrupts**:
  - UART IRQs are left for Linux to handle in S‑mode, if the UART is also used as a Linux serial port.

If Linux uses `earlycon=sbi` / `console=hvc0`, all **console output** goes through this SBI‑based path:

- `printk` → `hvc0` → SBI ecall → OpenSBI UART driver → UART16550 TX.

---

### Linux UART driver (`8250`‑based)

Linux uses the standard `8250`‑compatible driver (`serial8250_`) to drive the UART16550 as a Linux serial port (e.g., `ttyS0`):

- Discovers the UART from a device‑tree node like:

  ```dts
  uart16550: serial@40000000 {
      compatible = "ns16550a";
      reg = <0x40000000 0x100>;
      reg-shift = <0>;
      reg-io-width = <1>;
      clock-frequency = <50000000>;

      interrupt-parent = <&plic>;
      interrupts = <16>;
  };
  ```

- **Owns the UART IRQ**:
  - RX/TX interrupts are delivered to a PLIC S‑context, handled by the `8250` RX/TX IRQ handlers.
  - This covers normal serial‑port usage, `getty`, `socat`, etc.

Linux’s UART driver can:

- **Use the UART as a console** (via `console=ttyS0` or `earlycon=uart8250`).
- **Run alongside OpenSBI**’s UART console, but **not** simultaneously in fully independent ways without coordination.

---

## 3. Three common usage patterns

### Pattern A: SBI console only, no Linux serial port

- OpenSBI initializes UART and exposes SBI console.
- Linux uses `earlycon=sbi` and `console=hvc0`.
- Linux **does not** probe the UART as a `8250` device (or its probe is suppressed).
- UART is **entirely owned by OpenSBI**; `ttyS0` does not exist.

In this case:

- No UART IRQs are used by Linux.
- Baud rate, etc., must be set **solely in OpenSBI / platform code**.

---

### Pattern B: SBI console + Linux `ttyS0` (shared UART)

- OpenSBI initializes UART and exposes SBI console for `hvc0`.
- Linux probe for `8250` is **enabled**, so:
  - `hvc0` → SBI console API → UART16550 TX (poll).
  - `ttyS0` → `8250` driver → UART16550 (RX IRQ, possibly TX).

Typical setup:

- `earlycon=sbi`
- `console=ttyS0`
- `hvc0` is still available for hypervisor‑style output.

In this case:

- **Both directions are possible**, but:
  - RX data typically goes to `ttyS0`’s buffer.
  - Console output can go to both `hvc0` and `ttyS0` if you have multiple `console=` lines.
- **Baud‑rate changes on `ttyS0` break `hvc0`**:
  - If Linux changes the UART’s baud (e.g., via `stty`), OpenSBI still uses its old configuration, so `hvc0` output becomes garbage or vanishes.

---

### Pattern C: Linux‑only UART, no SBI console

- OpenSBI may still initialize the UART minimally, or leave it entirely to Linux.
- Linux uses `console=ttyS0` or `earlycon=uart8250,mmio32,0x40000000`.
- OpenSBI does **not** expose an SBI console on that UART.
- UART IRQs and configuration are **entirely under Linux’s control**.

---

## 4. Interrupts and synchronization

### UART interrupts

- UART16550 RX/TX interrupts are:
  - Handled by Linux’s `8250` driver in **S‑mode**, if the UART is also used as `ttyS0`.
  - **Not used by OpenSBI**; SBI console is typically **poll‑based**.

So:

- **OpenSBI never sees the UART IRQs** in a normal setup.
- Linux sees UART IRQs if the UART is also exposed as `ttyS0`.

### Concurrent access hazards

If you actively use both:

- `hvc0` (SBI‑based console).
- `ttyS0` (Linux `8250`‑based serial port).

on the **same UART16550**:

- **Writable registers** (e.g., `LCR`, `DLL/DLH`) must be **the same** for both, otherwise:
  - Output from `hvc0` is at the wrong baud/frame and appears garbled.
- **Baud‑rate changes on `ttyS0` break `hvc0`**:
  - The UART hardware is reconfigured, but OpenSBI is unaware and continues using its old divisor.
- **RX reading**:
  - If Linux is reading via `ttyS0`, OpenSBI‑level RX (if any) will see only the subset of bytes not already claimed by the Linux driver.
  - This is effectively a **race**, and mixing both RX paths is not recommended.

Recommended practice:

- Choose **one**:
  - Either `hvc0` as the console, with `ttyS0` minimized or disabled, or
  - `ttyS0` as the console, and `hvc0` disabled or unused.
- Avoid **dynamic baud‑rate changes** on the UART after boot if `hvc0` is in use.

---

## 5. Console configuration examples

### Example 1: SBI‑only console

```dts
&uart16550 {
    /* configured for OpenSBI UART console */
};

/* Linux boot args: */
earlycon=sbi
console=hvc0
```

- OpenSBI owns the UART.
- No Linux `ttyS0` unless explicitly re‑enabled.

---

### Example 2: Mixed SBI console and `ttyS0`

```dts
uart16550: serial@40000000 {
    compatible = "ns16550a";
    reg = <0x40000000 0x100>;
    reg-shift = <0>;
    reg-io-width = <1>;
    clock-frequency = <50000000>;

    interrupt-parent = <&plic>;
    interrupts = <16>;
};
```

Linux boot args:

```bash
earlycon=sbi
console=ttyS0
```

- `earlycon=sbi` uses OpenSBI during very early boot.
- `console=ttyS0` takes over later, giving priority to the Linux serial driver.
- `hvc0` may still exist for hypervisor‑style logging, but UART configuration must match everywhere.

---

## 6. Summary

- **OpenSBI**:
  - Uses UART16550 as a **firmware console**, typically via **poll‑mode output** and no UART IRQs.
  - Configures baud and registers once at boot; no runtime re‑configuration from Linux.
- **Linux `8250`**:
  - Drives the UART as a normal serial port, owns UART IRQs, and can change baud dynamically.
  - Can coexist with `hvc0`, but only if UART configuration is aligned.
- **Shared UART**:
  - Avoid changing UART parameters (baud, `LCR`, etc.) from Linux if `hvc0` is in use.
  - Prefer to **pick one console** (`hvc0` or `ttyS0`) as the primary path and let the other be optional or disabled.

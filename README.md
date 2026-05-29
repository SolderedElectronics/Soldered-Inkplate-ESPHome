# Soldered Inkplate ESPHome

ESPHome external component for Inkplate e-paper displays.

> [!IMPORTANT]
> This repo will be migrated to the official ESPHome repo, until then use this one.

## Supported boards

| Model | Resolution | Colors | Partial update | MCU | PSRAM |
|-------|-----------|--------|----------------|-----|-------|
| Inkplate 13 | 1200 × 1600 | 6-color ACeP | Yes | ESP32-S3 | Octal, 80 MHz |
| Inkplate 6 Color | 600 × 448 | 7-color ACeP | No | ESP32 | Quad, 40 MHz |
| Inkplate 2 | 104 × 212 | Black / White / Red | No | ESP32 | Quad, 40 MHz |

> [!WARNING]
> PSRAM is required — the framebuffer is too large for internal RAM. Inkplate 13 needs ~960 KB (octal PSRAM on ESP32-S3). Inkplate 2 and 6 Color need ~27 KB and ~135 KB respectively (quad PSRAM on ESP32).

## Usage

Add to your ESPHome `.yaml`:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/SolderedElectronics/Soldered-Inkplate-ESPHome
      ref: main
```

Then declare the display:

```yaml
esphome:
  name: my-inkplate
  on_boot:
    priority: -100    # run after all components initialised
    then:
      - component.update: my_display

spi:
  clk_pin: GPIOX
  mosi_pin: GPIOX    # MISO not needed — display is write-only

display:
  platform: inkplate_spi
  model: inkplate13         # inkplate13 | inkplate6color | inkplate2
  id: my_display
  update_interval: never    # trigger manually; use a time interval for auto-refresh
  lambda: |-
    it.fill(Color(255, 255, 255));
    it.print(10, 10, id(my_font), "Hello!");
```

The `on_boot` trigger is needed because the display does not draw automatically on startup — `component.update` kicks off the first refresh after all components have initialised.

`update_interval: never` is recommended for battery-powered use and for ACeP panels where refresh rate must be controlled. Use a `time` component or `interval:` block to trigger updates instead.

### Configuration options

| Key | Required | Default | Description |
|-----|----------|---------|-------------|
| `model` | yes | — | Board model (see table above) |
| `update_interval` | no | — | How often to trigger a full refresh |
| `full_update_every` | no | `1` | Trigger full refresh every N updates |
| `rotation` | no | `0°` | Display rotation (0 / 90 / 180 / 270) |
| `lambda` | no | — | Drawing lambda |

Pin defaults match the stock Inkplate hardware. Override any pin by adding e.g. `pin_rst: GPIO4` to the display block.

> [!CAUTION]
> Inkplate 13 enforces a **30 second minimum** between full refreshes. The component will reject a shorter `update_interval` at config validation time. ACeP panels can be permanently damaged by too-frequent refreshes.

### Inkplate 13 — partial update

Inkplate 13 supports refreshing a subregion without a full-panel redraw:

```cpp
// inside a lambda or interval action
if (!id(my_display).is_busy()) {
    id(my_display).filled_rectangle(x, y, w, h, Color(255, 255, 255));
    id(my_display).print(x, y, id(my_font), "updated");
    id(my_display).display_partial(x, y, w, h);
}
```

---

### About Soldered

<img src="https://raw.githubusercontent.com/SolderedElectronics/Soldered-Inputronic-Grid-Arduino-Library/dev/extras/Soldered-logo-color.png" alt="soldered-logo" width="500"/>

At Soldered, we design and manufacture a wide selection of electronic products to help you turn your ideas into acts and bring you one step closer to your final project. Our products are intented for makers and crafted in-house by our experienced team in Osijek, Croatia. We believe that sharing is a crucial element for improvement and innovation, and we work hard to stay connected with all our makers regardless of their skill or experience level. Therefore, all our products are open-source. Finally, we always have your back. If you face any problem concerning either your shopping experience or your electronics project, our team will help you deal with it, offering efficient customer service and cost-free technical support anytime. Some of those might be useful for you:

- [Web Store](https://www.soldered.com/shop)
- [Tutorials & Projects](https://soldered.com/learn)
- [Community & Technical support](https://soldered.com/community)


### Open-source license

Soldered invests vast amounts of time into hardware & software for these products, which are all open-source. Please support future development by buying one of our products.

Check license details in the LICENSE file. Long story short, use these open-source files for any purpose you want to, as long as you apply the same open-source licence to it and disclose the original source. No warranty - all designs in this repository are distributed in the hope that they will be useful, but without any warranty. They are provided "AS IS", therefore without warranty of any kind, either expressed or implied. The entire quality and performance of what you do with the contents of this repository are your responsibility. In no event, Soldered (TAVU) will be liable for your damages, losses, including any general, special, incidental or consequential damage arising out of the use or inability to use the contents of this repository.

## Have fun!

And thank you from your fellow makers at Soldered Electronics.

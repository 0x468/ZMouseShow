# ZMouseShow branding

The production icon uses the A01 **Focus Z** direction. It keeps the Z-series
navy rounded square and cyan-to-blue ribbon while adding a locator focus.

- `zmouse-app.ico`: application/taskbar icon, 16–256 px.
- `zmouse-tray-light.ico`: light glyph for dark notification areas.
- `zmouse-tray-dark.ico`: dark glyph for light notification areas.
- `zmouse-app-icon.svg` and `zmouse-app-icon-small.svg`: vector masters.
- `zmouse-tray-light.svg` and `zmouse-tray-dark.svg`: tray vector masters.
- `zmouse-app-icon.png`: 256 px preview.

Regenerate the checked-in assets from the repository root:

```powershell
python tools/generate_icons.py
```

The generator requires Pillow. Small icon frames intentionally omit the outer
locator ticks at 20 px and below so the silhouette stays readable.

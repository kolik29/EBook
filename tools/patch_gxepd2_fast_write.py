from pathlib import Path

Import("env")

PATCH_MARKER = "// EBook fast row write patch"

OLD_BLOCK = """  _writeCommand(0x13);
  for (uint16_t i = 0; i < h1; i++)
  {
    for (uint16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      uint16_t idx = mirror_y ? j + dx / 8 + uint16_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint16_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _writeData(data);
    }
  }
  _writeCommand(0x92); // partial out
"""

NEW_BLOCK = """  _writeCommand(0x13);
  {PATCH_MARKER}
  if (!invert && !mirror_y && !pgm)
  {
    const uint16_t row_bytes = w1 / 8;
    for (uint16_t i = 0; i < h1; i++)
    {
      const uint32_t idx = dx / 8 + uint32_t(i + dy) * wb;
      _writeData(bitmap + idx, row_bytes);
    }
  }
  else
  {
    for (uint16_t i = 0; i < h1; i++)
    {
      for (uint16_t j = 0; j < w1 / 8; j++)
      {
        uint8_t data;
        // use wb, h of bitmap for index!
        uint16_t idx = mirror_y ? j + dx / 8 + uint16_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint16_t(i + dy) * wb;
        if (pgm)
        {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
          data = pgm_read_byte(&bitmap[idx]);
#else
          data = bitmap[idx];
#endif
        }
        else
        {
          data = bitmap[idx];
        }
        if (invert) data = ~data;
        _writeData(data);
      }
    }
  }
  _writeCommand(0x92); // partial out
""".replace("{PATCH_MARKER}", PATCH_MARKER)


def patch_source():
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    env_name = env.subst("$PIOENV")
    source_path = libdeps_dir / env_name / "GxEPD2_4G" / "src" / "epd" / "GxEPD2_750_T7.cpp"

    if not source_path.exists():
        print("GxEPD2 fast write patch skipped: source not found")
        return

    source = source_path.read_text(encoding="utf-8")

    if PATCH_MARKER in source:
        return

    if OLD_BLOCK not in source:
        print("GxEPD2 fast write patch skipped: expected block not found")
        return

    source_path.write_text(source.replace(OLD_BLOCK, NEW_BLOCK), encoding="utf-8")
    print("GxEPD2 fast write patch applied")


patch_source()

param(
    [string]$OutputPath = "src/fonts/BookSerifCp1251.h",
    [string]$FontFamilyName = "Verdana"
)

Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"

$cp1251 = @{}
for ($i = 0x20; $i -le 0x7E; $i++) { $cp1251[$i] = $i }

$extended = @{
    0x80 = 0x0402; 0x81 = 0x0403; 0x82 = 0x201A; 0x83 = 0x0453
    0x84 = 0x201E; 0x85 = 0x2026; 0x86 = 0x2020; 0x87 = 0x2021
    0x88 = 0x20AC; 0x89 = 0x2030; 0x8A = 0x0409; 0x8B = 0x2039
    0x8C = 0x040A; 0x8D = 0x040C; 0x8E = 0x040B; 0x8F = 0x040F
    0x90 = 0x0452; 0x91 = 0x2018; 0x92 = 0x2019; 0x93 = 0x201C
    0x94 = 0x201D; 0x95 = 0x2022; 0x96 = 0x2013; 0x97 = 0x2014
    0x98 = 0x003F; 0x99 = 0x2122; 0x9A = 0x0459; 0x9B = 0x203A
    0x9C = 0x045A; 0x9D = 0x045C; 0x9E = 0x045B; 0x9F = 0x045F
    0xA0 = 0x00A0; 0xA1 = 0x040E; 0xA2 = 0x045E; 0xA3 = 0x0408
    0xA4 = 0x00A4; 0xA5 = 0x0490; 0xA6 = 0x00A6; 0xA7 = 0x00A7
    0xA8 = 0x0401; 0xA9 = 0x00A9; 0xAA = 0x0404; 0xAB = 0x00AB
    0xAC = 0x00AC; 0xAD = 0x002D; 0xAE = 0x00AE; 0xAF = 0x0407
    0xB0 = 0x00B0; 0xB1 = 0x00B1; 0xB2 = 0x0406; 0xB3 = 0x0456
    0xB4 = 0x0491; 0xB5 = 0x00B5; 0xB6 = 0x00B6; 0xB7 = 0x00B7
    0xB8 = 0x0451; 0xB9 = 0x2116; 0xBA = 0x0454; 0xBB = 0x00BB
    0xBC = 0x0458; 0xBD = 0x0405; 0xBE = 0x0455; 0xBF = 0x0457
}

foreach ($key in $extended.Keys) { $cp1251[$key] = $extended[$key] }
for ($i = 0xC0; $i -le 0xFF; $i++) { $cp1251[$i] = 0x0410 + ($i - 0xC0) }

$fontSpecs = @(
    @{ Name = "BookSerifCp1251Regular9pt"; PixelSize = 17; Style = [System.Drawing.FontStyle]::Regular },
    @{ Name = "BookSerifCp1251Bold9pt"; PixelSize = 17; Style = [System.Drawing.FontStyle]::Bold },
    @{ Name = "BookSerifCp1251Italic9pt"; PixelSize = 17; Style = [System.Drawing.FontStyle]::Italic },
    @{ Name = "BookSerifCp1251BoldItalic9pt"; PixelSize = 17; Style = [System.Drawing.FontStyle]::Bold -bor [System.Drawing.FontStyle]::Italic },
    @{ Name = "BookSerifCp1251Regular12pt"; PixelSize = 23; Style = [System.Drawing.FontStyle]::Regular },
    @{ Name = "BookSerifCp1251Bold12pt"; PixelSize = 23; Style = [System.Drawing.FontStyle]::Bold },
    @{ Name = "BookSerifCp1251Italic12pt"; PixelSize = 23; Style = [System.Drawing.FontStyle]::Italic },
    @{ Name = "BookSerifCp1251BoldItalic12pt"; PixelSize = 23; Style = [System.Drawing.FontStyle]::Bold -bor [System.Drawing.FontStyle]::Italic },
    @{ Name = "BookSerifCp1251Regular18pt"; PixelSize = 34; Style = [System.Drawing.FontStyle]::Regular },
    @{ Name = "BookSerifCp1251Bold18pt"; PixelSize = 34; Style = [System.Drawing.FontStyle]::Bold },
    @{ Name = "BookSerifCp1251Italic18pt"; PixelSize = 34; Style = [System.Drawing.FontStyle]::Italic },
    @{ Name = "BookSerifCp1251BoldItalic18pt"; PixelSize = 34; Style = [System.Drawing.FontStyle]::Bold -bor [System.Drawing.FontStyle]::Italic }
)

function New-MeasureGraphics {
    $bitmap = [System.Drawing.Bitmap]::new(1, 1, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.PageUnit = [System.Drawing.GraphicsUnit]::Pixel
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

    return @{
        Bitmap = $bitmap
        Graphics = $graphics
    }
}

function Add-ByteArrayLiteral {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$Name,
        [System.Collections.Generic.List[byte]]$Bytes
    )

    [void]$Builder.AppendLine("const uint8_t $($Name)Bitmaps[] PROGMEM = {")
    [void]$Builder.Append("    ")

    for ($i = 0; $i -lt $Bytes.Count; $i++) {
        if ($i -gt 0) {
            [void]$Builder.Append(", ")
            if (($i % 12) -eq 0) {
                [void]$Builder.AppendLine()
                [void]$Builder.Append("    ")
            }
        }

        [void]$Builder.Append(("0x{0:X2}" -f $Bytes[$i]))
    }

    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine("};")
    [void]$Builder.AppendLine()
}

function New-Glyph {
    param(
        [System.Drawing.Font]$Font,
        [System.Drawing.StringFormat]$Format,
        [int]$ByteValue,
        [int]$Codepoint,
        [int]$Ascent
    )

    $text = [string][char]$Codepoint
    $measureContext = New-MeasureGraphics
    $measure = $measureContext.Graphics.MeasureString($text, $Font, 1000, $Format)
    $advance = [Math]::Ceiling($measure.Width)
    if ($advance -lt 1) { $advance = [Math]::Ceiling($Font.Size / 3) }
    if ($ByteValue -eq 0x20 -or $ByteValue -eq 0xA0) { $advance = [Math]::Max(1, $advance) }

    $measureContext.Graphics.Dispose()
    $measureContext.Bitmap.Dispose()

    $pad = [Math]::Max(12, [Math]::Ceiling($Font.Size / 2))
    $canvasWidth = [Math]::Max(64, [Math]::Ceiling($measure.Width + ($pad * 2) + $Font.Size))
    $canvasHeight = [Math]::Max(64, [Math]::Ceiling($Font.GetHeight() + ($pad * 2) + $Font.Size))
    $baseline = $pad + $Ascent

    $bitmap = [System.Drawing.Bitmap]::new($canvasWidth, $canvasHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.PageUnit = [System.Drawing.GraphicsUnit]::Pixel
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
    $graphics.DrawString($text, $Font, [System.Drawing.Brushes]::Black, [System.Drawing.PointF]::new($pad, $pad), $Format)

    $minX = $canvasWidth
    $minY = $canvasHeight
    $maxX = -1
    $maxY = -1

    for ($y = 0; $y -lt $canvasHeight; $y++) {
        for ($x = 0; $x -lt $canvasWidth; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            if ($pixel.A -gt 96) {
                if ($x -lt $minX) { $minX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }

    $bits = New-Object "System.Collections.Generic.List[bool]"
    $width = 0
    $height = 0
    $xOffset = 0
    $yOffset = 1

    if ($maxX -ge $minX -and $maxY -ge $minY -and $ByteValue -ne 0x20 -and $ByteValue -ne 0xA0) {
        $width = $maxX - $minX + 1
        $height = $maxY - $minY + 1
        $xOffset = $minX - $pad
        $yOffset = $minY - $baseline

        for ($y = $minY; $y -le $maxY; $y++) {
            for ($x = $minX; $x -le $maxX; $x++) {
                $pixel = $bitmap.GetPixel($x, $y)
                $bits.Add($pixel.A -gt 96)
            }
        }

        while (($bits.Count % 8) -ne 0) {
            $bits.Add($false)
        }
    }

    $graphics.Dispose()
    $bitmap.Dispose()

    return @{
        Byte = $ByteValue
        Codepoint = $Codepoint
        Width = $width
        Height = $height
        Advance = [Math]::Min(255, [Math]::Max(1, $advance))
        XOffset = [Math]::Max(-128, [Math]::Min(127, $xOffset))
        YOffset = [Math]::Max(-128, [Math]::Min(127, $yOffset))
        Bits = $bits
    }
}

function Add-FontDefinition {
    param(
        [System.Text.StringBuilder]$Builder,
        [hashtable]$Spec
    )

    $font = [System.Drawing.Font]::new($FontFamilyName, [single]$Spec.PixelSize, $Spec.Style, [System.Drawing.GraphicsUnit]::Pixel)
    if ($font.Name -ne $FontFamilyName) {
        throw "Font family '$FontFamilyName' was not found. GDI selected '$($font.Name)'."
    }

    $format = [System.Drawing.StringFormat]::GenericTypographic.Clone()
    $format.FormatFlags = $format.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

    $family = $font.FontFamily
    $emHeight = $family.GetEmHeight($font.Style)
    $ascent = [Math]::Ceiling($font.Size * $family.GetCellAscent($font.Style) / $emHeight)
    $lineSpacing = [Math]::Ceiling($font.Size * $family.GetLineSpacing($font.Style) / $emHeight)

    $bytes = New-Object "System.Collections.Generic.List[byte]"
    $glyphs = New-Object "System.Collections.Generic.List[object]"

    for ($byteValue = 0x20; $byteValue -le 0xFF; $byteValue++) {
        $glyph = New-Glyph -Font $font -Format $format -ByteValue $byteValue -Codepoint $cp1251[$byteValue] -Ascent $ascent
        $glyph.Offset = $bytes.Count

        for ($i = 0; $i -lt $glyph.Bits.Count; $i += 8) {
            $packed = 0
            for ($bit = 0; $bit -lt 8; $bit++) {
                if ($glyph.Bits[$i + $bit]) {
                    $packed = $packed -bor (0x80 -shr $bit)
                }
            }
            $bytes.Add([byte]$packed)
        }

        $glyphs.Add($glyph)
    }

    if ($bytes.Count -gt 65535) {
        throw "Bitmap for $($Spec.Name) is too large for GFXglyph::bitmapOffset: $($bytes.Count) bytes."
    }

    Add-ByteArrayLiteral -Builder $Builder -Name $Spec.Name -Bytes $bytes

    [void]$Builder.AppendLine("const GFXglyph $($Spec.Name)Glyphs[] PROGMEM = {")
    for ($i = 0; $i -lt $glyphs.Count; $i++) {
        $glyph = $glyphs[$i]
        $suffix = if ($i -lt $glyphs.Count - 1) { "," } else { "" }
        [void]$Builder.AppendLine(("    {{ {0,5}, {1,3}, {2,3}, {3,3}, {4,4}, {5,4} }}{6} // 0x{7:X2} U+{8:X4}" -f `
            $glyph.Offset, $glyph.Width, $glyph.Height, $glyph.Advance, $glyph.XOffset, $glyph.YOffset, $suffix, $glyph.Byte, $glyph.Codepoint))
    }
    [void]$Builder.AppendLine("};")
    [void]$Builder.AppendLine()

    [void]$Builder.AppendLine("const GFXfont $($Spec.Name) PROGMEM = {")
    [void]$Builder.AppendLine("    (uint8_t *)$($Spec.Name)Bitmaps,")
    [void]$Builder.AppendLine("    (GFXglyph *)$($Spec.Name)Glyphs,")
    [void]$Builder.AppendLine(("    0x20, 0xFF, {0}" -f $lineSpacing))
    [void]$Builder.AppendLine("};")
    [void]$Builder.AppendLine()

    $font.Dispose()
    $format.Dispose()
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and !(Test-Path $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine("#pragma once")
[void]$builder.AppendLine("#include <Adafruit_GFX.h>")
[void]$builder.AppendLine()
[void]$builder.AppendLine("// Generated by tools/generate_cp1251_fonts.ps1.")
[void]$builder.AppendLine("// Source font family: $FontFamilyName.")
[void]$builder.AppendLine("// Glyph bytes use the Windows-1251 layout; source text is converted from UTF-8 at runtime.")
[void]$builder.AppendLine()

foreach ($spec in $fontSpecs) {
    Add-FontDefinition -Builder $builder -Spec $spec
}

[System.IO.File]::WriteAllText((Resolve-Path -LiteralPath ".").Path + [System.IO.Path]::DirectorySeparatorChar + $OutputPath, $builder.ToString(), [System.Text.Encoding]::ASCII)
Write-Host "Generated $OutputPath"

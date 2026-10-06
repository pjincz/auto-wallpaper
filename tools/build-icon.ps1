$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$sizes = @(16,20,24,32,48,64,128,256)
$images = @()
foreach ($size in $sizes) {
    $bmp = [Drawing.Bitmap]::new($size*4,$size*4)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.ScaleTransform($size/64.0,$size/64.0)
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc(8,8,104,104,180,90); $path.AddArc(144,8,104,104,270,90)
    $path.AddArc(144,144,104,104,0,90); $path.AddArc(8,144,104,104,90,90); $path.CloseFigure()
    $brush = [Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.Point]::new(0,8),[Drawing.Point]::new(0,248),[Drawing.ColorTranslator]::FromHtml('#16384f'),[Drawing.ColorTranslator]::FromHtml('#0b202e'))
    $g.FillPath($brush,$path); $brush.Dispose(); $path.Dispose()
    $brush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml('#ffc970'))
    $g.FillEllipse($brush,151,54,46,46); $brush.Dispose()
    foreach ($shape in @(@{Color='#75dbc6';Points=@(34,188,98,86,170,188)},@{Color='#31a9ae';Points=@(116,188,164,115,222,188)},@{Color='#effff9';Points=@(79,117,98,86,120,117,98,107)})) {
        $points = [Collections.Generic.List[Drawing.PointF]]::new()
        for ($i=0; $i -lt $shape.Points.Count; $i+=2) { $points.Add([Drawing.PointF]::new($shape.Points[$i],$shape.Points[$i+1])) }
        $brush = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($shape.Color))
        $g.FillPolygon($brush,$points.ToArray()); $brush.Dispose()
    }
    $pen = [Drawing.Pen]::new([Drawing.ColorTranslator]::FromHtml('#effff9'),9)
    $pen.StartCap='Round'; $pen.EndCap='Round'; $g.DrawLine($pen,48,208,208,208); $pen.Dispose(); $g.Dispose()
    $small = [Drawing.Bitmap]::new($size,$size)
    $g = [Drawing.Graphics]::FromImage($small); $g.InterpolationMode='HighQualityBicubic'
    $g.DrawImage($bmp,0,0,$size,$size); $g.Dispose(); $bmp.Dispose()
    $stream = [IO.MemoryStream]::new(); $small.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $images += ,$stream.ToArray()
    if ($size -eq 256) { $small.Save((Join-Path $root 'assets\app.png'),[Drawing.Imaging.ImageFormat]::Png) }
    $small.Dispose(); $stream.Dispose()
}
$file = [IO.File]::Create((Join-Path $root 'assets\app.ico'))
$writer = [IO.BinaryWriter]::new($file)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i=0; $i -lt $sizes.Count; ++$i) {
    $dimension = $sizes[$i] % 256
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write([byte[]]$image) }
$writer.Dispose()

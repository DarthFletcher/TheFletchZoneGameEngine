param(
    [string]$ProjectPath = "TheFletchZoneGameEngine.vcxproj"
)

if (-not (Test-Path $ProjectPath)) {
    throw "Project file not found: $ProjectPath"
}

[xml]$x = Get-Content -Path $ProjectPath
$ns = New-Object System.Xml.XmlNamespaceManager($x.NameTable)
$ns.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')

$targets = @(
    'shaders\scene_grid_ps.hlsl',
    'shaders\scene_grid_vs.hlsl'
)

foreach ($t in $targets) {
    $nodes = $x.SelectNodes("//msb:FxCompile[@Include='$t']", $ns)
    foreach ($n in @($nodes)) {
        [void]$n.ParentNode.RemoveChild($n)
    }
}

$x.Save($ProjectPath)
Write-Host "Removed scene grid shaders from FxCompile in $ProjectPath"
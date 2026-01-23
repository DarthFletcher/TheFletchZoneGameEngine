param(
    [string]$ProjectFile = "..\\TheFletchZoneGameEngine.vcxproj"
)

$projPath = Resolve-Path $ProjectFile
[xml]$xml = Get-Content $projPath

$ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$ns.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")

$targets = @(
    "shaders\\scene_triangle_ps.hlsl",
    "shaders\\scene_triangle_vs.hlsl",
    "shaders\\scene_grid_ps.hlsl",
    "shaders\\scene_grid_vs.hlsl"
)

# Find FxCompile items for scene shaders and remove them
$fxNodes = $xml.SelectNodes("//msb:FxCompile[@Include]", $ns)
$removedAny = $false
foreach ($n in @($fxNodes)) {
    $inc = $n.GetAttribute("Include")
    if ($targets -contains $inc) {
        $null = $n.ParentNode.RemoveChild($n)
        $removedAny = $true
    }
}

# Ensure there is a Remove entry so MSBuild doesn't pick them up implicitly
$itemGroup = $xml.SelectSingleNode("//msb:ItemGroup[msb:FxCompile]", $ns)
if (-not $itemGroup) {
    $itemGroup = $xml.CreateElement("ItemGroup", $xml.DocumentElement.NamespaceURI)
    $null = $xml.DocumentElement.AppendChild($itemGroup)
}

foreach ($t in $targets) {
    $existing = $xml.SelectSingleNode("//msb:FxCompile[@Remove='$t']", $ns)
    if (-not $existing) {
        $remove = $xml.CreateElement("FxCompile", $xml.DocumentElement.NamespaceURI)
        $null = $remove.SetAttribute("Remove", $t)
        $null = $itemGroup.AppendChild($remove)
    }
}

if ($removedAny) {
    Write-Host "Patched: removed scene shaders from FxCompile in $projPath"
}
else {
    Write-Host "No scene shader FxCompile entries found to remove in $projPath (ensured Remove entries exist)"
}

$xml.Save($projPath)

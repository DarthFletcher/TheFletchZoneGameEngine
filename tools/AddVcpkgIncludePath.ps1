# Add vcpkg include path to project file for DXC headers
$projectFile = "TheFletchZoneGameEngine.vcxproj"
$vcpkgInclude = Resolve-Path "vcpkg_installed\x64-windows\include" | Select-Object -ExpandProperty Path

Write-Host "Adding vcpkg include path: $vcpkgInclude"

$content = Get-Content $projectFile -Raw

# Check if vcpkg path already exists
if ($content -match [regex]::Escape("vcpkg_installed\x64-windows\include")) {
    Write-Host "? vcpkg path already present in project file"
    exit 0
}

# Find the Debug|x64 AdditionalIncludeDirectories line and prepend vcpkg path
$pattern = '(<ItemDefinitionGroup Condition="''[$][(]Configuration[)]|[$][(]Platform[)]''==''Debug\|x64''">\s+<ClCompile>.*?<AdditionalIncludeDirectories>)([^<]+)(</AdditionalIncludeDirectories>)'
$replacement = "`${1}$vcpkgInclude;`${2}`${3}"

$newContent = $content -replace $pattern, $replacement

if ($newContent -ne $content) {
    Set-Content $projectFile $newContent -Encoding UTF8
    Write-Host "? Project file updated successfully - vcpkg include added"
} else {
    Write-Host "?? Pattern not found - manual edit may be required"
}
